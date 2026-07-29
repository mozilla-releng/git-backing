/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EXIF.h"

#include <string.h>

#include "mozilla/CheckedInt.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/StaticPrefs_image.h"

namespace mozilla::image {

// Section references in this file refer to the EXIF v2.3 standard, also known
// as CIPA DC-008-Translation-2010.
//
// The current edition is EXIF v3.1, CIPA DC-008-Translation-2026 (also JEITA
// CP-3451H). EXIF v3.0 inserted a new section 4.6.4 and renumbered the rest of
// 4.6, so a clause number alone is ambiguous between the editions: references
// added since are given with both, as
// "Section 4.6.7 (Section 4.6.6 in EXIF v2.3)".

// Orientation, XResolution, YResolution and ResolutionUnit are in Section
// 4.6.4, Table 4; PixelXDimension and PixelYDimension in Section 4.6.5, Table
// 7; the two IFD pointers in Section 4.6.3. (In EXIF v3.x, 4.6.5/Table 6,
// 4.6.6/Table 8 and 4.6.3.)
// Typesafe enums are intentionally not used here since we're comparing to raw
// integers produced by parsing.
enum class EXIFTag : uint16_t {
  Orientation = 0x112,
  XResolution = 0x11a,
  YResolution = 0x11b,
  PixelXDimension = 0xa002,
  PixelYDimension = 0xa003,
  ResolutionUnit = 0x128,
  IFDPointer = 0x8769,
  GPSInfoIFDPointer = 0x8825,
};

// Tags in the GPS attribute IFD. See Section 4.6.7, "GPS Attribute
// Information" (Section 4.6.6 in EXIF v2.3).
//
// The GPS IFD defines tags 0x0000 through 0x001F. A coordinate is split across
// two tags: the value (Latitude/Longitude/Altitude) and a reference giving its
// sign (hemisphere, or above/below sea level), which may appear in either
// order. Note that a GPS IFD carrying nothing but GPSVersionID (0x0000) is
// common in the wild, which is why the presence of the IFD is tracked
// separately from the coordinates.
enum class EXIFGPSTag : uint16_t {
  LatitudeRef = 0x0001,   // "N" or "S"
  Latitude = 0x0002,      // degrees, minutes, seconds
  LongitudeRef = 0x0003,  // "E" or "W"
  Longitude = 0x0004,     // degrees, minutes, seconds
  AltitudeRef = 0x0005,  // 0/1 above/below ellipsoid, 2/3 above/below sea level
  Altitude = 0x0006,     // metres
};

// See Section 4.6.2. Type 129, UTF-8, was added in EXIF v3.0.
enum EXIFType {
  ByteType = 1,
  ASCIIType = 2,
  ShortType = 3,
  LongType = 4,
  RationalType = 5,
  UndefinedType = 7,
  SignedLongType = 9,
  SignedRational = 10,
  UTF8Type = 129,
};

static const char* EXIFHeader = "Exif\0\0";
static const uint32_t EXIFHeaderLength = 6;

uint32_t EXIFParser::TIFFHeaderStart() const {
  return mExpectExifIdCode ? EXIFHeaderLength : 0;
}

struct ParsedEXIFData {
  Orientation orientation;
  Maybe<float> resolutionX;
  Maybe<float> resolutionY;
  Maybe<uint32_t> pixelXDimension;
  Maybe<uint32_t> pixelYDimension;
  Maybe<ResolutionUnit> resolutionUnit;
  bool sawGPSIFD = false;
  // Unsigned magnitudes in decimal degrees, set only when ParseGPSCoordinate
  // accepts the entry. The ref tags carry the sign; CoordinatesFromParsedData
  // applies it once the IFD has been walked.
  Maybe<double> gpsLatitude;
  Maybe<double> gpsLongitude;
  char gpsLatitudeRef = '\0';   // 'N' or 'S'
  char gpsLongitudeRef = '\0';  // 'E' or 'W'
  Maybe<double> gpsAltitude;    // unsigned magnitude in metres
  uint8_t gpsAltitudeRef = 0;   // 0/1 ellipsoid, 2/3 sea level; odd is below
};

static float ToDppx(float aResolution, ResolutionUnit aUnit) {
  constexpr float kPointsPerInch = 72.0f;
  constexpr float kPointsPerCm = 1.0f / 2.54f;
  switch (aUnit) {
    case ResolutionUnit::Dpi:
      return aResolution / kPointsPerInch;
    case ResolutionUnit::Dpcm:
      return aResolution / kPointsPerCm;
  }
  MOZ_CRASH("Unknown resolution unit?");
}

static bool HasBothCoordinates(const ParsedEXIFData& aData) {
  return aData.gpsLatitude.isSome() && aData.gpsLongitude.isSome();
}

// Latitude and longitude both exactly zero is Null Island, in the Gulf of
// Guinea. Latitude alone being zero is ordinary -- that is just the equator --
// so both have to be zero. What it means for a file to say this is the caller's
// to decide; the parser only reports that it does.
static bool IsZeroPosition(const ParsedEXIFData& aData) {
  return HasBothCoordinates(aData) && *aData.gpsLatitude == 0.0 &&
         *aData.gpsLongitude == 0.0;
}

static EXIFGPSPresence GPSPresenceFromParsedData(const ParsedEXIFData& aData) {
  if (HasBothCoordinates(aData)) {
    return IsZeroPosition(aData) ? EXIFGPSPresence::ZeroPosition
                                 : EXIFGPSPresence::Position;
  }
  return aData.sawGPSIFD ? EXIFGPSPresence::NoPosition : EXIFGPSPresence::None;
}

static Maybe<EXIFGPSCoordinates> CoordinatesFromParsedData(
    const ParsedEXIFData& aData) {
  // Coordinates accompany a Position and nothing else, so a caller can rely on
  // gpsCoordinates.isSome() meaning exactly the same as gpsPresence ==
  // Position. Deferring to the classifier is what keeps that true.
  if (GPSPresenceFromParsedData(aData) != EXIFGPSPresence::Position) {
    return Nothing();
  }

  // The refs give the sign. 'S' and 'W' are the negative hemispheres; a missing
  // or unexpected ref leaves the magnitude unsigned, which is the most that can
  // honestly be said.
  double latitude = *aData.gpsLatitude;
  if (aData.gpsLatitudeRef == 'S') {
    latitude = -latitude;
  }
  double longitude = *aData.gpsLongitude;
  if (aData.gpsLongitudeRef == 'W') {
    longitude = -longitude;
  }

  EXIFGPSCoordinates coordinates{latitude, longitude, Nothing()};
  if (aData.gpsAltitude.isSome()) {
    double altitude = *aData.gpsAltitude;
    // EXIF v3.0 Section 4.6.7.1.6: the odd references are the negative ones.
    if (aData.gpsAltitudeRef == 1 || aData.gpsAltitudeRef == 3) {
      altitude = -altitude;
    }
    coordinates.altitudeMeters = Some(altitude);
  }
  return Some(coordinates);
}

static Resolution ResolutionFromParsedData(const ParsedEXIFData& aData,
                                           const gfx::IntSize& aRealImageSize) {
  if (!aData.resolutionUnit || !aData.resolutionX || !aData.resolutionY) {
    return {};
  }

  Resolution resolution{ToDppx(*aData.resolutionX, *aData.resolutionUnit),
                        ToDppx(*aData.resolutionY, *aData.resolutionUnit)};

  if (StaticPrefs::image_exif_density_correction_sanity_check_enabled()) {
    if (!aData.pixelXDimension || !aData.pixelYDimension) {
      return {};
    }

    const gfx::IntSize exifSize(*aData.pixelXDimension, *aData.pixelYDimension);

    gfx::IntSize scaledSize = aRealImageSize;
    resolution.ApplyTo(scaledSize.width, scaledSize.height);

    if (exifSize != scaledSize) {
      return {};
    }
  }

  return resolution;
}

/////////////////////////////////////////////////////////////
// Parse EXIF data, typically found in a JPEG's APP1 segment.
/////////////////////////////////////////////////////////////
EXIFData EXIFParser::ParseEXIF(const uint8_t* aData, const uint32_t aLength,
                               const gfx::IntSize& aRealImageSize,
                               const uint32_t aMaxLength) {
  if (!Initialize(aData, aLength, aMaxLength)) {
    return EXIFData();
  }

  if (mExpectExifIdCode) {
    if (!ParseEXIFHeader()) {
      return EXIFData();
    }
  }

  uint32_t offsetIFD;
  if (!ParseTIFFHeader(offsetIFD)) {
    return EXIFData();
  }

  JumpTo(offsetIFD);
  mVisitedIFDOffsets.AppendElement(offsetIFD);

  ParsedEXIFData data;
  ParseIFD(data, EXIFIFDKind::Root);

  return EXIFData{
      data.orientation, ResolutionFromParsedData(data, aRealImageSize),
      GPSPresenceFromParsedData(data), CoordinatesFromParsedData(data),
      /* parsedSuccessfully = */ true};
}

/////////////////////////////////////////////////////////
// Parse the EXIF header. (Section 4.7.2, Figure 30)
/////////////////////////////////////////////////////////
bool EXIFParser::ParseEXIFHeader() {
  return MatchString(EXIFHeader, EXIFHeaderLength);
}

/////////////////////////////////////////////////////////
// Parse the TIFF header. (Section 4.5.2, Table 1)
/////////////////////////////////////////////////////////
bool EXIFParser::ParseTIFFHeader(uint32_t& aIFD0OffsetOut) {
  // Determine byte order.
  if (MatchString("MM\0*", 4)) {
    mByteOrder = ByteOrder::BigEndian;
  } else if (MatchString("II*\0", 4)) {
    mByteOrder = ByteOrder::LittleEndian;
  } else {
    return false;
  }

  // Determine offset of the 0th IFD. It is relative to the beginning of the
  // TIFF header, which begins after the EXIF header, so we need to increase the
  // offset appropriately. Bound it against the block we were actually given
  // rather than against a fixed size, since how large that block may be depends
  // on the container it came out of.
  uint32_t ifd0Offset;
  if (!ReadUInt32(ifd0Offset)) {
    return false;
  }

  CheckedUint32 ifd0Start = CheckedUint32(ifd0Offset) + TIFFHeaderStart();
  if (!ifd0Start.isValid() || ifd0Start.value() >= mLength) {
    return false;
  }

  aIFD0OffsetOut = ifd0Start.value();
  return true;
}

// An arbitrary limit on the amount of pointers that we'll chase, to prevent bad
// inputs getting us stuck.
constexpr uint32_t kMaxEXIFDepth = 16;

// An arbitrary limit on how many distinct IFDs we'll walk. A well-formed file
// needs a handful.
constexpr uint32_t kMaxEXIFIFDCount = 32;

/////////////////////////////////////////////////////////
// Parse the entries in IFD0. (Section 4.6.2)
/////////////////////////////////////////////////////////
void EXIFParser::ParseIFD(ParsedEXIFData& aData, EXIFIFDKind aKind,
                          uint32_t aDepth) {
  if (NS_WARN_IF(aDepth > kMaxEXIFDepth)) {
    return;
  }

  uint16_t entryCount;
  if (!ReadUInt16(entryCount)) {
    return;
  }

  if (aKind == EXIFIFDKind::GPS) {
    // Tag 0x8825 resolved to something structurally valid. Recorded even when
    // there turns out to be no position here, because the difference between
    // "never had one" and "had one removed" is worth keeping.
    aData.sawGPSIFD = true;
  }

  // A handler that rejects its entry leaves the corresponding field at its
  // default and we move on to the next entry. Because ReadEntry always consumes
  // the full 12 bytes, one malformed entry cannot desynchronize the walk or
  // discard the entries that follow it.
  for (uint16_t entry = 0; entry < entryCount; ++entry) {
    EXIFEntry parsedEntry;
    if (!ReadEntry(parsedEntry)) {
      return;
    }

    if (aKind == EXIFIFDKind::GPS) {
      ParseGPSEntry(aData, parsedEntry);
      continue;
    }

    switch (EXIFTag(parsedEntry.mTag)) {
      case EXIFTag::Orientation:
        ParseOrientation(parsedEntry, aData.orientation);
        break;
      case EXIFTag::ResolutionUnit:
        ParseResolutionUnit(parsedEntry, aData.resolutionUnit);
        break;
      case EXIFTag::XResolution:
        ParseResolution(parsedEntry, aData.resolutionX);
        break;
      case EXIFTag::YResolution:
        ParseResolution(parsedEntry, aData.resolutionY);
        break;
      case EXIFTag::PixelXDimension:
        ParseDimension(parsedEntry, aData.pixelXDimension);
        break;
      case EXIFTag::PixelYDimension:
        ParseDimension(parsedEntry, aData.pixelYDimension);
        break;
      // Only IFD0 carries these. Refusing to follow them from anywhere else
      // stops a crafted file from building chains of them, and costs nothing:
      // no real file nests them.
      case EXIFTag::IFDPointer:
        if (aKind == EXIFIFDKind::Root) {
          ParseSubIFD(aData, parsedEntry, EXIFIFDKind::Exif, aDepth);
        }
        break;
      case EXIFTag::GPSInfoIFDPointer:
        if (mTarget == EXIFParseTarget::All && aKind == EXIFIFDKind::Root) {
          ParseSubIFD(aData, parsedEntry, EXIFIFDKind::GPS, aDepth);
        }
        break;

      // Tag 0xA005, the Interoperability IFD pointer, is deliberately not
      // followed. We want nothing from that IFD, and its tag numbering is what
      // collides with the GPS IFD's -- see the comment on EXIFIFDKind.
      default:
        break;
    }
  }
}

void EXIFParser::ParseSubIFD(ParsedEXIFData& aData, const EXIFEntry& aEntry,
                             EXIFIFDKind aKind, uint32_t aDepth) {
  CheckedUint32 offset =
      CheckedUint32(aEntry.ValueAsUInt32(mByteOrder)) + TIFFHeaderStart();
  if (!offset.isValid() || offset.value() >= mLength) {
    return;
  }

  // kMaxEXIFDepth bounds how deep the pointers go, but on its own it does not
  // bound the work: one IFD may repeat a pointer tag thousands of times, and if
  // each one points back at that same IFD the walk fans out combinatorially.
  // Visiting each IFD at most once bounds the whole traversal.
  if (mVisitedIFDOffsets.Length() >= kMaxEXIFIFDCount ||
      mVisitedIFDOffsets.Contains(offset.value())) {
    return;
  }
  mVisitedIFDOffsets.AppendElement(offset.value());

  ScopedJump jump(*this, offset.value());
  ParseIFD(aData, aKind, aDepth + 1);
}

bool EXIFParser::ReadEntry(EXIFEntry& aOut) {
  if (!ReadUInt16(aOut.mTag) || !ReadUInt16(aOut.mType) ||
      !ReadUInt32(aOut.mCount)) {
    return false;
  }

  if (mRemainingLength < sizeof(aOut.mValue)) {
    return false;
  }
  memcpy(aOut.mValue, mCurrent, sizeof(aOut.mValue));
  Advance(sizeof(aOut.mValue));
  return true;
}

uint16_t EXIFEntry::ValueAsUInt16(ByteOrder aByteOrder) const {
  // A value shorter than 4 bytes is left-aligned in the value field, so a SHORT
  // occupies the first two bytes and the rest is padding. See Section 4.6.2.
  switch (aByteOrder) {
    case ByteOrder::LittleEndian:
      return LittleEndian::readUint16(mValue);
    case ByteOrder::BigEndian:
      return BigEndian::readUint16(mValue);
    default:
      MOZ_ASSERT_UNREACHABLE("Should know the byte order by now");
      return 0;
  }
}

uint32_t EXIFEntry::ValueAsUInt32(ByteOrder aByteOrder) const {
  switch (aByteOrder) {
    case ByteOrder::LittleEndian:
      return LittleEndian::readUint32(mValue);
    case ByteOrder::BigEndian:
      return BigEndian::readUint32(mValue);
    default:
      MOZ_ASSERT_UNREACHABLE("Should know the byte order by now");
      return 0;
  }
}

bool EXIFParser::ReadRationalAt(uint32_t aTIFFRelativeOffset, float& aOut) {
  // Values larger than 4 bytes (like rationals) are stored elsewhere in the
  // block, and the entry's value field holds an offset to them. The offset is
  // attacker-controlled, so adding the header start is checked: an unchecked
  // add can wrap a near-UINT32_MAX offset down to a small in-bounds one and
  // read a rational from the header bytes instead of failing.
  CheckedUint32 base = CheckedUint32(aTIFFRelativeOffset) + TIFFHeaderStart();
  if (!base.isValid()) {
    return false;
  }
  ScopedJump jumpToValue(*this, base.value());
  uint32_t numerator;
  if (!ReadUInt32(numerator)) {
    return false;
  }
  uint32_t denominator;
  if (!ReadUInt32(denominator)) {
    return false;
  }
  if (denominator == 0) {
    return false;
  }
  aOut = float(numerator) / float(denominator);
  return true;
}

bool EXIFParser::ParseResolution(const EXIFEntry& aEntry, Maybe<float>& aOut) {
  if (aEntry.mType != RationalType || aEntry.mCount != 1) {
    return false;
  }
  float value;
  if (!ReadRationalAt(aEntry.ValueAsUInt32(mByteOrder), value)) {
    return false;
  }
  if (value == 0.0f) {
    return false;
  }
  aOut = Some(value);
  return true;
}

bool EXIFParser::ParseDimension(const EXIFEntry& aEntry,
                                Maybe<uint32_t>& aOut) {
  if (aEntry.mCount != 1) {
    return false;
  }

  switch (aEntry.mType) {
    case ShortType:
      aOut = Some(uint32_t(aEntry.ValueAsUInt16(mByteOrder)));
      break;
    case LongType:
      aOut = Some(aEntry.ValueAsUInt32(mByteOrder));
      break;
    default:
      return false;
  }
  return true;
}

bool EXIFParser::ParseResolutionUnit(const EXIFEntry& aEntry,
                                     Maybe<ResolutionUnit>& aOut) {
  if (aEntry.mType != ShortType || aEntry.mCount != 1) {
    return false;
  }
  switch (aEntry.ValueAsUInt16(mByteOrder)) {
    case 2:
      aOut = Some(ResolutionUnit::Dpi);
      break;
    case 3:
      aOut = Some(ResolutionUnit::Dpcm);
      break;
    default:
      return false;
  }
  return true;
}

// The refs fit inline and are read straight from the entry; no type check is
// needed, since mValue is four bytes whatever the type field says. The
// coordinates do not fit, and go through an offset.
void EXIFParser::ParseGPSEntry(ParsedEXIFData& aData, const EXIFEntry& aEntry) {
  double degrees;
  double metres;
  switch (EXIFGPSTag(aEntry.mTag)) {
    case EXIFGPSTag::LatitudeRef:
      aData.gpsLatitudeRef = char(aEntry.mValue[0]);
      break;
    case EXIFGPSTag::Latitude:
      if (ParseGPSCoordinate(aEntry, 90, degrees)) {
        aData.gpsLatitude = Some(degrees);
      }
      break;
    case EXIFGPSTag::LongitudeRef:
      aData.gpsLongitudeRef = char(aEntry.mValue[0]);
      break;
    case EXIFGPSTag::Longitude:
      if (ParseGPSCoordinate(aEntry, 180, degrees)) {
        aData.gpsLongitude = Some(degrees);
      }
      break;
    case EXIFGPSTag::AltitudeRef:
      aData.gpsAltitudeRef = aEntry.mValue[0];
      break;
    case EXIFGPSTag::Altitude:
      if (ParseGPSAltitude(aEntry, metres)) {
        aData.gpsAltitude = Some(metres);
      }
      break;
    default:
      break;
  }
}

bool EXIFParser::ParseGPSCoordinate(const EXIFEntry& aEntry,
                                    uint32_t aMaxDegrees, double& aOutDegrees) {
  // Section 4.6.7 (Section 4.6.6 in EXIF v2.3): a coordinate is three
  // RATIONALs, giving degrees, minutes and seconds.
  if (aEntry.mType != RationalType || aEntry.mCount != 3) {
    return false;
  }

  // Three RATIONALs are 24 bytes, far too big for the entry's 4-byte value
  // field, so that field holds an offset to them instead.
  CheckedUint32 base =
      CheckedUint32(aEntry.ValueAsUInt32(mByteOrder)) + TIFFHeaderStart();
  if (!base.isValid()) {
    return false;
  }

  ScopedJump jumpToValue(*this, base.value());

  // Combine the three components into decimal degrees: degrees + minutes / 60 +
  // seconds / 3600.
  constexpr double kComponentWeights[3] = {1.0, 1.0 / 60.0, 1.0 / 3600.0};
  double degrees = 0.0;
  for (int i = 0; i < 3; ++i) {
    uint32_t numerator;
    uint32_t denominator;
    if (!ReadUInt32(numerator) || !ReadUInt32(denominator)) {
      return false;
    }
    if (denominator == 0) {
      return false;
    }

    // Three well-formed rationals are not on their own a position: an entry
    // whose value offset is wrong lands on some other part of the block, and
    // arbitrary bytes divide into plausible-looking numbers. Requiring each
    // component to be in range is what separates a coordinate from a
    // coincidence. Minutes and seconds are compared as values rather than
    // integers because encoders routinely put the fraction in one of them, as
    // in 3900/100 seconds or 305/10 minutes.
    const double value = double(numerator) / double(denominator);
    const bool inRange = i == 0 ? value <= double(aMaxDegrees) : value < 60.0;
    if (!inRange) {
      return false;
    }

    degrees += value * kComponentWeights[i];
  }

  aOutDegrees = degrees;
  return true;
}

bool EXIFParser::ParseGPSAltitude(const EXIFEntry& aEntry, double& aOutMetres) {
  // Section 4.6.7 (Section 4.6.6 in EXIF v2.3): GPSAltitude is a single
  // RATIONAL, in metres.
  if (aEntry.mType != RationalType || aEntry.mCount != 1) {
    return false;
  }
  float metres;
  if (!ReadRationalAt(aEntry.ValueAsUInt32(mByteOrder), metres)) {
    return false;
  }
  aOutMetres = double(metres);
  return true;
}

bool EXIFParser::ParseOrientation(const EXIFEntry& aEntry, Orientation& aOut) {
  // Sanity check the type and count.
  if (aEntry.mType != ShortType || aEntry.mCount != 1) {
    return false;
  }

  switch (aEntry.ValueAsUInt16(mByteOrder)) {
    case 1:
      aOut = Orientation(Angle::D0, Flip::Unflipped);
      break;
    case 2:
      aOut = Orientation(Angle::D0, Flip::Horizontal);
      break;
    case 3:
      aOut = Orientation(Angle::D180, Flip::Unflipped);
      break;
    case 4:
      aOut = Orientation(Angle::D180, Flip::Horizontal);
      break;
    case 5:
      aOut = Orientation(Angle::D90, Flip::Horizontal);
      break;
    case 6:
      aOut = Orientation(Angle::D90, Flip::Unflipped);
      break;
    case 7:
      aOut = Orientation(Angle::D270, Flip::Horizontal);
      break;
    case 8:
      aOut = Orientation(Angle::D270, Flip::Unflipped);
      break;
    default:
      return false;
  }

  return true;
}

bool EXIFParser::Initialize(const uint8_t* aData, const uint32_t aLength,
                            const uint32_t aMaxLength) {
  if (aData == nullptr) {
    return false;
  }

  if (aLength > aMaxLength) {
    return false;
  }

  mStart = mCurrent = aData;
  mLength = mRemainingLength = aLength;
  mByteOrder = ByteOrder::Unknown;
  return true;
}

void EXIFParser::Advance(const uint32_t aDistance) {
  if (mRemainingLength >= aDistance) {
    mCurrent += aDistance;
    mRemainingLength -= aDistance;
  } else {
    mCurrent = mStart;
    mRemainingLength = 0;
  }
}

void EXIFParser::JumpTo(const uint32_t aOffset) {
  if (mLength >= aOffset) {
    mCurrent = mStart + aOffset;
    mRemainingLength = mLength - aOffset;
  } else {
    mCurrent = mStart;
    mRemainingLength = 0;
  }
}

bool EXIFParser::MatchString(const char* aString, const uint32_t aLength) {
  if (mRemainingLength < aLength) {
    return false;
  }

  for (uint32_t i = 0; i < aLength; ++i) {
    if (mCurrent[i] != aString[i]) {
      return false;
    }
  }

  Advance(aLength);
  return true;
}

bool EXIFParser::ReadUInt16(uint16_t& aValue) {
  if (mRemainingLength < 2) {
    return false;
  }

  bool matched = true;
  switch (mByteOrder) {
    case ByteOrder::LittleEndian:
      aValue = LittleEndian::readUint16(mCurrent);
      break;
    case ByteOrder::BigEndian:
      aValue = BigEndian::readUint16(mCurrent);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Should know the byte order by now");
      matched = false;
  }

  if (matched) {
    Advance(2);
  }

  return matched;
}

bool EXIFParser::ReadUInt32(uint32_t& aValue) {
  if (mRemainingLength < 4) {
    return false;
  }

  bool matched = true;
  switch (mByteOrder) {
    case ByteOrder::LittleEndian:
      aValue = LittleEndian::readUint32(mCurrent);
      break;
    case ByteOrder::BigEndian:
      aValue = BigEndian::readUint32(mCurrent);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Should know the byte order by now");
      matched = false;
  }

  if (matched) {
    Advance(4);
  }

  return matched;
}

}  // namespace mozilla::image
