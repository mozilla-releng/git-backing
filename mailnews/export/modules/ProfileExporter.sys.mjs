/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  MailTelemetry: "moz-src:///comm/mail/modules/MailTelemetry.sys.mjs",
  ZipArchive: "resource:///modules/ZipArchive.sys.mjs",
});

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
   */
  async startExport(targetFile) {
    const profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
    const rootPathCount = PathUtils.split(profileDir.parent.path).length;
    const zipEntryMap = await this.#collectFilesToZip(
      rootPathCount,
      profileDir
    );
    const entries = [];
    for (const [name, file] of zipEntryMap) {
      // The backup may be saved inside the profile. Adding the archive to
      // itself would never finish, because it grows as fast as it is read.
      if (!file.equals(targetFile)) {
        entries.push({ name, path: file.path });
      }
    }

    lazy.MailTelemetry.recordProfileExport("started");
    try {
      await lazy.ZipArchive.create(targetFile.path, entries, (done, total) =>
        this.onProgress(done, total)
      );
    } catch (e) {
      // Do not leave a partial archive behind looking like a usable backup.
      await IOUtils.remove(targetFile.path, { ignoreAbsent: true });
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
        if (IGNORE_PATHS.includes(parts[1])) {
          continue;
        }
        // Path separator inside a zip file is always "/".
        zipEntryMap.set(parts.join("/"), file);
      }
    }
    return zipEntryMap;
  }
}
