/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Tests for ProfileExporter: that it produces a readable archive, reports
 * progress in bytes, keeps the main thread free while a large file is being
 * added, and fails rather than producing an incomplete backup, also when
 * Thunderbird quits part way through.
 */

var { ProfileExporter } = ChromeUtils.importESModule(
  "resource:///modules/ProfileExporter.sys.mjs"
);
var { ZipArchive } = ChromeUtils.importESModule(
  "resource:///modules/ZipArchive.sys.mjs"
);
var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
var { AsyncShutdown } = ChromeUtils.importESModule(
  "resource://gre/modules/AsyncShutdown.sys.mjs"
);

// Big enough that a synchronous copy would hold the main thread for a
// noticeable time, small enough to keep the test quick.
const LARGE_SIZE = 128 * 1024 * 1024;

// The most bytes crypto.getRandomValues() fills in one call.
const MAX_RANDOM_BYTES = 65536;

const profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
const profileName = profileDir.leafName;

function profilePath(...parts) {
  return PathUtils.join(profileDir.path, ...parts);
}

function tmpZip(name) {
  const file = Services.dirsvc.get("TmpD", Ci.nsIFile);
  file.append(name);
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o644);
  file.remove(false);
  registerCleanupFunction(() => {
    if (file.exists()) {
      file.remove(false);
    }
  });
  return file;
}

add_setup(async function () {
  await IOUtils.makeDirectory(profilePath("Mail", "Local Folders"));
  await IOUtils.writeUTF8(profilePath("Mail", "Local Folders", "Inbox"), "a");
  if (!(await IOUtils.exists(profilePath("prefs.js")))) {
    await IOUtils.writeUTF8(profilePath("prefs.js"), "");
  }
  // Written in slices so the test does not need the whole file in memory.
  // Random data, repeated further apart than deflate looks back, compresses
  // as slowly as real mail with attachments; a run of one byte would be
  // compressed before any progress is reported.
  const slice = new Uint8Array(16 * 1024 * 1024);
  for (let i = 0; i < slice.length; i += MAX_RANDOM_BYTES) {
    crypto.getRandomValues(slice.subarray(i, i + MAX_RANDOM_BYTES));
  }
  const big = profilePath("Mail", "Local Folders", "Archives");
  for (let written = 0; written < LARGE_SIZE; written += slice.length) {
    await IOUtils.write(big, slice, { mode: "appendOrCreate" });
  }
  registerCleanupFunction(() =>
    IOUtils.remove(profilePath("Mail"), { recursive: true })
  );
});

add_task(async function testExportRoundTrip() {
  // Save into the profile itself, which a user may do. The archive must not
  // try to include itself.
  const zip = await IOUtils.getFile(profilePath("export.zip"));
  registerCleanupFunction(() =>
    IOUtils.remove(zip.path, { ignoreAbsent: true })
  );
  // Left behind by an export that never finished, as when Thunderbird quits
  // while exporting.
  await IOUtils.writeUTF8(profilePath("export.zip.part"), "partial");
  // Left behind by an earlier export to another name.
  await IOUtils.writeUTF8(profilePath("older.zip.part"), "partial");
  registerCleanupFunction(() => IOUtils.remove(profilePath("older.zip.part")));
  const exporter = new ProfileExporter();
  const progress = [];
  exporter.onProgress = (current, total) => progress.push([current, total]);

  // Look at the progress on every turn of the main thread while the export
  // runs. The other files are tiny, so progress part way through the large
  // file can only be seen if the main thread ran while that file was being
  // compressed. A synchronous export would allow no turns until it was over.
  let sawLargeFileInProgress = false;
  let exporting = true;
  (async () => {
    while (exporting) {
      await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));
      const [current, total] = progress.at(-1) ?? [0, 1];
      if (current > total * 0.1 && current < total * 0.9) {
        sawLargeFileInProgress = true;
      }
    }
  })();
  await exporter.startExport(zip);
  exporting = false;

  Assert.ok(
    sawLargeFileInProgress,
    "Main thread should keep running while the large file is being added"
  );

  const total = progress.at(-1)[1];
  Assert.greaterOrEqual(
    total,
    LARGE_SIZE,
    "Progress should be counted in bytes"
  );
  Assert.deepEqual(
    progress.at(-1),
    [total, total],
    "Progress should end complete"
  );
  for (let i = 1; i < progress.length; i++) {
    Assert.lessOrEqual(
      progress[i - 1][0],
      progress[i][0],
      "Progress should never go backwards"
    );
  }

  const names = (await ZipArchive.listEntries(zip.path)).map(e => e.name);
  Assert.ok(
    !names.includes(`${profileName}/export.zip`),
    "Archive should not contain itself"
  );
  Assert.ok(
    !names.includes(`${profileName}/export.zip.part`),
    "Archive should not contain a partial archive"
  );
  Assert.ok(
    !names.includes(`${profileName}/older.zip.part`),
    "Archive should not contain a partial archive of an earlier export"
  );
  Assert.ok(
    !(await IOUtils.exists(profilePath("export.zip.part"))),
    "Partial archive should have been renamed to the backup"
  );
  for (const name of ["Mail/Local Folders/Inbox", "prefs.js"]) {
    Assert.ok(
      names.includes(`${profileName}/${name}`),
      `Archive should have ${name}`
    );
  }
  const out = tmpZip("Archives.out");
  const failures = await ZipArchive.extract(zip.path, [
    { name: `${profileName}/Mail/Local Folders/Archives`, path: out.path },
  ]);
  Assert.deepEqual(failures, [], "Large file should have been extracted");
  Assert.equal(out.fileSize, LARGE_SIZE, "Large file should come back whole");
});

add_task(async function testCancelExport() {
  const zip = tmpZip("export-cancelled.zip");
  // The user chose to replace an older backup.
  await IOUtils.writeUTF8(zip.path, "older backup");
  const exporter = new ProfileExporter();
  const controller = new AbortController();
  // Cancel once the large file is under way.
  exporter.onProgress = () => controller.abort();
  await Assert.rejects(
    exporter.startExport(zip, { signal: controller.signal }),
    e => e.name == "AbortError",
    "Cancelled export should reject with an AbortError"
  );
  Assert.ok(
    !(await IOUtils.exists(`${zip.path}.part`)),
    "Partial backup should have been removed"
  );
  Assert.equal(
    await IOUtils.readUTF8(zip.path),
    "older backup",
    "Older backup should be left as it was"
  );
});

add_task(async function testUnreadableFileFailsExport() {
  if (AppConstants.platform == "win") {
    // Permissions do not make a file unreadable here.
    return;
  }
  const unreadable = profilePath("Mail", "Local Folders", "Unreadable");
  await IOUtils.writeUTF8(unreadable, "secret");
  await IOUtils.setPermissions(unreadable, 0o000);
  registerCleanupFunction(() => IOUtils.remove(unreadable));

  const zip = tmpZip("export-incomplete.zip");
  const exporter = new ProfileExporter();
  await Assert.rejects(
    exporter.startExport(zip),
    /Profile export failed: Failed to add .*Unreadable/,
    "Export should report the file it could not add"
  );
  Assert.ok(!zip.exists(), "Should not have left incomplete backup behind");
  Assert.ok(
    !(await IOUtils.exists(`${zip.path}.part`)),
    "Should not have left partial backup behind"
  );
});

// Last, because the shutdown phase it triggers stays triggered for anything
// else that uses it.
add_task(async function testQuitCancelsExport() {
  Services.prefs.setBoolPref("toolkit.asyncshutdown.testing", true);
  const zip = tmpZip("export-quit.zip");
  const exporter = new ProfileExporter();
  // Quit once the large file is well under way.
  const { promise: underWay, resolve: onUnderWay } = Promise.withResolvers();
  exporter.onProgress = (current, total) => {
    if (current > total * 0.1) {
      onUnderWay();
    }
  };
  const exported = exporter.startExport(zip);
  await underWay;
  Assert.ok(
    await IOUtils.exists(`${zip.path}.part`),
    "Partial backup should be written while exporting"
  );

  // Returns once the blockers have finished, as quitting would go on then.
  AsyncShutdown.appShutdownConfirmed._trigger();
  Assert.ok(
    !(await IOUtils.exists(`${zip.path}.part`)),
    "Partial backup should have been removed before quitting went on"
  );
  await Assert.rejects(
    exported,
    e => e.name == "AbortError",
    "Quitting should cancel the export"
  );
  Assert.ok(!zip.exists(), "Should not have left a backup behind");
});
