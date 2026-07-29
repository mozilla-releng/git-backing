/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <initializer_list>
#include <utility>

#include "gtest/gtest.h"
#include "mozilla/image/EXIF.h"
#include "nsTArray.h"

using namespace mozilla;
using namespace mozilla::image;

namespace {

// Builds EXIF blocks a byte at a time, bottom up.
//
// Every offset inside a TIFF block is relative to the start of the TIFF header,
// and an IFD has to know the offset of anything it points at. Rather than write
// placeholders and patch them, append the thing being pointed at first and feed
// the offset it returns into the entry that references it:
//
//   uint32_t lat = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
//   uint32_t gps = builder.AddIFD({{0x0002, RationalType, 3, lat}});
//   builder.SetIFD0(builder.AddIFD({{0x8825, LongType, 1, gps}}));
//
// The one offset that cannot work that way is IFD0's own, since it lives in the
// header ahead of everything else, so SetIFD0 patches it at the end.
class TIFFBuilder {
 public:
  // EXIF type codes. See EXIF Section 4.6.2.
  static constexpr uint16_t kByte = 1;
  static constexpr uint16_t kASCII = 2;
  static constexpr uint16_t kShort = 3;
  static constexpr uint16_t kLong = 4;
  static constexpr uint16_t kRational = 5;
  static constexpr uint16_t kUndefined = 7;

  struct Entry {
    uint16_t mTag;
    uint16_t mType;
    uint32_t mCount;
    // The value itself when it fits in four bytes, otherwise the offset of the
    // value, as returned by one of the Add* methods.
    uint32_t mValue;
  };

  explicit TIFFBuilder(ByteOrder aByteOrder, bool aWithExifIdCode = false)
      : mByteOrder(aByteOrder) {
    if (aWithExifIdCode) {
      AppendRaw("Exif\0\0", 6);
    }

    mTIFFHeaderStart = mData.Length();
    if (aByteOrder == ByteOrder::BigEndian) {
      AppendRaw("MM\0*", 4);
    } else {
      AppendRaw("II*\0", 4);
    }
    // Patched by SetIFD0().
    AppendUInt32(0);
  }

  uint32_t AddIFD(std::initializer_list<Entry> aEntries) {
    return AddIFDWithCount(aEntries, uint16_t(aEntries.size()));
  }

  // Writes aDeclaredCount as the entry count regardless of how many entries
  // follow, so that a walk can be pointed at more entries than exist.
  uint32_t AddIFDWithCount(std::initializer_list<Entry> aEntries,
                           uint16_t aDeclaredCount) {
    uint32_t offset = CurrentOffset();
    AppendUInt16(aDeclaredCount);
    for (const Entry& entry : aEntries) {
      AppendUInt16(entry.mTag);
      AppendUInt16(entry.mType);
      AppendUInt32(entry.mCount);
      AppendValueField(entry);
    }
    // No next IFD.
    AppendUInt32(0);
    return offset;
  }

  uint32_t AddRepeatedIFD(uint16_t aTag, uint16_t aType, uint32_t aValue,
                          uint16_t aCount) {
    uint32_t offset = CurrentOffset();
    AppendUInt16(aCount);
    for (uint16_t i = 0; i < aCount; ++i) {
      AppendUInt16(aTag);
      AppendUInt16(aType);
      AppendUInt32(1);
      AppendUInt32(aValue);
    }
    AppendUInt32(0);
    return offset;
  }

  uint32_t AddRationals(
      std::initializer_list<std::pair<uint32_t, uint32_t>> aRationals) {
    uint32_t offset = CurrentOffset();
    for (const auto& rational : aRationals) {
      AppendUInt32(rational.first);
      AppendUInt32(rational.second);
    }
    return offset;
  }

  void SetIFD0(uint32_t aOffset) {
    WriteUInt32At(mTIFFHeaderStart + 4, aOffset);
  }

  const nsTArray<uint8_t>& Data() const { return mData; }
  uint32_t CurrentOffset() const {
    return uint32_t(mData.Length() - mTIFFHeaderStart);
  }

 private:
  void AppendRaw(const char* aBytes, size_t aLength) {
    mData.AppendElements(reinterpret_cast<const uint8_t*>(aBytes), aLength);
  }

  void AppendUInt16(uint16_t aValue) {
    if (mByteOrder == ByteOrder::BigEndian) {
      mData.AppendElement(uint8_t(aValue >> 8));
      mData.AppendElement(uint8_t(aValue));
    } else {
      mData.AppendElement(uint8_t(aValue));
      mData.AppendElement(uint8_t(aValue >> 8));
    }
  }

  void AppendUInt32(uint32_t aValue) {
    if (mByteOrder == ByteOrder::BigEndian) {
      mData.AppendElement(uint8_t(aValue >> 24));
      mData.AppendElement(uint8_t(aValue >> 16));
      mData.AppendElement(uint8_t(aValue >> 8));
      mData.AppendElement(uint8_t(aValue));
    } else {
      mData.AppendElement(uint8_t(aValue));
      mData.AppendElement(uint8_t(aValue >> 8));
      mData.AppendElement(uint8_t(aValue >> 16));
      mData.AppendElement(uint8_t(aValue >> 24));
    }
  }

  void AppendValueField(const Entry& aEntry) {
    // A value shorter than the four-byte field is left-aligned within it, which
    // for a SHORT means two value bytes followed by two of padding. Writing it
    // as a 32-bit integer would put the padding first on a big-endian build.
    if (aEntry.mType == kShort && aEntry.mCount == 1) {
      AppendUInt16(uint16_t(aEntry.mValue));
      AppendUInt16(0);
      return;
    }
    AppendUInt32(aEntry.mValue);
  }

  void WriteUInt32At(size_t aPosition, uint32_t aValue) {
    if (mByteOrder == ByteOrder::BigEndian) {
      mData[aPosition + 0] = uint8_t(aValue >> 24);
      mData[aPosition + 1] = uint8_t(aValue >> 16);
      mData[aPosition + 2] = uint8_t(aValue >> 8);
      mData[aPosition + 3] = uint8_t(aValue);
    } else {
      mData[aPosition + 0] = uint8_t(aValue);
      mData[aPosition + 1] = uint8_t(aValue >> 8);
      mData[aPosition + 2] = uint8_t(aValue >> 16);
      mData[aPosition + 3] = uint8_t(aValue >> 24);
    }
  }

  nsTArray<uint8_t> mData;
  ByteOrder mByteOrder;
  size_t mTIFFHeaderStart = 0;
};

EXIFData ParseAll(const nsTArray<uint8_t>& aData,
                  bool aExpectExifIdCode = false,
                  uint32_t aMaxLength = kMaxEXIFLength) {
  return EXIFParser::Parse(aExpectExifIdCode, aData.Elements(),
                           uint32_t(aData.Length()), gfx::IntSize(100, 50),
                           aMaxLength, EXIFParseTarget::All);
}

EXIFGPSPresence ParseGPS(const nsTArray<uint8_t>& aData) {
  return ParseAll(aData).gpsPresence;
}

// A file whose GPS IFD holds a usable position: 51 degrees 30 minutes north,
// 0 degrees 7 minutes 39 seconds west.
nsTArray<uint8_t> BuildWithPosition(ByteOrder aByteOrder) {
  TIFFBuilder builder(aByteOrder);
  uint32_t latitude = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 3, latitude},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({
      {0x0112, TIFFBuilder::kShort, 1, 6},
      {0x8825, TIFFBuilder::kLong, 1, gps},
  }));
  return builder.Data().Clone();
}

// The same position, but with the reference tags a real camera writes and an
// altitude. The refs and the altitude ref are 2-byte and 1-byte values stored
// inline in the entry, which for a little-endian block AppendValueField writes
// left-aligned, i.e. exactly where the parser reads them. Latitude 51 30 0,
// longitude 0 7 39, altitude 100 m, with the signs taken from the arguments.
nsTArray<uint8_t> BuildWithSignedPosition(char aLatRef, char aLonRef,
                                          uint8_t aAltRef) {
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t altitude = builder.AddRationals({{100, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0001, TIFFBuilder::kASCII, 2, uint32_t(uint8_t(aLatRef))},
      {0x0002, TIFFBuilder::kRational, 3, latitude},
      {0x0003, TIFFBuilder::kASCII, 2, uint32_t(uint8_t(aLonRef))},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
      {0x0005, TIFFBuilder::kByte, 1, aAltRef},
      {0x0006, TIFFBuilder::kRational, 1, altitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  return builder.Data().Clone();
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// The headline behaviour: is there a position, in either byte order.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, GPSPositionLittleEndian)
{
  EXPECT_EQ(EXIFGPSPresence::Position,
            ParseGPS(BuildWithPosition(ByteOrder::LittleEndian)));
}

TEST(EXIFParser, GPSPositionBigEndian)
{
  EXPECT_EQ(EXIFGPSPresence::Position,
            ParseGPS(BuildWithPosition(ByteOrder::BigEndian)));
}

TEST(EXIFParser, GPSPositionDoesNotDisturbOrientation)
{
  EXIFData data = ParseAll(BuildWithPosition(ByteOrder::LittleEndian));
  EXPECT_EQ(EXIFGPSPresence::Position, data.gpsPresence);
  EXPECT_EQ(Orientation(Angle::D90, Flip::Unflipped), data.orientation);
}

TEST(EXIFParser, NoGPSIFDAtAll)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  builder.SetIFD0(builder.AddIFD({{0x0112, TIFFBuilder::kShort, 1, 1}}));
  EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(builder.Data().Clone()));
}

///////////////////////////////////////////////////////////////////////////////
// The actual coordinates.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, DecodesCoordinatesInDecimalDegrees)
{
  EXIFData data = ParseAll(BuildWithSignedPosition('N', 'W', 0));
  ASSERT_EQ(EXIFGPSPresence::Position, data.gpsPresence);
  ASSERT_TRUE(data.gpsCoordinates.isSome());
  // 51 deg 30 min = 51.5; 0 deg 7 min 39 sec = 0.1275, west so negative.
  EXPECT_NEAR(51.5, data.gpsCoordinates->latitudeDegrees, 1e-9);
  EXPECT_NEAR(-0.1275, data.gpsCoordinates->longitudeDegrees, 1e-9);
  ASSERT_TRUE(data.gpsCoordinates->altitudeMeters.isSome());
  EXPECT_NEAR(100.0, *data.gpsCoordinates->altitudeMeters, 1e-9);
}

// The common case: a position with no GPSAltitude at all is still a position,
// and the altitude stays absent rather than defaulting to zero.
TEST(EXIFParser, PositionWithoutAltitude)
{
  EXIFData data = ParseAll(BuildWithPosition(ByteOrder::LittleEndian));
  ASSERT_TRUE(data.gpsCoordinates.isSome());
  EXPECT_TRUE(data.gpsCoordinates->altitudeMeters.isNothing());
}

// The magnitude comes from the coordinate; the sign comes from the reference
// tag. S and W are the negative hemispheres, and altitude ref 1 is below the
// ellipsoid.
TEST(EXIFParser, CoordinateSignsFollowTheReferenceTags)
{
  EXIFData data = ParseAll(BuildWithSignedPosition('S', 'E', 1));
  ASSERT_TRUE(data.gpsCoordinates.isSome());
  EXPECT_NEAR(-51.5, data.gpsCoordinates->latitudeDegrees, 1e-9);
  EXPECT_NEAR(0.1275, data.gpsCoordinates->longitudeDegrees, 1e-9);
  ASSERT_TRUE(data.gpsCoordinates->altitudeMeters.isSome());
  EXPECT_NEAR(-100.0, *data.gpsCoordinates->altitudeMeters, 1e-9);
}

// EXIF v3.0 Section 4.6.7.1.6 redefined refs 0 and 1 as heights above and below
// the ellipsoid, and added 2 and 3 for the same thing against sea level. The
// odd ones are the negative ones.
TEST(EXIFParser, SeaLevelAltitudeReferences)
{
  EXIFData above = ParseAll(BuildWithSignedPosition('N', 'E', 2));
  ASSERT_TRUE(above.gpsCoordinates.isSome());
  ASSERT_TRUE(above.gpsCoordinates->altitudeMeters.isSome());
  EXPECT_NEAR(100.0, *above.gpsCoordinates->altitudeMeters, 1e-9);

  EXIFData below = ParseAll(BuildWithSignedPosition('N', 'E', 3));
  ASSERT_TRUE(below.gpsCoordinates.isSome());
  ASSERT_TRUE(below.gpsCoordinates->altitudeMeters.isSome());
  EXPECT_NEAR(-100.0, *below.gpsCoordinates->altitudeMeters, 1e-9);
}

// Coordinates are exposed only alongside a Position, so the two agree: no
// position, no coordinates.
TEST(EXIFParser, NoCoordinatesWithoutAPosition)
{
  EXIFData data = ParseAll(BuildWithSignedPosition('N', 'W', 0));
  EXPECT_TRUE(data.gpsCoordinates.isSome());

  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t gps = builder.AddIFD({{0x0000, TIFFBuilder::kByte, 4, 0x02030000}});
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXIFData stripped = ParseAll(builder.Data().Clone());
  EXPECT_EQ(EXIFGPSPresence::NoPosition, stripped.gpsPresence);
  EXPECT_TRUE(stripped.gpsCoordinates.isNothing());
}

// Image decoders ask only for orientation, so they never pay to decode a
// position, and it never appears in their result.
TEST(EXIFParser, DefaultTargetProducesNoCoordinates)
{
  EXIFData data = EXIFParser::Parse(
      /* aExpectExifIdCode = */ false,
      BuildWithSignedPosition('N', 'W', 0).Elements(),
      uint32_t(BuildWithSignedPosition('N', 'W', 0).Length()),
      gfx::IntSize(100, 50), kMaxEXIFLength);
  EXPECT_TRUE(data.gpsCoordinates.isNothing());
}

///////////////////////////////////////////////////////////////////////////////
// GPS tag numbers are not globally unique. The Interoperability IFD, reached
// from the Exif IFD via tag 0xA005, uses 0x0001 and 0x0002 for
// InteroperabilityIndex and InteroperabilityVersion, and it is present in the
// large majority of camera JPEGs. A walk that dispatched on tag number without
// tracking which IFD it was in would report a position for almost every photo
// ever taken.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, InteroperabilityIFDIsNotMistakenForGPS)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t interop = builder.AddIFD({
      // InteroperabilityIndex, "R98".
      {0x0001, TIFFBuilder::kASCII, 4, 0x00383952},
      // InteroperabilityVersion.
      {0x0002, TIFFBuilder::kUndefined, 4, 0x30303130},
  });
  uint32_t exif = builder.AddIFD({
      {0xA005, TIFFBuilder::kLong, 1, interop},
  });
  builder.SetIFD0(builder.AddIFD({
      {0x0112, TIFFBuilder::kShort, 1, 1},
      {0x8769, TIFFBuilder::kLong, 1, exif},
  }));

  EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(builder.Data().Clone()));
}

///////////////////////////////////////////////////////////////////////////////
// A GPS IFD that exists but says nothing about where the photo was taken. This
// is what a partially stripped file looks like, and it is worth telling apart
// from a file that never had a position.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, EmptyGPSIFDIsNoPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t gps = builder.AddIFD({});
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

TEST(EXIFParser, GPSVersionOnlyIsNoPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  // GPSVersionID. Its value bytes are never read, so their order is irrelevant.
  uint32_t gps = builder.AddIFD({{0x0000, TIFFBuilder::kByte, 4, 0}});
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

TEST(EXIFParser, LatitudeWithoutLongitudeIsNoPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
  uint32_t gps =
      builder.AddIFD({{0x0002, TIFFBuilder::kRational, 3, latitude}});
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

// Reported as its own classification rather than folded into NoPosition: the
// file does carry coordinates, and what that is worth is the caller's call.
TEST(EXIFParser, AllZeroCoordinatesAreZeroPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t zeroes = builder.AddRationals({{0, 1}, {0, 1}, {0, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 3, zeroes},
      {0x0004, TIFFBuilder::kRational, 3, zeroes},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXIFData data = ParseAll(builder.Data().Clone());
  EXPECT_EQ(EXIFGPSPresence::ZeroPosition, data.gpsPresence);
  EXPECT_TRUE(data.gpsCoordinates.isNothing());
}

// Only latitude being zero is ordinary: that is the equator, not a blanked
// coordinate. Discounting it would throw away real positions.
TEST(EXIFParser, ZeroLatitudeWithRealLongitudeIsAPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{0, 1}, {0, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{78, 1}, {30, 1}, {0, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 3, latitude},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::Position, ParseGPS(builder.Data().Clone()));
}

TEST(EXIFParser, ZeroDenominatorIsNoPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{51, 0}, {30, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 3, latitude},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

// Destination coordinates say where the photographer was heading, not where the
// photo was taken.
TEST(EXIFParser, DestinationCoordinatesAreNotAPosition)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0014, TIFFBuilder::kRational, 3, latitude},
      {0x0016, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

///////////////////////////////////////////////////////////////////////////////
// Malformed coordinate entries.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, CoordinateWithWrongTypeIsRejected)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kLong, 3, 51},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

TEST(EXIFParser, CoordinateWithWrongCountIsRejected)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{51, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 1, latitude},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

// A count large enough that multiplying it by the type size would overflow 32
// bits. The count check rejects it before any arithmetic happens.
TEST(EXIFParser, CoordinateWithOverflowingCountIsRejected)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 0xFFFFFFFFu, 8},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
  EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()));
}

TEST(EXIFParser, CoordinateOffsetOutOfRangeIsRejected)
{
  for (uint32_t offset : {0x7FFFFFFFu, 0xFFFFFFFFu, 0u}) {
    TIFFBuilder builder(ByteOrder::LittleEndian);
    uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
    uint32_t gps = builder.AddIFD({
        {0x0002, TIFFBuilder::kRational, 3, offset},
        {0x0004, TIFFBuilder::kRational, 3, longitude},
    });
    builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));
    EXPECT_EQ(EXIFGPSPresence::NoPosition, ParseGPS(builder.Data().Clone()))
        << "coordinate offset " << offset;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Pointers that lie.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, GPSPointerOutOfRangeIsIgnored)
{
  for (uint32_t offset : {0x7FFFFFFFu, 0xFFFFFFFFu}) {
    TIFFBuilder builder(ByteOrder::LittleEndian);
    builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, offset}}));
    EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(builder.Data().Clone()))
        << "GPS pointer " << offset;
  }
}

// IFD0's own offset is recorded before the walk starts, so a pointer back to it
// is refused rather than followed a second time.
TEST(EXIFParser, GPSPointerBackToIFD0Terminates)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t ifd0 = builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, 8}});
  builder.SetIFD0(ifd0);
  EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(builder.Data().Clone()));
}

TEST(EXIFParser, ExifPointerCycleTerminates)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t ifd0 = builder.AddIFD({{0x8769, TIFFBuilder::kLong, 1, 8}});
  builder.SetIFD0(ifd0);
  EXIFData data = ParseAll(builder.Data().Clone());
  EXPECT_EQ(EXIFGPSPresence::None, data.gpsPresence);
}

// Bounding recursion depth alone does not bound the work: a single IFD may
// repeat a pointer tag thousands of times, and if each points back at that same
// IFD the walk fans out combinatorially. Visiting each IFD at most once is what
// stops it. If this test hangs, that protection has been lost.
TEST(EXIFParser, PointerFanOutTerminates)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t ifd0 = builder.AddRepeatedIFD(0x8769, TIFFBuilder::kLong, 8, 5000);
  builder.SetIFD0(ifd0);
  EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(builder.Data().Clone()));
}

///////////////////////////////////////////////////////////////////////////////
// Structurally broken input.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, DeclaredEntryCountBeyondTheBufferTerminates)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  builder.SetIFD0(
      builder.AddIFDWithCount({{0x0112, TIFFBuilder::kShort, 1, 1}}, 0xFFFF));
  EXIFData data = ParseAll(builder.Data().Clone());
  // The one real entry is still read before the buffer runs out.
  EXPECT_EQ(Orientation(Angle::D0, Flip::Unflipped), data.orientation);
}

TEST(EXIFParser, GarbageIsRejected)
{
  nsTArray<uint8_t> data;
  data.AppendElements(reinterpret_cast<const uint8_t*>("not an exif block"),
                      17);
  EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(data));
}

TEST(EXIFParser, EmptyInputIsRejected)
{
  nsTArray<uint8_t> empty;
  EXPECT_EQ(EXIFGPSPresence::None, ParseGPS(empty));
}

TEST(EXIFParser, IFD0OffsetPastTheBufferIsRejected)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  builder.AddIFD({{0x0112, TIFFBuilder::kShort, 1, 6}});
  builder.SetIFD0(0xFFFFFF00u);
  EXIFData data = ParseAll(builder.Data().Clone());
  EXPECT_EQ(Orientation(), data.orientation);
}

// Truncating at every possible length must never crash, and must never produce
// a position that the whole buffer does not.
TEST(EXIFParser, TruncationSweep)
{
  nsTArray<uint8_t> full = BuildWithPosition(ByteOrder::LittleEndian);
  ASSERT_EQ(EXIFGPSPresence::Position, ParseGPS(full));

  for (size_t length = 0; length <= full.Length(); ++length) {
    nsTArray<uint8_t> truncated;
    truncated.AppendElements(full.Elements(), length);
    EXIFGPSPresence presence = ParseGPS(truncated);
    EXPECT_TRUE(presence == EXIFGPSPresence::None ||
                presence == EXIFGPSPresence::NoPosition ||
                presence == EXIFGPSPresence::ZeroPosition ||
                presence == EXIFGPSPresence::Position)
        << "truncated to " << length;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Regressions guarding the entry-reading refactor.
///////////////////////////////////////////////////////////////////////////////

// A tag handler that rejects its entry used to abandon the whole IFD, so a
// malformed entry silently discarded every tag after it. Reading the value
// field in one place means the walk stays in step and later entries survive.
TEST(EXIFParser, MalformedEntryDoesNotDiscardLaterEntries)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  builder.SetIFD0(builder.AddIFD({
      // ResolutionUnit with a value outside the two the spec defines.
      {0x0128, TIFFBuilder::kShort, 1, 99},
      {0x0112, TIFFBuilder::kShort, 1, 3},
  }));

  EXIFData data = ParseAll(builder.Data().Clone());
  EXPECT_EQ(Orientation(Angle::D180, Flip::Unflipped), data.orientation);
}

TEST(EXIFParser, OrientationRoundTripsInBothByteOrders)
{
  for (ByteOrder order : {ByteOrder::LittleEndian, ByteOrder::BigEndian}) {
    TIFFBuilder builder(order);
    builder.SetIFD0(builder.AddIFD({{0x0112, TIFFBuilder::kShort, 1, 8}}));
    EXIFData data = ParseAll(builder.Data().Clone());
    EXPECT_EQ(Orientation(Angle::D270, Flip::Unflipped), data.orientation);
  }
}

TEST(EXIFParser, ExifIdCodeIsRequiredWhenExpected)
{
  nsTArray<uint8_t> withCode =
      TIFFBuilder(ByteOrder::LittleEndian, /* aWithExifIdCode = */ true)
          .Data()
          .Clone();
  nsTArray<uint8_t> withoutCode = BuildWithPosition(ByteOrder::LittleEndian);

  // Expecting the identifier code when it is absent finds nothing.
  EXPECT_EQ(EXIFGPSPresence::None,
            ParseAll(withoutCode, /* aExpectExifIdCode = */ true).gpsPresence);
  EXPECT_EQ(EXIFGPSPresence::None,
            ParseAll(withCode, /* aExpectExifIdCode = */ false).gpsPresence);
}

// The identifier code shifts every offset in the block by six bytes. Getting
// that wrong would send the GPS pointer to the wrong place.
TEST(EXIFParser, OffsetsAreRelativeToTheTIFFHeaderNotTheBlock)
{
  TIFFBuilder builder(ByteOrder::LittleEndian, /* aWithExifIdCode = */ true);
  uint32_t latitude = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 3, latitude},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({{0x8825, TIFFBuilder::kLong, 1, gps}}));

  EXPECT_EQ(EXIFGPSPresence::Position,
            ParseAll(builder.Data().Clone(), /* aExpectExifIdCode = */ true)
                .gpsPresence);
}

///////////////////////////////////////////////////////////////////////////////
// The size cap, and the parse target.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFParser, BlocksLongerThanTheCapAreRejected)
{
  nsTArray<uint8_t> data = BuildWithPosition(ByteOrder::LittleEndian);
  ASSERT_LT(data.Length(), size_t(1024));

  EXPECT_EQ(EXIFGPSPresence::Position,
            ParseAll(data, false, kMaxEXIFLength).gpsPresence);
  EXPECT_EQ(EXIFGPSPresence::Position,
            ParseAll(data, false, kMaxJPEGEXIFLength).gpsPresence);
  EXPECT_EQ(EXIFGPSPresence::None,
            ParseAll(data, false, uint32_t(data.Length() - 1)).gpsPresence);
}

// The JPEG cap is the largest APP1 payload there can be, identifier code
// included, so a block filled to exactly that size has to be accepted. It was
// once six bytes too small, which quietly dropped orientation and resolution
// from a JPEG whose APP1 segment was filled to the legal maximum.
TEST(EXIFParser, ABlockFilledToTheJPEGCapIsAccepted)
{
  nsTArray<uint8_t> data = BuildWithPosition(ByteOrder::LittleEndian);
  ASSERT_LT(data.Length(), size_t(kMaxJPEGEXIFLength));

  data.SetLength(kMaxJPEGEXIFLength);
  EXPECT_EQ(EXIFGPSPresence::Position,
            ParseAll(data, false, kMaxJPEGEXIFLength).gpsPresence);

  data.SetLength(kMaxJPEGEXIFLength + 1);
  EXPECT_EQ(EXIFGPSPresence::None,
            ParseAll(data, false, kMaxJPEGEXIFLength).gpsPresence);
}

// An eXIf chunk larger than a JPEG APP1 segment is legal in PNG. It used to be
// dropped whole, taking the orientation with it.
TEST(EXIFParser, BlocksLargerThanAJPEGSegmentAreAcceptedWithTheLargerCap)
{
  TIFFBuilder builder(ByteOrder::LittleEndian);
  uint32_t latitude = builder.AddRationals({{51, 1}, {30, 1}, {0, 1}});
  uint32_t longitude = builder.AddRationals({{0, 1}, {7, 1}, {39, 1}});
  uint32_t gps = builder.AddIFD({
      {0x0002, TIFFBuilder::kRational, 3, latitude},
      {0x0004, TIFFBuilder::kRational, 3, longitude},
  });
  builder.SetIFD0(builder.AddIFD({
      {0x0112, TIFFBuilder::kShort, 1, 6},
      {0x8825, TIFFBuilder::kLong, 1, gps},
  }));

  // Pad past what a JPEG APP1 segment could hold.
  nsTArray<uint8_t> data = builder.Data().Clone();
  data.SetLength(kMaxJPEGEXIFLength + 1024);

  EXIFData large = ParseAll(data, false, kMaxEXIFLength);
  EXPECT_EQ(EXIFGPSPresence::Position, large.gpsPresence);
  EXPECT_EQ(Orientation(Angle::D90, Flip::Unflipped), large.orientation);

  EXPECT_EQ(EXIFGPSPresence::None,
            ParseAll(data, false, kMaxJPEGEXIFLength).gpsPresence);
}

// Image decoders ask only for orientation and resolution, and must not pay for
// the GPS walk.
TEST(EXIFParser, DefaultTargetDoesNotLookForGPS)
{
  nsTArray<uint8_t> data = BuildWithPosition(ByteOrder::LittleEndian);

  EXIFData defaultTarget = EXIFParser::Parse(
      /* aExpectExifIdCode = */ false, data.Elements(), uint32_t(data.Length()),
      gfx::IntSize(100, 50), kMaxEXIFLength);

  EXPECT_EQ(EXIFGPSPresence::None, defaultTarget.gpsPresence);
  EXPECT_EQ(Orientation(Angle::D90, Flip::Unflipped),
            defaultTarget.orientation);
}
