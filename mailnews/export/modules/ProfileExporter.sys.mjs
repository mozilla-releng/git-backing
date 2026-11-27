/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  MailTelemetry: "moz-src:///comm/mail/modules/MailTelemetry.sys.mjs",
  ZipArchive: "resource:///modules/ZipArchive.sys.mjs",
});

// The backup is written under its name with this added, and renamed when it is
// complete, so that an export that does not finish never leaves a file
// looking like a usable backup.
const PARTIAL_SUFFIX = ".part";

// A partial backup left by an export that never finished, such as one cut
// short by a crash. Never part of the profile, so it is not backed up.
const STALE_PARTIAL_SUFFIX = ".zip" + PARTIAL_SUFFIX;

// No need to backup those paths, they are not used when importing.
const IGNORE_PATHS = [
  ".startup-incomplete",
  "cache2",
  "chrome_debugger_profile",
  "crashes",
  "datareporting",
  "extensions",
  "extension-store",
  "logs",
  "lock",
  "minidumps",
  "parent.lock",
  "shader-cache",
  "saved-telemetry-pings",
  "security_state",
  "storage",
  "xulstore",
];

/**
 * A module to export the current profile to a zip file.
 */
export class ProfileExporter {
  _logger = console.createInstance({
    prefix: "mail.export",
    maxLogLevel: "Warn",
    maxLogLevelPref: "mail.export.loglevel",
  });

  /**
   * Callback for progress updates, in bytes written out of bytes to write.
   *
   * @type {function(number, number):void}
   */
  onProgress = () => {};

  /**
   * Export the current profile to the specified target zip file.
   *
   * @param {nsIFile} targetFile - A target zip file to write to.
   * @param {object} [options]
   * @param {AbortSignal} [options.signal] - Cancels the export when aborted.
   *   The partial archive is removed, any file already at `targetFile` is
   *   left as it was, and the export rejects with an AbortError. Quitting
   *   Thunderbird cancels the export the same way, and waits until the
   *   partial archive is removed.
   */
  async startExport(targetFile, { signal } = {}) {
    const profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
    const rootPathCount = PathUtils.split(profileDir.parent.path).length;
    const zipEntryMap = await this.#collectFilesToZip(
      rootPathCount,
      profileDir
    );
    const partialPath = targetFile.path + PARTIAL_SUFFIX;
    const entries = [];
    for (const [name, file] of zipEntryMap) {
      // The backup may be saved inside the profile. Adding the archive to
      // itself would never finish, because it grows as fast as it is read.
      // So can the partial archive of an earlier export to the same name
      // that never finished.
      if (!file.equals(targetFile) && file.path != partialPath) {
        entries.push({ name, path: file.path });
      }
    }

    // Quitting would otherwise kill the worker part way through, and leave
    // the partial archive behind with nothing to remove it.
    const quitController = new AbortController();
    const exportSignal = signal
      ? AbortSignal.any([signal, quitController.signal])
      : quitController.signal;
    const exported = this.#writeArchive(
      partialPath,
      targetFile.path,
      entries,
      exportSignal
    );
    const shutdownBlocker = async () => {
      quitController.abort();
      await exported.catch(() => {});
    };
    lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
      "ProfileExporter: cancelling the export and removing the partial backup",
      shutdownBlocker
    );
    try {
      await exported;
    } finally {
      lazy.AsyncShutdown.appShutdownConfirmed.removeBlocker(shutdownBlocker);
    }
  }

  /**
   * Write the archive under a temporary name, then rename it.
   *
   * @param {string} partialPath - Where to write the archive until complete.
   * @param {string} targetPath - Where the complete archive goes.
   * @param {{name: string, path: string}[]} entries - The files to add.
   * @param {AbortSignal} signal - Cancels writing when aborted.
   */
  async #writeArchive(partialPath, targetPath, entries, signal) {
    lazy.MailTelemetry.recordProfileExport("started");
    try {
      await lazy.ZipArchive.create(
        partialPath,
        entries,
        (done, total) => this.onProgress(done, total),
        { signal }
      );
      await IOUtils.move(partialPath, targetPath);
    } catch (e) {
      await IOUtils.remove(partialPath, { ignoreAbsent: true });
      if (signal.aborted) {
        throw e;
      }
      throw new Error(`Profile export failed: ${e.message}`, { cause: e });
    }
    lazy.MailTelemetry.recordProfileExport("completed", "complete");
  }

  /**
   * Recursively collect files to be zipped, save the entries into zipEntryMap.
   *
   * @param {number} rootPathCount - The count of rootPath parts.
   * @param {nsIFile} folder - The folder to search for files to zip.
   * @returns {Map<string, nsIFile>} Collection of files to be zipped.
   */
  async #collectFilesToZip(rootPathCount, folder) {
    const zipEntryMap = new Map();
    for (const file of folder.directoryEntries) {
      if (!(await IOUtils.exists(file.path))) {
        continue;
      }
      if ((await IOUtils.stat(file.path)).type == "directory") {
        for (const [key, value] of await this.#collectFilesToZip(
          rootPathCount,
          file
        )) {
          zipEntryMap.set(key, value);
        }
      } else {
        // We don't want to include the rootPath part in the zip file.
        const parts = PathUtils.split(file.path).slice(rootPathCount);
        // Parts look like this: ["profile-default", "lock"].
        if (
          IGNORE_PATHS.includes(parts[1]) ||
          file.leafName.endsWith(STALE_PARTIAL_SUFFIX)
        ) {
          continue;
        }
        // Path separator inside a zip file is always "/".
        zipEntryMap.set(parts.join("/"), file);
      }
    }
    return zipEntryMap;
  }
}
