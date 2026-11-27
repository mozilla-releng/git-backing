/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { BaseProfileImporter } = ChromeUtils.importESModule(
  "resource:///modules/BaseProfileImporter.sys.mjs"
);
var { ZipArchive } = ChromeUtils.importESModule(
  "resource:///modules/ZipArchive.sys.mjs"
);

function createZipProfile(entries) {
  const tmpZipFile = Services.dirsvc.get("TmpD", Ci.nsIFile);
  tmpZipFile.append("profile.zip");
  tmpZipFile.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o644);
  tmpZipFile.remove(false);
  info(`Created a temporary zip file at ${tmpZipFile.path}`);

  const zipWriter = Cc["@mozilla.org/zipwriter;1"].createInstance(
    Ci.nsIZipWriter
  );
  // MODE_WRONLY (0x02) and MODE_CREATE (0x08)
  zipWriter.open(tmpZipFile, 0x02 | 0x08);
  for (const entry of entries) {
    const stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
      Ci.nsIStringInputStream
    );
    stream.setByteStringData("this content doesn't matter");
    zipWriter.addEntryStream(
      entry,
      Date.now() * 1000,
      Ci.nsIZipWriter.COMPRESSION_NONE,
      stream,
      false
    );
  }
  zipWriter.close();

  return tmpZipFile;
}

/**
 * Count the temporary directories extractZipFile has made and left behind.
 */
async function countTmpProfiles() {
  return (await IOUtils.getChildren(PathUtils.tempDir)).filter(path =>
    PathUtils.filename(path).startsWith("tmp-profile")
  ).length;
}

/**
 * Tests extracting from the root of a zip file.
 */
add_task(async function testExtractFromRoot() {
  const zipFile = createZipProfile(["foo.txt", "bar/nope.png", "baz/quux.js"]);
  const importer = new (class extends BaseProfileImporter {
    IGNORE_DIRS = ["bar"];
  })();
  const extractDir = await importer.extractZipFile(zipFile, () => {});
  Assert.ok(extractDir.exists(), `directory ${extractDir.path} should exist`);

  const foo = extractDir.clone();
  foo.append("foo.txt");
  Assert.ok(foo.isFile(), `file ${foo.path} should exist`);

  const bar = extractDir.clone();
  bar.append("bar");
  Assert.ok(
    !bar.exists(),
    `directory ${bar.path} should not exist, because we're ignoring it`
  );

  const quux = extractDir.clone();
  quux.append("baz");
  Assert.ok(quux.isDirectory(), `directory ${quux.path} should exist`);
  quux.append("quux.js");
  Assert.ok(quux.isFile(), `file ${quux.path} should exist`);
});

/**
 * Tests extracting from a zip file with a top-level directory. We're using
 * BaseProfileImporter, which says all zip files are valid, so the contents of
 * the directory will be extracted, but not the directory itself.
 */
add_task(async function testExtractFromLevel1() {
  const zipFile = createZipProfile([
    "inner/foo.txt",
    "inner/bar/nope.png",
    "inner/baz/quux.js",
  ]);
  const importer = new (class extends BaseProfileImporter {
    IGNORE_DIRS = ["bar"];
  })();
  const extractDir = await importer.extractZipFile(zipFile, () => {});
  Assert.ok(extractDir.exists(), `directory ${extractDir.path} should exist`);

  const foo = extractDir.clone();
  foo.append("foo.txt");
  Assert.ok(foo.isFile(), `file ${foo.path} should exist`);

  const bar = extractDir.clone();
  bar.append("bar");
  Assert.ok(
    !bar.exists(),
    `directory ${bar.path} should not exist, because we're ignoring it`
  );

  const quux = extractDir.clone();
  quux.append("baz");
  Assert.ok(quux.isDirectory(), `directory ${quux.path} should exist`);
  quux.append("quux.js");
  Assert.ok(quux.isFile(), `file ${quux.path} should exist`);
});

/**
 * Tests extracting from a zip file with a top-level directory. We'll say that
 * the directory does NOT contain a valid profile, so the directory AND its
 * contents will be extracted.
 */
add_task(async function testExtractFromLevel0() {
  const zipFile = createZipProfile([
    "inner/foo.txt",
    "inner/bar/nope.png",
    "inner/baz/quux.js",
  ]);
  const importer = new (class extends BaseProfileImporter {
    IGNORE_DIRS = ["bar"];
    validateZipSource(_entryNames, _prefix = "") {
      return false;
    }
  })();
  const extractDir = await importer.extractZipFile(zipFile, () => {});
  Assert.ok(extractDir.exists(), `directory ${extractDir.path} should exist`);

  const inner = extractDir.clone();
  inner.append("inner");
  Assert.ok(inner.isDirectory(), `directory ${inner.path} should exist`);

  const foo = inner.clone();
  foo.append("foo.txt");
  Assert.ok(foo.isFile(), `file ${foo.path} should exist`);

  const bar = inner.clone();
  bar.append("bar");
  // Only top-level directories can be ignored.
  Assert.ok(
    bar.isDirectory(),
    `directory ${bar.path} should exist, because we're not ignoring it`
  );
  bar.append("nope.png");
  Assert.ok(bar.isFile(), `file ${bar.path} should exist`);

  const quux = inner.clone();
  quux.append("baz");
  Assert.ok(quux.isDirectory(), `directory ${quux.path} should exist`);
  quux.append("quux.js");
  Assert.ok(quux.isFile(), `file ${quux.path} should exist`);
});

/**
 * Test progress updates.
 */
add_task(async function testExtractManyFiles() {
  const files = [];
  for (let i = 0x61; i <= 0x7a; i++) {
    files.push(`${String.fromCharCode(i).repeat(3)}.txt`);
  }
  const zipFile = createZipProfile(files);
  const importer = new BaseProfileImporter();
  const progressReports = [];
  const extractDir = await importer.extractZipFile(
    zipFile,
    function (progress) {
      Assert.greater(progress, 0, "progress should be more than 0");
      Assert.lessOrEqual(progress, 1, "progress should be less or equal to 1");
      progressReports.push(progress);
    }
  );
  Assert.ok(extractDir.exists(), `directory ${extractDir.path} should exist`);
  Assert.greater(
    progressReports.length,
    1,
    "progress should have been reported during extraction"
  );
  Assert.equal(
    progressReports.at(-1),
    1,
    "progress should have been reported at the end of extraction"
  );
});

/**
 * Tests that a zip file with names reaching outside the target directory is
 * refused, and nothing is written outside the target directory.
 */
add_task(async function testExtractUnsafeNames() {
  for (const unsafeName of [
    "../escaped.txt",
    "baz/../../escaped.txt",
    "/escaped.txt",
  ]) {
    const zipFile = createZipProfile(["good.txt", unsafeName]);
    const importer = new BaseProfileImporter();
    await Assert.rejects(
      importer.extractZipFile(zipFile, () => {}),
      /Unsafe filename/,
      `Zip file containing ${unsafeName} should be refused`
    );
    const escaped = zipFile.parent.clone();
    escaped.append("escaped.txt");
    Assert.ok(!escaped.exists(), `file ${escaped.path} should not exist`);
    Assert.ok(
      !(await IOUtils.exists("/escaped.txt")),
      "Nothing should have been written at the root of the file system"
    );
  }
});

/**
 * Tests that a file which cannot be extracted stops the import, rather than
 * importing the rest without it.
 */
add_task(async function testExtractFailureStopsImport() {
  const zipFile = createZipProfile(["foo.txt", "bar.txt"]);
  // Damage the stored content of the first file, so its CRC-32 is wrong.
  const archive = await IOUtils.read(zipFile.path);
  const content = new TextEncoder().encode("this content doesn't matter");
  const offset = archive.findIndex((_, i) =>
    content.every((byte, j) => archive[i + j] == byte)
  );
  Assert.greater(offset, 0, "Should find the stored content");
  archive[offset] ^= 0xff;
  await IOUtils.write(zipFile.path, archive);

  const before = await countTmpProfiles();
  const importer = new BaseProfileImporter();
  await Assert.rejects(
    importer.extractZipFile(zipFile, () => {}),
    /Failed to extract 1 of 2 files/,
    "Import should stop"
  );
  Assert.equal(
    await countTmpProfiles(),
    before,
    "Should not leave a partly extracted profile behind"
  );
});

/**
 * Tests that the temporary directory is removed if extracting throws, not
 * only when individual files fail.
 */
add_task(async function testExtractThrowCleansUp() {
  const zipFile = createZipProfile(["foo.txt", "bar.txt"]);
  const originalExtract = ZipArchive.extract;
  ZipArchive.extract = async () => {
    throw new Error("Simulated extraction error");
  };
  try {
    const before = await countTmpProfiles();
    const importer = new BaseProfileImporter();
    await Assert.rejects(
      importer.extractZipFile(zipFile, () => {}),
      /Simulated extraction error/,
      "Error from ZipArchive.extract should be passed on"
    );
    Assert.equal(
      await countTmpProfiles(),
      before,
      "Should not leave a temporary directory behind"
    );
  } finally {
    ZipArchive.extract = originalExtract;
  }
});
