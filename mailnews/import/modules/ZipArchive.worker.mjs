/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Reads and writes zip archives off the main thread, for ZipArchive.sys.mjs.
 * Each worker runs one operation, posting progress messages followed by a
 * result or an error.
 */

import {
  configure,
  Reader,
  Writer,
  ZipReader,
  ZipWriter,
} from "moz-src:///comm/third_party/zipjs/lib/zip-core-custom.js";

// How much data zip.js passes through its streams at once.
const CHUNK_SIZE = 4 * 1024 * 1024;

// How much extracted or archived data to collect before writing it to disk.
const WRITE_BUFFER_SIZE = 16 * 1024 * 1024;

// The shortest time between two progress messages.
const PROGRESS_INTERVAL_MS = 250;

// Entries are compressed and decompressed on this worker's own thread, by
// Gecko's CompressionStream and DecompressionStream. The vendored zip.js has
// no codec of its own, so it is never given a compression level: it only
// uses CompressionStream for the default level.
configure({ chunkSize: CHUNK_SIZE, useWebWorkers: false });

/**
 * Reads a file at any offset, without reading it all into memory.
 */
class DiskFileReader extends Reader {
  #path;
  #file;

  /**
   * @param {string} path - Absolute path of the file to read.
   */
  constructor(path) {
    super();
    this.#path = path;
  }

  /**
   * Open the file, if it is not open yet. zip.js does this in init(), but
   * opening first lets a caller find out whether the file can be read before
   * zip.js starts an entry for it.
   */
  open() {
    this.#file ??= IOUtils.openFileForSyncReading(this.#path);
    this.size = this.#file.size;
  }

  init() {
    this.open();
    super.init();
  }

  readUint8Array(offset, length) {
    const data = new Uint8Array(
      Math.max(0, Math.min(length, this.size - offset))
    );
    this.#file.readBytesInto(data, offset);
    return data;
  }

  close() {
    this.#file?.close();
  }
}

/**
 * Writes a new file from the start, in large pieces. zip.js keeps count of
 * the bytes written in this.size itself.
 */
class DiskFileWriter extends Writer {
  #path;
  #buffer;
  #bufferedSize = 0;
  #created = false;

  /**
   * @param {string} path - Absolute path of the file to write. Any existing
   *   file is replaced.
   * @param {number} [expectedSize=WRITE_BUFFER_SIZE] - How many bytes the
   *   file is expected to hold, so that a small file gets a small buffer.
   */
  constructor(path, expectedSize = WRITE_BUFFER_SIZE) {
    super();
    this.#path = path;
    this.#buffer = new Uint8Array(Math.min(expectedSize, WRITE_BUFFER_SIZE));
  }

  /**
   * Buffer data for the file, writing it out when the buffer is full.
   *
   * @param {Uint8Array} data - The next bytes of the file.
   */
  async writeUint8Array(data) {
    if (this.#bufferedSize + data.length > this.#buffer.length) {
      await this.#flush();
    }
    if (data.length > this.#buffer.length) {
      await this.#write(data);
      return;
    }
    this.#buffer.set(data, this.#bufferedSize);
    this.#bufferedSize += data.length;
  }

  async getData() {
    await this.#flush();
    if (!this.#created) {
      await this.#write(new Uint8Array());
    }
  }

  async #flush() {
    if (this.#bufferedSize) {
      const size = this.#bufferedSize;
      this.#bufferedSize = 0;
      await this.#write(this.#buffer.subarray(0, size));
    }
  }

  /**
   * Write to the file, replacing it on the first write and appending after.
   *
   * @param {Uint8Array} data
   */
  async #write(data) {
    await IOUtils.write(this.#path, data, {
      mode: this.#created ? "append" : "overwrite",
    });
    this.#created = true;
  }
}

/**
 * Counts bytes processed across many entries, and posts the total now and
 * then.
 */
class ProgressReporter {
  #totalBytes;
  #doneBytes = 0;
  #lastReport = 0;

  /**
   * @param {number} totalBytes
   */
  constructor(totalBytes) {
    this.#totalBytes = totalBytes;
  }

  /**
   * @param {number} bytes - Bytes processed since the last call.
   */
  add(bytes) {
    this.#doneBytes += bytes;
    const now = Date.now();
    if (now - this.#lastReport >= PROGRESS_INTERVAL_MS) {
      this.#lastReport = now;
      this.#post();
    }
  }

  finish() {
    this.#doneBytes = this.#totalBytes;
    this.#post();
  }

  #post() {
    self.postMessage({
      progress: [Math.min(this.#doneBytes, this.#totalBytes), this.#totalBytes],
    });
  }

  /**
   * Make an onprogress callback for zip.js, which reports the bytes of one
   * entry processed so far.
   *
   * @returns {function(number):void}
   */
  forEntry() {
    let entryBytes = 0;
    return done => {
      this.add(done - entryBytes);
      entryBytes = done;
    };
  }
}

/**
 * List the entries of an archive.
 *
 * @param {object} args
 * @param {string} args.archivePath
 * @returns {Promise<{name: string, directory: boolean, size: number}[]>}
 */
async function listEntries({ archivePath }) {
  const reader = new DiskFileReader(archivePath);
  const zipReader = new ZipReader(reader);
  try {
    const entries = await zipReader.getEntries();
    return entries.map(entry => ({
      name: entry.filename,
      directory: entry.directory,
      size: entry.uncompressedSize,
    }));
  } finally {
    await zipReader.close();
    reader.close();
  }
}

/**
 * Extract some entries of an archive to files, one after another. Entries
 * that fail are reported, rather than stopping the others.
 *
 * @param {object} args
 * @param {string} args.archivePath
 * @param {{name: string, path: string}[]} args.entries - The entries to
 *   extract, and absolute paths to extract each one to.
 * @returns {Promise<{name: string, message: string}[]>} The entries that
 *   could not be extracted.
 */
async function extract({ archivePath, entries }) {
  const reader = new DiskFileReader(archivePath);
  const zipReader = new ZipReader(reader, {
    checkCrc32: true,
    checkOverlappingEntry: true,
  });
  const failures = [];
  try {
    const entriesByName = new Map();
    for (const entry of await zipReader.getEntries()) {
      entriesByName.set(entry.filename, entry);
    }
    const items = entries.map(({ name: entryName, path }) => ({
      entryName,
      path,
      entry: entriesByName.get(entryName),
    }));
    const progress = new ProgressReporter(
      items.reduce((sum, { entry }) => sum + (entry?.uncompressedSize ?? 0), 0)
    );
    for (const { entryName, path, entry } of items) {
      try {
        if (!entry || entry.directory) {
          throw new Error("No such file in the archive");
        }
        await IOUtils.makeDirectory(PathUtils.parent(path));
        await entry.getData(new DiskFileWriter(path, entry.uncompressedSize), {
          onprogress: progress.forEntry(),
        });
        await IOUtils.setModificationTime(path, entry.lastModDate.getTime());
      } catch (e) {
        failures.push({ name: entryName, message: e.message ?? String(e) });
      }
    }
    progress.finish();
  } finally {
    await zipReader.close();
    reader.close();
  }
  return failures;
}

/**
 * Write files into a new archive, compressing them with deflate. Files that
 * are removed before their turn are left out; any other file that cannot be
 * read fails the archive.
 *
 * @param {object} args
 * @param {string} args.archivePath - Absolute path of the archive to create.
 *   Any existing file is replaced.
 * @param {{name: string, path: string}[]} args.entries - Names for the
 *   entries, and absolute paths of the files to read each one from.
 */
async function create({ archivePath, entries }) {
  const sources = [];
  let totalBytes = 0;
  for (const { name: entryName, path } of entries) {
    let stat;
    try {
      stat = await IOUtils.stat(path);
    } catch (e) {
      // Profiles are in use while they are exported, and an export can take
      // minutes, so a short-lived file such as .startup-incomplete may have
      // been removed since it was listed. It is no longer part of the
      // profile, so it is left out rather than failing the whole archive.
      if (!(await IOUtils.exists(path))) {
        continue;
      }
      throw new Error(`Failed to add ${entryName}: ${e}`, { cause: e });
    }
    sources.push({
      entryName,
      path,
      size: stat.size,
      lastModified: stat.lastModified,
    });
    totalBytes += stat.size;
  }
  const progress = new ProgressReporter(totalBytes);
  const zipWriter = new ZipWriter(new DiskFileWriter(archivePath));
  // Entries are added one at a time. Added together, zip.js would hold all
  // but one of them in memory until its turn to be written.
  for (const { entryName, path, size, lastModified } of sources) {
    const reader = new DiskFileReader(path);
    try {
      try {
        reader.open();
      } catch (e) {
        if (!(await IOUtils.exists(path))) {
          progress.add(size);
          continue;
        }
        throw e;
      }
      await zipWriter.add(entryName, reader, {
        lastModDate: new Date(lastModified),
        onprogress: progress.forEntry(),
      });
    } catch (e) {
      throw new Error(`Failed to add ${entryName}: ${e}`, { cause: e });
    } finally {
      reader.close();
    }
  }
  await zipWriter.close();
  progress.finish();
}

const OPERATIONS = { listEntries, extract, create };

self.onmessage = async ({ data: { operation, args } }) => {
  try {
    const result = await OPERATIONS[operation](args);
    self.postMessage({ result });
  } catch (e) {
    self.postMessage({ error: e.message ?? String(e) });
  }
};
