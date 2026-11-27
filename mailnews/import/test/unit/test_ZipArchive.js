/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Tests for ZipArchive: archives it creates can be listed and extracted
 * again unchanged, progress is reported in bytes, and damaged or missing
 * entries are reported rather than extracted.
 */

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
var { ZipArchive } = ChromeUtils.importESModule(
  "resource:///modules/ZipArchive.sys.mjs"
);
var { writeTruncatedZip } = ChromeUtils.importESModule(
  "resource://testing-common/TruncatedZip.sys.mjs"
);

// Several of zip.js's chunks, so that data crosses chunk boundaries.
const LARGE_SIZE = 9 * 1024 * 1024 + 7;
// Zip timestamps are stored to the second, or to two seconds without the
// extended timestamp field.
const TIMESTAMP_PRECISION_MS = 2000;
const MODIFIED_TIME = Date.UTC(2020, 1, 29, 12, 34, 56);

// The parts of a zip local file header that findEntryData() reads.
const LOCAL_HEADER_SIGNATURE = 0x04034b50;
const LOCAL_HEADER_NAME_LENGTH = 26;
const LOCAL_HEADER_EXTRA_LENGTH = 28;
const LOCAL_HEADER_SIZE = 30;

let tmpDir;

function patterned(size) {
  const data = new Uint8Array(size);
  for (let i = 0; i < size; i++) {
    data[i] = (i * 31 + (i >> 11)) % 251;
  }
  return data;
}

const FILES = {
  "prefs.js": new TextEncoder().encode('user_pref("a", 1);\n'),
  "Mail/Local Folders/Inbox": patterned(LARGE_SIZE),
  "Mail/Local Folders/Empty": new Uint8Array(),
  "Mail/Local Folders/Ünïcödé name": new TextEncoder().encode("unicode"),
};

add_setup(async function () {
  tmpDir = await IOUtils.createUniqueDirectory(PathUtils.tempDir, "ziparchive");
  registerCleanupFunction(() => IOUtils.remove(tmpDir, { recursive: true }));
});

/**
 * Write FILES under a new directory.
 *
 * @returns {Promise<{name: string, path: string}[]>}
 */
async function writeSourceFiles() {
  const sourceDir = await IOUtils.createUniqueDirectory(tmpDir, "source");
  const entries = [];
  for (const [name, data] of Object.entries(FILES)) {
    const path = PathUtils.join(sourceDir, ...name.split("/"));
    await IOUtils.makeDirectory(PathUtils.parent(path));
    await IOUtils.write(path, data);
    await IOUtils.setModificationTime(path, MODIFIED_TIME);
    entries.push({ name, path });
  }
  return entries;
}

/**
 * Find where the data of an entry starts, from its local file header.
 *
 * @param {Uint8Array} archive
 * @param {string} name
 * @returns {number} The offset of the data, or -1 if there is no such entry.
 */
function findEntryData(archive, name) {
  const view = new DataView(archive.buffer);
  const encodedName = new TextEncoder().encode(name);
  for (let i = 0; i + LOCAL_HEADER_SIZE <= archive.length; i++) {
    if (view.getUint32(i, true) != LOCAL_HEADER_SIGNATURE) {
      continue;
    }
    const nameLength = view.getUint16(i + LOCAL_HEADER_NAME_LENGTH, true);
    const extraLength = view.getUint16(i + LOCAL_HEADER_EXTRA_LENGTH, true);
    const nameStart = i + LOCAL_HEADER_SIZE;
    if (
      nameLength == encodedName.length &&
      encodedName.every((byte, j) => archive[nameStart + j] == byte)
    ) {
      return nameStart + nameLength + extraLength;
    }
  }
  return -1;
}

async function createArchive() {
  const archivePath = await IOUtils.createUniqueFile(tmpDir, "test.zip");
  const progress = [];
  await ZipArchive.create(archivePath, await writeSourceFiles(), (...p) =>
    progress.push(p)
  );
  return { archivePath, progress };
}

add_task(async function testRoundTrip() {
  const { archivePath, progress } = await createArchive();

  const totalBytes = Object.values(FILES).reduce((s, d) => s + d.length, 0);
  Assert.deepEqual(
    progress.at(-1),
    [totalBytes, totalBytes],
    "Creating should report progress in bytes, ending complete"
  );
  Assert.less(
    (await IOUtils.stat(archivePath)).size,
    totalBytes,
    "Files should be compressed"
  );

  const entries = await ZipArchive.listEntries(archivePath);
  Assert.deepEqual(
    entries.map(e => [e.name, e.directory, e.size]).sort(),
    Object.entries(FILES)
      .map(([name, data]) => [name, false, data.length])
      .sort(),
    "Archive should list each file, with its size"
  );

  const targetDir = await IOUtils.createUniqueDirectory(tmpDir, "target");
  const extractProgress = [];
  const failures = await ZipArchive.extract(
    archivePath,
    Object.keys(FILES).map(name => ({
      name,
      path: PathUtils.join(targetDir, ...name.split("/")),
    })),
    (...p) => extractProgress.push(p)
  );
  Assert.deepEqual(failures, [], "Every entry should have been extracted");
  Assert.deepEqual(
    extractProgress.at(-1),
    [totalBytes, totalBytes],
    "Extracting should report progress in bytes, ending complete"
  );
  for (let i = 1; i < extractProgress.length; i++) {
    Assert.lessOrEqual(
      extractProgress[i - 1][0],
      extractProgress[i][0],
      "Progress should never go backwards"
    );
  }

  for (const [name, data] of Object.entries(FILES)) {
    const path = PathUtils.join(targetDir, ...name.split("/"));
    const extracted = await IOUtils.read(path);
    Assert.equal(extracted.length, data.length, `${name} has the right size`);
    Assert.ok(
      extracted.every((byte, i) => byte == data[i]),
      `${name} should have the right content`
    );
    const { lastModified } = await IOUtils.stat(path);
    Assert.lessOrEqual(
      Math.abs(lastModified - MODIFIED_TIME),
      TIMESTAMP_PRECISION_MS,
      `${name} should keep its modification time`
    );
  }
});

add_task(async function testCreateReplacesExistingFile() {
  const archivePath = PathUtils.join(tmpDir, "existing.zip");
  await IOUtils.write(archivePath, patterned(LARGE_SIZE));
  const sources = await writeSourceFiles();
  await ZipArchive.create(archivePath, sources.slice(0, 1));
  const entries = await ZipArchive.listEntries(archivePath);
  Assert.deepEqual(
    entries.map(e => e.name),
    [sources[0].name],
    "Old file should have been replaced, not added to"
  );
});

add_task(async function testNotAnArchive() {
  const path = PathUtils.join(tmpDir, "not-a-zip.zip");
  await IOUtils.writeUTF8(path, "This is not a zip file.");
  await Assert.rejects(
    ZipArchive.listEntries(path),
    /./,
    "Listing a file that is not an archive should fail"
  );
  await Assert.rejects(
    ZipArchive.listEntries(PathUtils.join(tmpDir, "missing.zip")),
    /./,
    "Listing a missing file should fail"
  );
});

add_task(async function testDamagedAndMissingEntries() {
  const { archivePath } = await createArchive();

  // Damage the first byte of the prefs.js data, found from its local header.
  const archive = await IOUtils.read(archivePath);
  const offset = findEntryData(archive, "prefs.js");
  Assert.greater(offset, 0, "Should find the prefs.js data");
  archive[offset] ^= 0xff;
  await IOUtils.write(archivePath, archive);

  const targetDir = await IOUtils.createUniqueDirectory(tmpDir, "damaged");
  const failures = await ZipArchive.extract(archivePath, [
    { name: "prefs.js", path: PathUtils.join(targetDir, "prefs.js") },
    { name: "not/there", path: PathUtils.join(targetDir, "there") },
    {
      name: "Mail/Local Folders/Inbox",
      path: PathUtils.join(targetDir, "Inbox"),
    },
  ]);
  Assert.deepEqual(
    failures.map(f => f.name).sort(),
    ["not/there", "prefs.js"],
    "Damaged and missing entries should be reported"
  );
  Assert.equal(
    (await IOUtils.stat(PathUtils.join(targetDir, "Inbox"))).size,
    LARGE_SIZE,
    "Undamaged entry should still be extracted"
  );
});

add_task(async function testUnreadableSourceFailsCreate() {
  if (AppConstants.platform == "win") {
    // Permissions do not make a file unreadable here.
    return;
  }
  const unreadable = PathUtils.join(tmpDir, "unreadable");
  await IOUtils.writeUTF8(unreadable, "secret");
  await IOUtils.setPermissions(unreadable, 0o000);
  registerCleanupFunction(() => IOUtils.remove(unreadable));

  const archivePath = PathUtils.join(tmpDir, "incomplete.zip");
  await Assert.rejects(
    ZipArchive.create(archivePath, [{ name: "unreadable", path: unreadable }]),
    /unreadable/,
    "Error should name the entry that could not be added"
  );
});

add_task(async function testRemovedSourceIsLeftOut() {
  const [first, ...others] = await writeSourceFiles();
  const archivePath = PathUtils.join(tmpDir, "removed.zip");
  await IOUtils.remove(first.path);
  await ZipArchive.create(archivePath, [
    first,
    { name: "never-there", path: PathUtils.join(tmpDir, "never-there") },
    ...others,
  ]);
  const names = (await ZipArchive.listEntries(archivePath)).map(e => e.name);
  Assert.deepEqual(
    names,
    others.map(e => e.name),
    "Files removed before their turn should be left out, and the rest added"
  );
});

add_task(async function testAbortCreate() {
  const sources = await writeSourceFiles();
  const archivePath = PathUtils.join(tmpDir, "aborted.zip");

  const controller = new AbortController();
  const creating = ZipArchive.create(archivePath, sources, null, {
    signal: controller.signal,
  });
  controller.abort();
  await Assert.rejects(
    creating,
    e => e.name == "AbortError",
    "Aborting while the archive is being created should stop it"
  );

  await Assert.rejects(
    ZipArchive.create(archivePath, sources, null, {
      signal: controller.signal,
    }),
    e => e.name == "AbortError",
    "Already aborted signal should stop the archive before it starts"
  );
});

add_task(async function testTruncatedArchive() {
  const archivePath = PathUtils.join(tmpDir, "truncated.zip");
  await writeTruncatedZip(archivePath, "prefs.js");
  registerCleanupFunction(() => IOUtils.remove(archivePath));

  await Assert.rejects(
    ZipArchive.listEntries(archivePath),
    e => e.name == ZipArchive.TRUNCATED_ERROR,
    "Listing an archive with truncated offsets should fail"
  );
  await Assert.rejects(
    ZipArchive.extract(archivePath, [
      { name: "prefs.js", path: PathUtils.join(tmpDir, "prefs.js") },
    ]),
    e => e.name == ZipArchive.TRUNCATED_ERROR,
    "Extracting from an archive with truncated offsets should fail"
  );
  Assert.ok(
    !(await IOUtils.exists(PathUtils.join(tmpDir, "prefs.js"))),
    "Nothing should have been extracted"
  );
});
