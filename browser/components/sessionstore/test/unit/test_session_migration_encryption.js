/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

do_get_profile();

const { SessionFile } = ChromeUtils.importESModule(
  "resource:///modules/sessionstore/SessionFile.sys.mjs"
);

const { FirefoxProfileMigrator } = ChromeUtils.importESModule(
  "resource:///modules/FirefoxProfileMigrator.sys.mjs"
);

// MigrationUtils is a browser-window global (from browser.js), but xpcshell
// tests don't have a browser window so the explicit import is necessary.
// eslint-disable-next-line mozilla/no-redeclare-with-import-autofix
const { MigrationUtils } = ChromeUtils.importESModule(
  "resource:///modules/MigrationUtils.sys.mjs"
);

const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);
updateAppInfo({
  name: "SessionRestoreTest",
  ID: "{230de50e-4cd1-11dc-8314-0800200c9a66}",
  version: "1",
  platformVersion: "",
});

const ENCRYPTION_PREF = "browser.sessionstore.encryption.enabled";

function nsIFileFromPath(path) {
  let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  file.initWithPath(path);
  return file;
}

// Set up a source profile directory with a plaintext session file,
// sessionCheckpoints.json, and optionally a prefs.js.
async function makeSourceProfile(sourceDir, prefsContent) {
  await IOUtils.makeDirectory(sourceDir);
  let state = {
    windows: [{ tabs: [{ entries: [{ url: "https://example.com" }] }] }],
  };
  await IOUtils.writeUTF8(
    PathUtils.join(sourceDir, "sessionstore.jsonlz4"),
    JSON.stringify(state),
    { compress: true }
  );
  await IOUtils.writeJSON(
    PathUtils.join(sourceDir, "sessionCheckpoints.json"),
    { profile: true }
  );
  if (prefsContent) {
    await IOUtils.writeUTF8(
      PathUtils.join(sourceDir, "prefs.js"),
      prefsContent
    );
  }
}

async function runSessionMigration(sourceDir, destDir) {
  await IOUtils.makeDirectory(destDir);
  Services.env.set("MOZ_RESET_PROFILE_SESSION", "1");
  let migrator = new FirefoxProfileMigrator();
  let resources = migrator.getResourcesInternal(
    nsIFileFromPath(sourceDir),
    nsIFileFromPath(destDir)
  );
  let sessionResource = resources.find(
    r => r.type == MigrationUtils.resourceTypes.SESSION
  );
  Assert.ok(sessionResource, "SESSION resource was created");
  let succeeded = await new Promise(resolve => {
    sessionResource.migrate(resolve);
  });
  Assert.ok(succeeded, "migration callback reported success");
}

add_setup(async function () {
  Services.prefs.getDefaultBranch("").setBoolPref(ENCRYPTION_PREF, true);
  Services.prefs.lockPref(ENCRYPTION_PREF);
  registerCleanupFunction(() => {
    Services.prefs.unlockPref(ENCRYPTION_PREF);
    Services.prefs.getDefaultBranch("").deleteBranch(ENCRYPTION_PREF);
  });

  await SessionFile.read();
});

// Lockstore key files from the source profile should be copied to the
// destination so SessionMigration can decrypt the session data.
add_task(async function test_copies_lockstore_keys() {
  let sourceDir = PathUtils.join(PathUtils.tempDir, "copy-keys-source");
  let destDir = PathUtils.join(PathUtils.tempDir, "copy-keys-dest");

  await makeSourceProfile(sourceDir);

  // Place lockstore key files in the source.
  await IOUtils.writeUTF8(
    PathUtils.join(sourceDir, "lockstore.keys.sqlite"),
    "fake-key-data"
  );
  await IOUtils.writeUTF8(
    PathUtils.join(sourceDir, "lockstore.keys.sqlite-wal"),
    "fake-wal-data"
  );

  await runSessionMigration(sourceDir, destDir);

  Assert.ok(
    await IOUtils.exists(PathUtils.join(destDir, "lockstore.keys.sqlite")),
    "lockstore.keys.sqlite was copied"
  );
  Assert.ok(
    await IOUtils.exists(PathUtils.join(destDir, "lockstore.keys.sqlite-wal")),
    "lockstore.keys.sqlite-wal was copied"
  );

  await IOUtils.remove(sourceDir, { recursive: true });
  await IOUtils.remove(destDir, { recursive: true });
});

// The encryption pref should be migrated from the source profile's prefs.js.
add_task(async function test_migrates_encryption_pref_true() {
  let sourceDir = PathUtils.join(PathUtils.tempDir, "pref-true-source");
  let destDir = PathUtils.join(PathUtils.tempDir, "pref-true-dest");

  await makeSourceProfile(
    sourceDir,
    'user_pref("browser.sessionstore.encryption.enabled", true);\n'
  );
  await runSessionMigration(sourceDir, destDir);

  Assert.equal(
    Services.prefs.getBoolPref(ENCRYPTION_PREF),
    true,
    "encryption pref migrated as true"
  );

  await IOUtils.remove(sourceDir, { recursive: true });
  await IOUtils.remove(destDir, { recursive: true });
});

add_task(async function test_migrates_encryption_pref_false() {
  Services.prefs.unlockPref(ENCRYPTION_PREF);
  Services.prefs.getDefaultBranch("").setBoolPref(ENCRYPTION_PREF, false);

  let sourceDir = PathUtils.join(PathUtils.tempDir, "pref-false-source");
  let destDir = PathUtils.join(PathUtils.tempDir, "pref-false-dest");

  await makeSourceProfile(
    sourceDir,
    'user_pref("browser.sessionstore.encryption.enabled", false);\n'
  );
  await runSessionMigration(sourceDir, destDir);

  Assert.equal(
    Services.prefs.getBoolPref(ENCRYPTION_PREF),
    false,
    "encryption pref migrated as false"
  );

  // Restore locked state for subsequent tests.
  Services.prefs.getDefaultBranch("").setBoolPref(ENCRYPTION_PREF, true);
  Services.prefs.lockPref(ENCRYPTION_PREF);

  await IOUtils.remove(sourceDir, { recursive: true });
  await IOUtils.remove(destDir, { recursive: true });
});
