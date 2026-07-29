/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_decoders_EXIF_h
#define mozilla_image_decoders_EXIF_h

#include <stdint.h>

#include "Orientation.h"
#include "mozilla/Maybe.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/image/Resolution.h"
#include "nsDebug.h"
#include "nsTArray.h"

namespace mozilla::image {

enum class ByteOrder : uint8_t { Unknown, LittleEndian, BigEndian };

// The largest Exif block a JPEG can carry. An APP1 segment's 16-bit length
// field caps the bytes after it at 65533, and the block handed to the parser is
// exactly those bytes: the "Exif\0\0" identifier code followed by the TIFF
// block. Exif 3.0 does not define a multi-segment form for Exif, so this really
// is the ceiling. See Section 4.7.2. This bounds the whole block including the
// identifier, so it must not subtract the identifier's 6 bytes -- doing so
// would reject a segment that is filled to the legal maximum.
static constexpr uint32_t kMaxJPEGEXIFLength = 65533;

// Exif carried anywhere other than a JPEG APP1 segment has no comparable
// limit. PNG's eXIf chunk length field alone allows just under 2 GiB, so
// callers reading from such a container are responsible for bounding the input
// themselves.
static constexpr uint32_t kMaxEXIFLength = 0x7FFFFFFFu;

// Which IFD a walk is currently inside.
//
// This is not cosmetic. Tag numbers are namespaced per IFD and they collide:
// 0x0001 and 0x0002 are GPSLatitudeRef and GPSLatitude in the GPS attribute
// IFD, but InteroperabilityIndex and InteroperabilityVersion in the
// Interoperability IFD, which is reached from the Exif IFD via tag 0xA005 and
// is present in the large majority of camera JPEGs. A walk that dispatched on
// tag number alone would report GPS data on almost every photo.
enum class EXIFIFDKind : uint8_t {
  Root,  // IFD0
  Exif,  // the Exif private IFD, reached via tag 0x8769
  GPS,   // the GPS attribute IFD, reached via tag 0x8825
};

// Whether the metadata can say where the image was captured.
enum class EXIFGPSPresence : uint8_t {
  // No GPS attribute IFD was reached.
  None,
  // A GPS attribute IFD exists but carries no usable position -- commonly a
  // GPSVersionID and nothing else.
  NoPosition,
  // Both coordinates are present and well formed, and both are exactly zero:
  // Null Island, in the Gulf of Guinea.
  ZeroPosition,
  // Both GPSLatitude and GPSLongitude are present, well formed, and not both
  // zero.
  Position,
};

// Whether to do the extra work of looking for GPS metadata.
enum class EXIFParseTarget : uint8_t {
  OrientationAndResolution,
  All,
};

// A decoded geographic position, in signed decimal degrees: latitude negative
// south of the equator, longitude negative west of the prime meridian, and
// altitude, when present, negative below sea level.
struct EXIFGPSCoordinates {
  double latitudeDegrees = 0.0;
  double longitudeDegrees = 0.0;
  Maybe<double> altitudeMeters;
};

struct EXIFData {
  const Orientation orientation = Orientation();
  const Resolution resolution = Resolution();
  const EXIFGPSPresence gpsPresence = EXIFGPSPresence::None;
  // The decoded position. Present only when gpsPresence is Position and the
  // parse asked for coordinates (EXIFParseTarget::All).
  const Maybe<EXIFGPSCoordinates> gpsCoordinates;
  // Whether the TIFF structure was valid enough to walk. False means Parse
  // bailed before reaching the entries -- a bad identifier code, TIFF header or
  // IFD0 offset -- so the fields above are defaults rather than findings, and
  // gpsPresence in particular is a non-answer rather than a confident None.
  const bool parsedSuccessfully = false;
};

struct ParsedEXIFData;

enum class ResolutionUnit : uint8_t {
  Dpi,
  Dpcm,
};

// A single 12-byte IFD entry. See Section 4.6.2.
//
// The trailing 4 bytes are kept raw rather than interpreted here because their
// meaning depends on the entry: if the value fits in 4 bytes it is stored
// directly, left-aligned; otherwise the 4 bytes are a byte offset, relative to
// the start of the TIFF header, to where the value actually lives.
struct EXIFEntry {
  uint16_t mTag = 0;
  uint16_t mType = 0;
  uint32_t mCount = 0;
  uint8_t mValue[4] = {};

  uint16_t ValueAsUInt16(ByteOrder aByteOrder) const;
  uint32_t ValueAsUInt32(ByteOrder aByteOrder) const;
};

class EXIFParser {
 public:
  // aExpectExifIdCode Determines whether to expect the leading "Exif\0\0". True
  // for exif in jpeg, false for exif in png.
  // aMaxLength is the largest Exif block the containing format can legally
  // hold; anything longer is rejected outright. It has no default because the
  // right value differs by an order of magnitude between containers, and
  // silently applying a JPEG-sized limit elsewhere loses metadata.
  static EXIFData Parse(
      bool aExpectExifIdCode, const uint8_t* aData, const uint32_t aLength,
      const gfx::IntSize& aRealImageSize, const uint32_t aMaxLength,
      EXIFParseTarget aTarget = EXIFParseTarget::OrientationAndResolution) {
    EXIFParser parser(aExpectExifIdCode, aTarget);
    return parser.ParseEXIF(aData, aLength, aRealImageSize, aMaxLength);
  }

 private:
  EXIFParser(bool aExpectExifIdCode, EXIFParseTarget aTarget)
      : mStart(nullptr),
        mCurrent(nullptr),
        mLength(0),
        mRemainingLength(0),
        mByteOrder(ByteOrder::Unknown),
        mExpectExifIdCode(aExpectExifIdCode),
        mTarget(aTarget) {}

  EXIFData ParseEXIF(const uint8_t* aData, const uint32_t aLength,
                     const gfx::IntSize& aRealImageSize,
                     const uint32_t aMaxLength);
  bool ParseEXIFHeader();
  bool ParseTIFFHeader(uint32_t& aIFD0OffsetOut);

  void ParseIFD(ParsedEXIFData&, EXIFIFDKind aKind, uint32_t aDepth = 0);

  // Follows a pointer tag into the IFD it names, at most once per distinct
  // target offset.
  void ParseSubIFD(ParsedEXIFData&, const EXIFEntry&, EXIFIFDKind aKind,
                   uint32_t aDepth);

  // Reads one 12-byte entry. On success the cursor has advanced by exactly 12
  // bytes and so is positioned on the next entry, whatever the tag handlers
  // then do with the value. Keeping that guarantee here is what lets a
  // malformed entry be skipped rather than abandoning the rest of the IFD.
  bool ReadEntry(EXIFEntry& aOut);

  // Tag handlers. These do not move the cursor, except to follow an offset to
  // an out-of-line value and come back again.
  bool ParseOrientation(const EXIFEntry&, Orientation&);
  bool ParseResolution(const EXIFEntry&, Maybe<float>&);
  bool ParseResolutionUnit(const EXIFEntry&, Maybe<ResolutionUnit>&);
  bool ParseDimension(const EXIFEntry&, Maybe<uint32_t>&);

  // Dispatches one entry of the GPS attribute IFD. A leaf: nothing here points
  // at another IFD, so the depth and fan-out limits stop mattering below this
  // point. An entry this does not recognise is ignored.
  void ParseGPSEntry(ParsedEXIFData&, const EXIFEntry&);

  // Reads a GPS coordinate (three RATIONALs: degrees, minutes, seconds) and, if
  // it is well formed and every component is in range, reports its unsigned
  // magnitude in decimal degrees through aOutDegrees. aMaxDegrees bounds the
  // degrees component (90 for latitude, 180 for longitude). The sign comes
  // separately from the coordinate's reference tag.
  bool ParseGPSCoordinate(const EXIFEntry&, uint32_t aMaxDegrees,
                          double& aOutDegrees);

  // Reads GPSAltitude, a single RATIONAL giving metres. The sign comes
  // separately from GPSAltitudeRef.
  bool ParseGPSAltitude(const EXIFEntry&, double& aOutMeters);

  bool Initialize(const uint8_t* aData, const uint32_t aLength,
                  const uint32_t aMaxLength);
  void Advance(const uint32_t aDistance);
  void JumpTo(const uint32_t aOffset);

  uint32_t CurrentOffset() const { return mCurrent - mStart; }

  uint32_t TIFFHeaderStart() const;

  class ScopedJump {
    EXIFParser& mParser;
    uint32_t mOldOffset;

   public:
    ScopedJump(EXIFParser& aParser, uint32_t aOffset)
        : mParser(aParser), mOldOffset(aParser.CurrentOffset()) {
      mParser.JumpTo(aOffset);
    }

    ~ScopedJump() { mParser.JumpTo(mOldOffset); }
  };

  bool MatchString(const char* aString, const uint32_t aLength);
  bool ReadUInt16(uint16_t& aOut);
  bool ReadUInt32(uint32_t& aOut);

  // Reads a RATIONAL stored out of line at aTIFFRelativeOffset, restoring the
  // cursor afterwards.
  bool ReadRationalAt(uint32_t aTIFFRelativeOffset, float& aOut);

  const uint8_t* mStart;
  const uint8_t* mCurrent;
  uint32_t mLength;
  uint32_t mRemainingLength;
  ByteOrder mByteOrder;
  bool mExpectExifIdCode;
  EXIFParseTarget mTarget;

  // Offsets of the IFDs we have already walked, so that we walk each at most
  // once. Real files have two to four.
  AutoTArray<uint32_t, 8> mVisitedIFDOffsets;
};

}  // namespace mozilla::image

#endif  // mozilla_image_decoders_EXIF_h
