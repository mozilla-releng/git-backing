/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const WORKER_URL = "resource:///modules/ZipArchive.worker.mjs";

/**
 * @callback ZipProgressCallback
 * @param {number} doneBytes - Bytes processed so far.
 * @param {number} totalBytes - Bytes to process in all.
 */

/**
 * @typedef {object} ZipArchiveEntry
 * @property {string} name - The path of the entry in the archive.
 * @property {boolean} directory - True if the entry is a directory.
 * @property {number} size - The uncompressed size of the entry, in bytes.
 */

/**
 * @typedef {object} ZipEntryFile
 * @property {string} name - The path of an entry in the archive.
 * @property {string} path - The absolute path of a file on disk.
 */

/**
 * Reads and writes zip archives, including ZIP64 archives larger than 4 GB,
 * on a worker thread. Data is streamed between the archive and the files on
 * disk, so neither needs to fit in memory.
 */
export const ZipArchive = {
  /**
   * The name of the error thrown for an archive larger than 4 GB that was
   * written without ZIP64, and so has truncated offsets. Older versions of
   * Thunderbird made profile backups like this.
   */
  TRUNCATED_ERROR: "ZipTruncatedError",

  /**
   * List the entries of an archive.
   *
   * @param {string} archivePath - Absolute path of the archive.
   * @returns {Promise<ZipArchiveEntry[]>} Rejects with TRUNCATED_ERROR as the
   *   error name if the archive's offsets are truncated.
   */
  listEntries(archivePath) {
    return run("listEntries", { archivePath });
  },

  /**
   * Extract entries of an archive to files. Missing parent directories are
   * created. An entry that fails does not stop the others.
   *
   * @param {string} archivePath - Absolute path of the archive.
   * @param {ZipEntryFile[]} entries - The entries to extract, and the paths
   *   to extract them to.
   * @param {ZipProgressCallback} [onProgress]
   * @returns {Promise<{name: string, message: string}[]>} The entries that
   *   could not be extracted, and why.
   */
  extract(archivePath, entries, onProgress) {
    return run("extract", { archivePath, entries }, onProgress);
  },

  /**
   * Create an archive from files, compressed with deflate.
   *
   * @param {string} archivePath - Absolute path of the archive to create. Any
   *   existing file is replaced.
   * @param {ZipEntryFile[]} entries - Names for the entries, and the paths of
   *   the files to read them from.
   * @param {ZipProgressCallback} [onProgress]
   * @param {object} [options]
   * @param {AbortSignal} [options.signal] - Stops creating the archive when
   *   aborted. The partial archive is left for the caller to remove.
   * @returns {Promise} Rejects if any file could not be added, or with an
   *   AbortError if stopped.
   */
  create(archivePath, entries, onProgress, { signal } = {}) {
    return run("create", { archivePath, entries }, onProgress, signal);
  },
};

/**
 * Run one operation on a new worker.
 *
 * @param {string} operation - The name of a function in the worker.
 * @param {object} args - Arguments for the function.
 * @param {ZipProgressCallback} [onProgress]
 * @param {AbortSignal} [signal] - Asks the worker to stop when aborted. The
 *   promise settles once the worker has stopped, so that it is no longer
 *   writing to any file.
 * @returns {Promise<any>} The result of the function.
 */
async function run(operation, args, onProgress, signal) {
  signal?.throwIfAborted();
  const worker = new ChromeWorker(WORKER_URL, { type: "module" });
  const abort = () => worker.postMessage({ abort: true });
  signal?.addEventListener("abort", abort, { once: true });
  return new Promise((resolve, reject) => {
    worker.addEventListener("error", event => {
      reject(new Error(`ZipArchive worker failed: ${event.message}`));
    });
    worker.addEventListener("message", ({ data }) => {
      if (data.progress) {
        onProgress?.(...data.progress);
      } else if (data.error) {
        const error = new Error(data.error);
        error.name = data.errorName;
        reject(error);
      } else {
        resolve(data.result);
      }
    });
    worker.postMessage({ operation, args });
  }).finally(() => {
    signal?.removeEventListener("abort", abort);
    worker.terminate();
  });
}
