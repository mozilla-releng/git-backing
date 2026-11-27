/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ZipArchive } from "resource:///modules/ZipArchive.sys.mjs";

const END_OF_CENTRAL_DIRECTORY_SIGNATURE = 0x06054b50;
const END_OF_CENTRAL_DIRECTORY_LENGTH = 22;
const CENTRAL_DIRECTORY_OFFSET_FIELD = 16;
const FOUR_GB = 2 ** 32;

/**
 * Write an archive shaped like those from writers without ZIP64 support, as
 * older versions of Thunderbird were: larger than 4 GB, with the offset of
 * its central directory recorded modulo 4 GB.
 *
 * The first entry is real and sits at the start of the file. A sparse 4 GB
 * gap follows it, so the file costs little disk, then the central directory
 * and end record, unchanged.
 *
 * @param {string} archivePath - Absolute path of the archive to write.
 * @param {string} name - The name of the one entry.
 */
export async function writeTruncatedZip(archivePath, name) {
  const sourcePath = archivePath + ".source";
  const smallPath = archivePath + ".small";
  await IOUtils.writeUTF8(sourcePath, "This content does not matter.\n");
  await ZipArchive.create(smallPath, [{ name, path: sourcePath }]);
  const archive = await IOUtils.read(smallPath);
  await IOUtils.remove(sourcePath);
  await IOUtils.remove(smallPath);

  const view = new DataView(archive.buffer);
  const endOffset = archive.length - END_OF_CENTRAL_DIRECTORY_LENGTH;
  if (view.getUint32(endOffset, true) != END_OF_CENTRAL_DIRECTORY_SIGNATURE) {
    throw new Error("Expected an end record without a comment");
  }
  const directoryOffset = view.getUint32(
    endOffset + CENTRAL_DIRECTORY_OFFSET_FIELD,
    true
  );

  const file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  file.initWithPath(archivePath);
  const stream = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
    Ci.nsIFileOutputStream
  );
  const PR_WRONLY = 0x2;
  const PR_CREATE_FILE = 0x08;
  const PR_TRUNCATE = 0x20;
  stream.init(file, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0o644, 0);
  const binary = Cc["@mozilla.org/binaryoutputstream;1"].createInstance(
    Ci.nsIBinaryOutputStream
  );
  binary.setOutputStream(stream);
  binary.writeByteArray(archive.subarray(0, directoryOffset));
  stream
    .QueryInterface(Ci.nsISeekableStream)
    .seek(Ci.nsISeekableStream.NS_SEEK_SET, directoryOffset + FOUR_GB);
  binary.writeByteArray(archive.subarray(directoryOffset));
  binary.close();
}
