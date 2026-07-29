/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EXIFScanner.h"

#include <string.h>

#include <algorithm>

#include "mozilla/CheckedInt.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/Maybe.h"

namespace mozilla::image {

namespace {

// Everything a locator needs to know about the bytes it was handed.
struct ScanContext {
  Span<const uint8_t> mBytes;
  // Whether mBytes covers the file to its end. This is what separates a
  // genuinely truncated file, which is malformed, from our having simply read
  // less of a well-formed one than we needed.
  bool mHaveWholeFile = false;
};

EXIFScanOutcome RanOutOfBytes(const ScanContext& aContext) {
  return aContext.mHaveWholeFile ? EXIFScanOutcome::MalformedContainer
                                 : EXIFScanOutcome::EXIFLocationUnknown;
}

// The part of [aStart, aStart + aLength) we actually have.
Span<const uint8_t> AvailableRange(Span<const uint8_t> aBytes, size_t aStart,
                                   uint64_t aLength) {
  if (aStart >= aBytes.Length()) {
    return Span<const uint8_t>();
  }
  return aBytes.Subspan(aStart,
                        std::min<uint64_t>(aLength, aBytes.Length() - aStart));
}

bool StartsWith(Span<const uint8_t> aBytes, const char* aPrefix,
                size_t aPrefixLength) {
  return aBytes.Length() >= aPrefixLength &&
         memcmp(aBytes.Elements(), aPrefix, aPrefixLength) == 0;
}

template <size_t N>
bool StartsWithLiteral(Span<const uint8_t> aBytes, const char (&aPrefix)[N]) {
  // N counts the literal's terminating NUL, which is not part of the pattern.
  return StartsWith(aBytes, aPrefix, N - 1);
}

// "II" 42 or "MM" 42, the two ways a TIFF header can begin. See TIFF 6.0
// Section 2, and EXIF Section 4.5.2.
bool StartsWithTIFFMagic(Span<const uint8_t> aBytes) {
  return StartsWithLiteral(aBytes, "II\x2A\x00") ||
         StartsWithLiteral(aBytes, "MM\x00\x2A");
}

// Containers disagree about how an EXIF block is framed, and encoders disagree
// with the containers:
//
//   - A JPEG APP1 segment begins with the "Exif\0\0" identifier code, per
//     Section 4.7.2.
//   - A PNG eXIf chunk is specified not to carry that identifier, yet some
//     encoders write it anyway.
//
// Rather than trust the container, look for the TIFF header that has to follow
// either way. Returns its offset within aBlock.
Maybe<uint32_t> FindTIFFHeaderInEXIFBlock(Span<const uint8_t> aBlock) {
  if (StartsWithTIFFMagic(aBlock)) {
    return Some(0u);
  }

  constexpr uint32_t kExifIdCodeLength = 6;
  if (StartsWithLiteral(aBlock, "Exif\0\0") &&
      StartsWithTIFFMagic(aBlock.From(kExifIdCodeLength))) {
    return Some(kExifIdCodeLength);
  }

  return Nothing();
}

// An EXIF block we have found, and whether we have all of it.
struct LocatedEXIF {
  Span<const uint8_t> mBlock;
  bool mComplete = false;
};

void FinishWithEXIF(const ScanContext& aContext, const LocatedEXIF& aLocated,
                    EXIFScanResult& aResult) {
  // What a block we do not have in full means. With the whole file in hand its
  // declared length overran the file, which is malformed; otherwise we have
  // simply not read far enough, which is truncation and says nothing either
  // way.
  const EXIFScanOutcome incomplete = aContext.mHaveWholeFile
                                         ? EXIFScanOutcome::MalformedContainer
                                         : EXIFScanOutcome::EXIFTruncated;

  Maybe<uint32_t> tiffStart = FindTIFFHeaderInEXIFBlock(aLocated.mBlock);
  if (tiffStart.isNothing()) {
    // We are only here because the container said this was EXIF, so bytes that
    // are not a TIFF header mean the file is inconsistent with itself. If we do
    // not have the whole block yet, we may simply not have reached the header.
    aResult.outcome =
        aLocated.mComplete ? EXIFScanOutcome::MalformedContainer : incomplete;
    return;
  }

  Span<const uint8_t> tiff = aLocated.mBlock.From(*tiffStart);
  if (tiff.Length() > kMaxEXIFLength) {
    aResult.outcome = EXIFScanOutcome::MalformedContainer;
    return;
  }

  // The real image size is only used for the density-correction sanity check,
  // which produces a resolution we do not look at.
  EXIFData data = EXIFParser::Parse(
      /* aExpectExifIdCode = */ false, tiff.Elements(),
      static_cast<uint32_t>(tiff.Length()), gfx::IntSize(), kMaxEXIFLength,
      EXIFParseTarget::All);

  // A block present in full that did not parse as a TIFF structure tells us
  // nothing about GPS, so it must not read as a confident absence. The parser
  // returns a default (gpsPresence None) on structural failure just as it does
  // for a valid file with no GPS, so the two are told apart only by this flag.
  // When we do not have all of the block the failure may be for lack of bytes,
  // so that is reported as truncation rather than as malformed.
  if (!data.parsedSuccessfully) {
    aResult.outcome =
        aLocated.mComplete ? EXIFScanOutcome::MalformedContainer : incomplete;
    return;
  }

  aResult.gpsPresence = data.gpsPresence;
  // Present only alongside a Position, and a validated position cannot be
  // un-found by more bytes, so it rides out even when the block was clipped --
  // the same authoritativeness as gpsPresence == Position.
  aResult.gpsCoordinates = data.gpsCoordinates;

  // The outcome describes what happened to the scan, not what it found. Callers
  // combine the two: a position is conclusive however little of the block we
  // had, whereas the absence of one only means something if we had all of it.
  aResult.outcome = aLocated.mComplete ? EXIFScanOutcome::Complete : incomplete;
}

/////////////////////////////////////////////////////////////////////////////
// JPEG. ISO/IEC 10918-1 marker syntax; EXIF Section 4.7.2 for the APP1 layout.
/////////////////////////////////////////////////////////////////////////////

void ScanJPEG(const ScanContext& aContext, EXIFScanResult& aResult) {
  Span<const uint8_t> bytes = aContext.mBytes;

  // Past the SOI marker that the sniffer already matched.
  size_t offset = 2;

  while (true) {
    // Any number of 0xFF bytes may pad the gap before a marker.
    while (offset < bytes.Length() && bytes[offset] == 0xFF) {
      ++offset;
    }
    if (offset >= bytes.Length()) {
      aResult.outcome = RanOutOfBytes(aContext);
      return;
    }

    const uint8_t marker = bytes[offset++];

    // Standalone markers: TEM and the restart markers carry no payload.
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }

    // Start of scan, or end of image. Entropy-coded data follows SOS and no
    // further metadata segment can precede the image after that point.
    if (marker == 0xDA || marker == 0xD9) {
      aResult.outcome = EXIFScanOutcome::NoEXIF;
      return;
    }

    if (offset + 2 > bytes.Length()) {
      aResult.outcome = RanOutOfBytes(aContext);
      return;
    }

    // The length field counts its own two bytes.
    const uint16_t segmentLength = BigEndian::readUint16(&bytes[offset]);
    if (segmentLength < 2) {
      aResult.outcome = EXIFScanOutcome::MalformedContainer;
      return;
    }
    const size_t payloadStart = offset + 2;
    const uint32_t payloadLength = segmentLength - 2u;

    // APP1 is where EXIF lives, but it is also where XMP lives, and a photo
    // commonly carries both in either order. Dispatching on the identifier
    // string rather than on the marker is what tells them apart.
    if (marker == 0xE1) {
      Span<const uint8_t> payload =
          AvailableRange(bytes, payloadStart, payloadLength);

      if (StartsWithLiteral(payload, "Exif\0\0")) {
        FinishWithEXIF(aContext, {payload, payload.Length() == payloadLength},
                       aResult);
        return;
      }

      // Not enough of the payload to read the identifier, so we cannot rule out
      // that this was the EXIF segment.
      if (payload.Length() < 6 && payload.Length() < payloadLength) {
        aResult.outcome = RanOutOfBytes(aContext);
        return;
      }
    }

    const CheckedUint64 next = CheckedUint64(payloadStart) + payloadLength;
    if (!next.isValid() || next.value() > bytes.Length()) {
      aResult.outcome = RanOutOfBytes(aContext);
      return;
    }
    offset = static_cast<size_t>(next.value());
  }
}

/////////////////////////////////////////////////////////////////////////////
// PNG. The eXIf chunk, added in the 1.5.0 extensions and normative in the PNG
// Specification (Third Edition), carries a bare TIFF block with no identifier
// code.
/////////////////////////////////////////////////////////////////////////////

bool ChunkTypeIs(const uint8_t* aType, const char (&aName)[5]) {
  return memcmp(aType, aName, 4) == 0;
}

// ImageMagick stores a hex-encoded copy of the whole JPEG APP1 segment in a
// text chunk under one of these keywords. It is common enough in converted
// files that treating it as "no EXIF" would be a meaningful false negative.
bool HasRawProfileKeyword(Span<const uint8_t> aChunkData) {
  return StartsWithLiteral(aChunkData, "Raw profile type exif") ||
         StartsWithLiteral(aChunkData, "Raw profile type APP1");
}

void ScanPNG(const ScanContext& aContext, EXIFScanResult& aResult) {
  Span<const uint8_t> bytes = aContext.mBytes;

  // Past the 8-byte signature that the sniffer already matched.
  size_t offset = 8;

  while (true) {
    // Chunk header: a 4-byte big-endian length followed by a 4-byte type.
    if (offset + 8 > bytes.Length()) {
      aResult.outcome = RanOutOfBytes(aContext);
      return;
    }

    const uint32_t dataLength = BigEndian::readUint32(&bytes[offset]);
    const uint8_t* type = &bytes[offset + 4];
    const size_t dataStart = offset + 8;

    // A chunk length is a four-byte unsigned integer whose value may not
    // exceed 2^31-1, so the top bit must be clear. See PNG Specification
    // (Third Edition), 5.3, Table 5.
    if (dataLength > 0x7FFFFFFFu) {
      aResult.outcome = EXIFScanOutcome::MalformedContainer;
      return;
    }

    if (ChunkTypeIs(type, "eXIf")) {
      Span<const uint8_t> payload =
          AvailableRange(bytes, dataStart, dataLength);
      FinishWithEXIF(aContext, {payload, payload.Length() == dataLength},
                     aResult);
      return;
    }

    if (ChunkTypeIs(type, "tEXt") || ChunkTypeIs(type, "zTXt")) {
      if (HasRawProfileKeyword(AvailableRange(bytes, dataStart, dataLength))) {
        aResult.outcome = EXIFScanOutcome::UnsupportedEXIFEncoding;
        return;
      }
    }

    if (ChunkTypeIs(type, "IEND")) {
      aResult.outcome = EXIFScanOutcome::NoEXIF;
      return;
    }

    // The data, then a 4-byte CRC. CRCs are not verified: we are locating
    // metadata, not decoding an image, and a bad CRC would not change where the
    // next chunk starts.
    const CheckedUint64 next = CheckedUint64(dataStart) + dataLength + 4;
    if (!next.isValid() || next.value() > bytes.Length()) {
      aResult.outcome = RanOutOfBytes(aContext);
      return;
    }
    offset = static_cast<size_t>(next.value());
  }
}

/////////////////////////////////////////////////////////////////////////////
// Sniffing.
/////////////////////////////////////////////////////////////////////////////

EXIFContainer SniffContainer(Span<const uint8_t> aBytes) {
  if (StartsWithLiteral(aBytes, "\xFF\xD8\xFF")) {
    return EXIFContainer::JPEG;
  }
  if (StartsWithLiteral(aBytes, "\x89PNG\r\n\x1A\n")) {
    return EXIFContainer::PNG;
  }
  if (StartsWithTIFFMagic(aBytes)) {
    return EXIFContainer::TIFF;
  }
  if (StartsWithLiteral(aBytes, "II\x2B\x00") ||
      StartsWithLiteral(aBytes, "MM\x00\x2B")) {
    return EXIFContainer::BigTIFF;
  }
  if (StartsWithLiteral(aBytes, "RIFF") && aBytes.Length() >= 12 &&
      StartsWithLiteral(aBytes.From(8), "WEBP")) {
    return EXIFContainer::WebP;
  }
  if (StartsWithLiteral(aBytes, "GIF8")) {
    return EXIFContainer::GIF;
  }
  if (StartsWithLiteral(aBytes, "BM")) {
    return EXIFContainer::BMP;
  }
  // A JPEG XL container, or a naked codestream.
  if (StartsWithLiteral(aBytes, "\x00\x00\x00\x0CJXL \r\n\x87\n") ||
      StartsWithLiteral(aBytes, "\xFF\x0A")) {
    return EXIFContainer::JXL;
  }

  // ISO base media files put a box length in the first four bytes and the
  // "ftyp" box type in the next four; the brand that follows says which
  // dialect. See ISO/IEC 14496-12, ISO/IEC 23008-12, and the AV1 Image File
  // Format specification for the avif and avis brands.
  if (aBytes.Length() >= 12 && StartsWithLiteral(aBytes.From(4), "ftyp")) {
    Span<const uint8_t> brand = aBytes.From(8);
    if (StartsWithLiteral(brand, "avif") || StartsWithLiteral(brand, "avis")) {
      return EXIFContainer::AVIF;
    }
    return EXIFContainer::HEIF;
  }

  if (StartsWithLiteral(aBytes, "<?xml") || StartsWithLiteral(aBytes, "<svg")) {
    return EXIFContainer::SVG;
  }

  return EXIFContainer::Unknown;
}

}  // namespace

EXIFScanResult ScanEXIF(Span<const uint8_t> aBytes, uint64_t aFileSize) {
  EXIFScanResult result;
  result.container = SniffContainer(aBytes);

  const ScanContext context{aBytes, aBytes.Length() >= aFileSize};

  switch (result.container) {
    case EXIFContainer::JPEG:
      ScanJPEG(context, result);
      break;
    case EXIFContainer::PNG:
      ScanPNG(context, result);
      break;
    case EXIFContainer::TIFF:
      // The file is the EXIF block; there is no container to walk.
      FinishWithEXIF(context, {aBytes, context.mHaveWholeFile}, result);
      break;

    case EXIFContainer::Unknown:
    case EXIFContainer::BigTIFF:
    case EXIFContainer::WebP:
    case EXIFContainer::HEIF:
    case EXIFContainer::AVIF:
    case EXIFContainer::GIF:
    case EXIFContainer::BMP:
    case EXIFContainer::JXL:
    case EXIFContainer::SVG:
      result.outcome = EXIFScanOutcome::UnsupportedContainer;
      break;
  }

  return result;
}

}  // namespace mozilla::image
