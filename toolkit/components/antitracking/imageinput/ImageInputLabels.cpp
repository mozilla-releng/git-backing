/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputLabels.h"

#include "nsIPrincipal.h"
#include "nsString.h"
#include "nsUnicharUtils.h"

namespace mozilla::imageinput {

nsLiteralCString InputTypeLabel(ImageInputSource aInputType) {
  switch (aInputType) {
    case ImageInputSource::FilePicker:
      return "file_picker"_ns;
    case ImageInputSource::DirectoryPicker:
      return "directory_picker"_ns;
    case ImageInputSource::Drop:
      return "drop"_ns;
    case ImageInputSource::Paste:
      return "paste"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled image input type");
  return "other"_ns;
}

nsLiteralCString DetectedFormatLabel(image::EXIFContainer aContainer) {
  switch (aContainer) {
    case image::EXIFContainer::JPEG:
      return "jpeg"_ns;
    case image::EXIFContainer::PNG:
      return "png"_ns;
    case image::EXIFContainer::TIFF:
      return "tiff"_ns;
    case image::EXIFContainer::BigTIFF:
      return "bigtiff"_ns;
    case image::EXIFContainer::WebP:
      return "webp"_ns;
    case image::EXIFContainer::HEIF:
      return "heif"_ns;
    case image::EXIFContainer::AVIF:
      return "avif"_ns;
    case image::EXIFContainer::GIF:
      return "gif"_ns;
    case image::EXIFContainer::BMP:
      return "bmp"_ns;
    case image::EXIFContainer::JXL:
      return "jxl"_ns;
    case image::EXIFContainer::SVG:
      return "svg"_ns;
    case image::EXIFContainer::Unknown:
      return "unknown"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled container");
  return "unknown"_ns;
}

nsLiteralCString ScanOutcomeLabel(image::EXIFScanOutcome aOutcome) {
  switch (aOutcome) {
    case image::EXIFScanOutcome::Complete:
      return "ok"_ns;
    case image::EXIFScanOutcome::NoEXIF:
      return "no_exif"_ns;
    case image::EXIFScanOutcome::UnsupportedContainer:
      return "unsupported_container"_ns;
    case image::EXIFScanOutcome::MalformedContainer:
      return "malformed_container"_ns;
    case image::EXIFScanOutcome::UnsupportedEXIFEncoding:
      return "unsupported_exif_encoding"_ns;
    case image::EXIFScanOutcome::EXIFTruncated:
      return "exif_truncated"_ns;
    case image::EXIFScanOutcome::EXIFLocationUnknown:
      return "exif_location_unknown"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled scan outcome");
  return "read_error"_ns;
}

nsLiteralCString HasGPSLabel(const image::EXIFScanResult& aResult) {
  if (aResult.gpsPresence == image::EXIFGPSPresence::Position) {
    return "true"_ns;
  }

  switch (aResult.outcome) {
    case image::EXIFScanOutcome::Complete:
      // Zeroed coordinates are counted with the partially stripped files. Both
      // are a GPS IFD that no longer says where the photo was taken, which is
      // all this metric asks. The parser keeps them apart; see EXIFGPSPresence.
      if (aResult.gpsPresence == image::EXIFGPSPresence::NoPosition ||
          aResult.gpsPresence == image::EXIFGPSPresence::ZeroPosition) {
        return "ifd_only"_ns;
      }
      return "false"_ns;
    case image::EXIFScanOutcome::NoEXIF:
      return "false"_ns;
    default:
      return "unknown"_ns;
  }
}

bool ShouldRecordFor(nsIPrincipal* aPrincipal) {
  // The global may have gone away, in which case there is nothing to attribute
  // this to.
  if (!aPrincipal) {
    return false;
  }

  // A sandboxed iframe or a data: document is still web content, and the file
  // still becomes readable to script there.
  if (aPrincipal->GetIsNullPrincipal()) {
    return true;
  }

  return aPrincipal->GetIsContentPrincipal() &&
         (aPrincipal->SchemeIs("http") || aPrincipal->SchemeIs("https") ||
          aPrincipal->SchemeIs("file"));
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
