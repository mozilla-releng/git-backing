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
   * List the entries of an archive.
   *
   * @param {string} archivePath - Absolute path of the archive.
   * @returns {Promise<ZipArchiveEntry[]>}
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
   * @returns {Promise} Rejects if any file could not be added.
   */
  create(archivePath, entries, onProgress) {
    return run("create", { archivePath, entries }, onProgress);
  },
};

/**
 * Run one operation on a new worker.
 *
 * @param {string} operation - The name of a function in the worker.
 * @param {object} args - Arguments for the function.
 * @param {ZipProgressCallback} [onProgress]
 * @returns {Promise<any>} The result of the function.
 */
function run(operation, args, onProgress) {
  const worker = new ChromeWorker(WORKER_URL, { type: "module" });
  return new Promise((resolve, reject) => {
    worker.addEventListener("error", event => {
      reject(new Error(`ZipArchive worker failed: ${event.message}`));
    });
    worker.addEventListener("message", ({ data }) => {
      if (data.progress) {
        onProgress?.(...data.progress);
      } else if (data.error) {
        reject(new Error(data.error));
      } else {
        resolve(data.result);
      }
    });
    worker.postMessage({ operation, args });
  }).finally(() => worker.terminate());
}
