/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { setTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { ZipArchive } from "resource:///modules/ZipArchive.sys.mjs";

/**
 * An object to represent a source profile to import from.
 *
 * @typedef {object} SourceProfile
 * @property {string} name - The profile name.
 * @property {nsIFile} dir - The profile location.
 *
 * An object to represent items to import.
 * @typedef {object} ImportItems
 * @property {boolean} accounts - Whether to import accounts and settings.
 * @property {boolean} addressBooks - Whether to import address books.
 * @property {boolean} calendars - Whether to import calendars.
 * @property {boolean} mailMessages - Whether to import mail messages.
 */

/**
 * Common interfaces shared by profile importers.
 *
 * @abstract
 */
export class BaseProfileImporter {
  /** @type {boolean} - Whether to allow importing from a user picked dir. */
  USE_FILE_PICKER = true;

  /** @type {ImportItems} */
  SUPPORTED_ITEMS = {
    accounts: true,
    addressBooks: true,
    calendars: true,
    mailMessages: true,
  };

  /** When importing from a zip file, ignoring these folders. */
  IGNORE_DIRS = [];

  /**
   * Callback for progress updates.
   *
   * @param {number} _current - Current imported items count.
   * @param {number} _total - Total items count.
   */
  onProgress = (_current, _total) => {};

  /**
   * @returns {SourceProfile[]} Profiles found on this machine.
   */
  async getSourceProfiles() {
    throw Components.Exception(
      `getSourceProfiles not implemented in ${this.constructor.name}`,
      Cr.NS_ERROR_NOT_IMPLEMENTED
    );
  }

  /**
   * Test `sourceProfileDir` for required files and prevent progress if they
   * are not present. Override this in subclasses.
   *
   * @param {nsIFile} _sourceProfileDir - A directory or file (likely but not
   *   necessarily a zip file) to be imported.
   * @returns {Promise<boolean>} false if importing this source should not
   *   continue.
   */
  async validateSource(_sourceProfileDir) {
    return true;
  }

  /**
   * Test a zip file for required files.
   *
   * @param {string[]} _entryNames - The names of the entries in the zip file
   *   to be imported.
   * @param {string} [_prefix=""] - A prefix to apply to all paths being checked.
   * @returns {boolean} false if importing this source should not continue.
   */
  validateZipSource(_entryNames, _prefix = "") {
    return true;
  }

  /**
   * Get the names of the entries of a zip file, as nsIZipReader did:
   * directories are included even when the zip file only has entries for
   * files inside them.
   *
   * @param {ZipArchiveEntry[]} entries - The entries of the zip file.
   * @returns {string[]} Entry names. Directory names end with "/".
   */
  zipEntryNames(entries) {
    const names = new Set();
    for (const { name } of entries) {
      names.add(name);
      for (
        let slash = name.indexOf("/");
        slash != -1 && slash < name.length - 1;
        slash = name.indexOf("/", slash + 1)
      ) {
        names.add(name.slice(0, slash + 1));
      }
    }
    return [...names];
  }

  /**
   * @callback ProgressCallback
   * @param {number} progress - A value between 0 and 1.
   */
  /**
   * Extract a zip file to a temporary directory.
   *
   * @param {nsIFile} sourceFile
   * @param {ProgressCallback} progressCallback
   * @returns {nsIFile}
   * @throws {Error} If the archive could not be read, or any file could not
   *   be extracted. Nothing is left in the temporary directory.
   */
  async extractZipFile(sourceFile, progressCallback) {
    const entries = await ZipArchive.listEntries(sourceFile.path);

    // Extract the zip file to a tmp dir.
    const targetDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
    targetDir.append("tmp-profile");
    targetDir.createUnique(Ci.nsIFile.DIRECTORY_TYPE, 0o755);

    // The profile data could be at the top level, or inside a lone folder at
    // the top level. Find out which.
    let depth = 0;
    const entryNames = this.zipEntryNames(entries);
    const rootEntries = entryNames.filter(e => e.match(/^[^\/]*\/?$/));
    if (
      entryNames.length > 1 &&
      rootEntries.length == 1 &&
      rootEntries[0].endsWith("/") &&
      this.validateZipSource(entryNames, rootEntries[0])
    ) {
      this._logger.debug(`Found an inner directory ${rootEntries[0]}`);
      depth = 1;
    }

    const toExtract = [];
    for (const { name, directory } of entries) {
      const parts = name.split("/").slice(depth);
      if (
        parts.length == 0 ||
        this.IGNORE_DIRS.includes(parts[0]) ||
        directory
      ) {
        this._logger.debug(`Skipping ${name}`);
        continue;
      }
      // Drop the root folder name in the zip file. Never write outside the
      // target directory: append() rejects names containing a separator.
      const target = targetDir.clone();
      try {
        const unsafePart = parts.find(part => ["", ".", ".."].includes(part));
        if (unsafePart !== undefined) {
          throw new Error(`Unsafe path part "${unsafePart}"`);
        }
        for (const part of parts) {
          target.append(part);
        }
      } catch (e) {
        this._logger.warn(`Skipping ${name}: ${e}`);
        continue;
      }
      this._logger.debug(`Extracting ${name} to ${target.path}`);
      toExtract.push({ name, path: target.path });
    }

    try {
      const failures = await ZipArchive.extract(
        sourceFile.path,
        toExtract,
        (doneBytes, totalBytes) => {
          if (totalBytes) {
            progressCallback(doneBytes / totalBytes);
          }
        }
      );
      if (failures.length) {
        for (const { name, message } of failures) {
          this._logger.error(`Failed to extract ${name}: ${message}`);
        }
        throw new Error(
          `Failed to extract ${failures.length} of ${toExtract.length} files from ${sourceFile.leafName}`
        );
      }
    } catch (e) {
      // Importing what did extract would quietly leave mail behind.
      await IOUtils.remove(targetDir.path, { recursive: true });
      throw e;
    }
    progressCallback(1);
    return targetDir;
  }

  /**
   * Actually start importing things to the current profile.
   *
   * @param {nsIFile} _sourceProfileDir - The source location to import from.
   * @param {ImportItems} _items - The items to import.
   * @returns {boolean} Returns true when accounts have been imported, which
   *   means a restart is needed. Otherwise, no restart is needed.
   */
  async startImport(_sourceProfileDir, _items) {
    throw Components.Exception(
      `startImport not implemented in ${this.constructor.name}`,
      Cr.NS_ERROR_NOT_IMPLEMENTED
    );
  }

  /**
   * Reset use_without_mail_account, so that imported accounts are correctly
   * rendered in the folderPane.
   */
  _onImportAccounts() {
    Services.prefs.setBoolPref("app.use_without_mail_account", false);
  }

  /**
   * Increase _itemsImportedCount by one, and call onProgress.
   */
  async _updateProgress() {
    this.onProgress(++this._itemsImportedCount, this._itemsTotalCount);
    return new Promise(resolve => setTimeout(resolve));
  }

  _logger = console.createInstance({
    prefix: "mail.import",
    maxLogLevel: "Warn",
    maxLogLevelPref: "mail.import.loglevel",
  });
}
