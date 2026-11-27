/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { FileUtils } from "resource://gre/modules/FileUtils.sys.mjs";

const { MODE_WRONLY, MODE_CREATE } = FileUtils;

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  MailTelemetry: "moz-src:///comm/mail/modules/MailTelemetry.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// Report progress, and let other events run, after this many files.
const FILES_PER_PROGRESS_UPDATE = 10;

// No need to backup those paths, they are not used when importing.
const IGNORE_PATHS = [
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
   * Callback for progress updates.
   */
  onProgress = () => {};

  /**
   * Export the current profile to the specified target zip file.
   *
   * @param {nsIFile} targetFile - A target zip file to write to.
   */
  async startExport(targetFile) {
    const zipW = Components.Constructor(
      "@mozilla.org/zipwriter;1",
      "nsIZipWriter"
    )();
    zipW.open(targetFile, MODE_WRONLY | MODE_CREATE);
    lazy.MailTelemetry.recordProfileExport("started");
    const profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
    const rootPathCount = PathUtils.split(profileDir.parent.path).length;
    const zipEntryMap = await this.#collectFilesToZip(
      rootPathCount,
      profileDir
    );

    let i = 0;
    try {
      for (const [path, file] of zipEntryMap) {
        try {
          zipW.addEntryFile(
            path,
            Ci.nsIZipWriter.COMPRESSION_NONE, // bigger file but much faster
            file,
            false
          );
        } catch (e) {
          throw new Error(`Profile export failed adding ${path}`, { cause: e });
        }
        if (++i % FILES_PER_PROGRESS_UPDATE === 0) {
          this.onProgress(i, zipEntryMap.size);
          await new Promise(resolve => lazy.setTimeout(resolve));
        }
      }
      this.onProgress(zipEntryMap.size, zipEntryMap.size);
      zipW.close();
    } catch (e) {
      try {
        zipW.close();
      } catch (closeError) {
        this._logger.error("Failed to close the zip writer", closeError);
      }
      // Do not leave a partial archive behind looking like a usable backup.
      await IOUtils.remove(targetFile.path, { ignoreAbsent: true });
      throw e;
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
