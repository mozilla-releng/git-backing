/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_decoders_EXIFScanner_h
#define mozilla_image_decoders_EXIFScanner_h

#include <stdint.h>

#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/image/EXIF.h"

namespace mozilla::image {

// The container format a file's leading bytes identify it as. The names of
// these reach telemetry, so treat them as a data contract.
enum class EXIFContainer : uint8_t {
  Unknown,
  JPEG,
  PNG,
  TIFF,
  // II 2B 00 / MM 00 2B. A different header layout to classic TIFF, so it must
  // be recognised separately rather than mis-parsed as one.
  BigTIFF,
  // Recognised, but their EXIF is not located. See the file comment.
  WebP,
  HEIF,
  AVIF,
  GIF,
  BMP,
  JXL,
  SVG,
};

// Why a scan produced the GPS answer it did.
enum class EXIFScanOutcome : uint8_t {
  // The EXIF block was found and walked in full.
  Complete,
  // The container is well formed and carries no EXIF block at all.
  NoEXIF,
  // A format whose EXIF we do not locate, or none we recognise.
  UnsupportedContainer,
  // Segment, chunk or block lengths are inconsistent.
  MalformedContainer,
  // EXIF is present but in an encoding we cannot read. Distinguished from
  // NoEXIF so that it does not read as a confident "no GPS".
  UnsupportedEXIFEncoding,
  // We know where the EXIF block is but did not have all of it.
  EXIFTruncated,
  // We ran out of bytes before locating anything.
  EXIFLocationUnknown,
};

struct EXIFScanResult {
  EXIFContainer container = EXIFContainer::Unknown;
  EXIFScanOutcome outcome = EXIFScanOutcome::UnsupportedContainer;
  EXIFGPSPresence gpsPresence = EXIFGPSPresence::None;
  // The decoded position, present exactly when gpsPresence is Position.
  Maybe<EXIFGPSCoordinates> gpsCoordinates;
};

// Finds the EXIF block in an image file and reports whether it says where the
// photo was taken.
//
// aBytes must start at file offset 0 and may be a prefix of the file;
// aFileSize is the full size, which is how the scan tells "the file ends here"
// apart from "our buffer ends here".
//
// Reading a prefix is deliberate: this exists to be run over files a user has
// just handed to a web page, and pulling whole images into memory to answer one
// question about their header would be a poor trade. The consequence is an
// asymmetry that callers must respect:
//
//   A POSITIVE result is always authoritative. A coordinate that has been
//   validated cannot be un-found by supplying more bytes, so Position is
//   reportable even when the outcome is EXIFTruncated.
//
//   A NEGATIVE result is only authoritative for Complete and NoEXIF. Under any
//   other outcome the absence of GPS means "we could not tell", not "there is
//   none", and must not be reported as a confident no.
//
// Only JPEG, PNG and TIFF are located. WebP stores its metadata chunks after
// the image data, and HEIF and AVIF reach theirs through an offset table into
// mdat; both would need a second, seeking read to resolve, and together they
// are a low single-digit percentage of the images users upload. They are still
// identified, so their share stays visible in the data.
//
// This is pure computation over bytes: no I/O, no allocation of the input and
// no threading assumptions. In particular it carries no process-type assertion,
// because EXIFParser is legitimately used in the parent process to decode
// chrome images. Restricting the scanning of user-supplied files to content
// processes is the caller's job.
EXIFScanResult ScanEXIF(Span<const uint8_t> aBytes, uint64_t aFileSize);

}  // namespace mozilla::image

#endif  // mozilla_image_decoders_EXIFScanner_h
