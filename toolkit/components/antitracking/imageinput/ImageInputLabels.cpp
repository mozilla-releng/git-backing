/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputLabels.h"

#include "nsString.h"
#include "nsUnicharUtils.h"

namespace mozilla::imageinput {

const char* InputTypeLabel(ImageInputSource aInputType) {
  switch (aInputType) {
    case ImageInputSource::FilePicker:
      return "file_picker";
    case ImageInputSource::DirectoryPicker:
      return "directory_picker";
    case ImageInputSource::Drop:
      return "drop";
    case ImageInputSource::Paste:
      return "paste";
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled image input type");
  return "other";
}

const char* DetectedFormatLabel(image::EXIFContainer aContainer) {
  switch (aContainer) {
    case image::EXIFContainer::JPEG:
      return "jpeg";
    case image::EXIFContainer::PNG:
      return "png";
    case image::EXIFContainer::TIFF:
      return "tiff";
    case image::EXIFContainer::BigTIFF:
      return "bigtiff";
    case image::EXIFContainer::WebP:
      return "webp";
    case image::EXIFContainer::HEIF:
      return "heif";
    case image::EXIFContainer::AVIF:
      return "avif";
    case image::EXIFContainer::GIF:
      return "gif";
    case image::EXIFContainer::BMP:
      return "bmp";
    case image::EXIFContainer::JXL:
      return "jxl";
    case image::EXIFContainer::SVG:
      return "svg";
    case image::EXIFContainer::Unknown:
      return "unknown";
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled container");
  return "unknown";
}

const char* ScanOutcomeLabel(image::EXIFScanOutcome aOutcome) {
  switch (aOutcome) {
    case image::EXIFScanOutcome::Complete:
      return "ok";
    case image::EXIFScanOutcome::NoEXIF:
      return "no_exif";
    case image::EXIFScanOutcome::UnsupportedContainer:
      return "unsupported_container";
    case image::EXIFScanOutcome::MalformedContainer:
      return "malformed_container";
    case image::EXIFScanOutcome::UnsupportedEXIFEncoding:
      return "unsupported_exif_encoding";
    case image::EXIFScanOutcome::EXIFTruncated:
      return "exif_truncated";
    case image::EXIFScanOutcome::EXIFLocationUnknown:
      return "exif_location_unknown";
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled scan outcome");
  return "read_error";
}

const char* HasGPSLabel(const image::EXIFScanResult& aResult) {
  if (aResult.gpsPresence == image::EXIFGPSPresence::Position) {
    return "true";
  }

  switch (aResult.outcome) {
    case image::EXIFScanOutcome::Complete:
      // Zeroed coordinates are counted with the partially stripped files. Both
      // are a GPS IFD that no longer says where the photo was taken, which is
      // all this metric asks. The parser keeps them apart; see EXIFGPSPresence.
      if (aResult.gpsPresence == image::EXIFGPSPresence::NoPosition ||
          aResult.gpsPresence == image::EXIFGPSPresence::ZeroPosition) {
        return "ifd_only";
      }
      return "false";
    case image::EXIFScanOutcome::NoEXIF:
      return "false";
    default:
      return "unknown";
  }
}

const char* DeclaredTypeLabel(const nsACString& aType) {
  if (aType.IsEmpty()) {
    return "empty";
  }

  // The image types from netwerk/mime/nsMimeTypes.h that occur in practice,
  // plus the two HEIF types that file has no entry for.
  static const char* const kKnown[] = {
      "image/jpeg",    "image/png",   "image/gif",   "image/webp",
      "image/avif",    "image/tiff",  "image/bmp",   "image/x-icon",
      "image/svg+xml", "image/jxl",   "image/heic",  "image/heif",
      "image/apng",    "image/x-png", "image/pjpeg", "image/vnd.microsoft.icon",
  };

  for (const char* known : kKnown) {
    if (aType.EqualsASCII(known)) {
      return known;
    }
  }
  return "other";
}

bool IsCandidateImage(const nsACString& aType, const nsAString& aName) {
  if (StringBeginsWith(aType, "image/"_ns)) {
    return true;
  }

  static const char16_t* const kExtensions[] = {
      u".jpg",  u".jpeg", u".png",  u".tif", u".tiff", u".dng", u".heic",
      u".heif", u".webp", u".avif", u".gif", u".bmp",  u".svg", u".jxl",
  };

  for (const char16_t* extension : kExtensions) {
    if (StringEndsWith(aName, nsDependentString(extension),
                       nsCaseInsensitiveStringComparator)) {
      return true;
    }
  }
  return false;
}

}  // namespace mozilla::imageinput
