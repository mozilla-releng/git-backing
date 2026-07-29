/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/image/exif_gps_ffi.h"
#include "nsTArray.h"

// The scanner itself is covered by the crate's own unit tests, which can reach
// far more of it than this can. What these cover is the boundary those tests
// cannot: that the generated header describes the same struct layout and enum
// widths the Rust side writes, so a value does not arrive here reinterpreted.
// A mismatch would be silent -- the wrong enumerator, not a crash.

using namespace mozilla::image;

namespace {

void AppendU16(nsTArray<uint8_t>& aOut, uint16_t aValue) {
  aOut.AppendElement(uint8_t(aValue & 0xFF));
  aOut.AppendElement(uint8_t(aValue >> 8));
}

void AppendU32(nsTArray<uint8_t>& aOut, uint32_t aValue) {
  for (int i = 0; i < 4; ++i) {
    aOut.AppendElement(uint8_t((aValue >> (8 * i)) & 0xFF));
  }
}

// A little-endian TIFF block whose IFD0 points at a GPS attribute IFD carrying
// 51 deg 30 min 0 sec by 0 deg 7 min 39 sec. Laid out by hand: header at 0,
// IFD0 at 8, the GPS IFD at 26, the two coordinates at 56 and 80.
nsTArray<uint8_t> TIFFBlockWithPosition() {
  nsTArray<uint8_t> b;
  b.AppendElements(reinterpret_cast<const uint8_t*>("II\x2A\x00"), 4);
  AppendU32(b, 8);

  AppendU16(b, 1);
  AppendU16(b, 0x8825);  // GPSInfoIFDPointer
  AppendU16(b, 4);       // LONG
  AppendU32(b, 1);
  AppendU32(b, 26);
  AppendU32(b, 0);

  AppendU16(b, 2);
  AppendU16(b, 0x0002);  // GPSLatitude
  AppendU16(b, 5);       // RATIONAL
  AppendU32(b, 3);
  AppendU32(b, 56);
  AppendU16(b, 0x0004);  // GPSLongitude
  AppendU16(b, 5);
  AppendU32(b, 3);
  AppendU32(b, 80);
  AppendU32(b, 0);

  const uint32_t kRationals[12] = {51, 1, 30, 1, 0, 1, 0, 1, 7, 1, 39, 1};
  for (uint32_t value : kRationals) {
    AppendU32(b, value);
  }
  return b;
}

// SOI, an optional Exif APP1, then SOS.
nsTArray<uint8_t> JPEGWith(const nsTArray<uint8_t>* aBlock) {
  nsTArray<uint8_t> jpeg;
  jpeg.AppendElement(0xFF);
  jpeg.AppendElement(0xD8);

  if (aBlock) {
    const uint16_t length = uint16_t(aBlock->Length() + 6 + 2);
    jpeg.AppendElement(0xFF);
    jpeg.AppendElement(0xE1);
    jpeg.AppendElement(uint8_t(length >> 8));
    jpeg.AppendElement(uint8_t(length & 0xFF));
    jpeg.AppendElements(reinterpret_cast<const uint8_t*>("Exif\0\0"), 6);
    jpeg.AppendElements(*aBlock);
  }

  const uint8_t sos[] = {0xFF, 0xDA, 0x00, 0x02};
  jpeg.AppendElements(sos, sizeof(sos));
  return jpeg;
}

EXIFScanResult ScanWhole(const nsTArray<uint8_t>& aBytes) {
  return exif_gps_scan(aBytes.Elements(), aBytes.Length(), aBytes.Length());
}

}  // namespace

TEST(ImageInputEXIFScan, APositionCrossesTheBoundaryIntact)
{
  nsTArray<uint8_t> block = TIFFBlockWithPosition();
  EXIFScanResult result = ScanWhole(JPEGWith(&block));

  EXPECT_EQ(result.container, EXIFContainer::JPEG);
  EXPECT_EQ(result.outcome, EXIFScanOutcome::Complete);
  EXPECT_EQ(result.gpsPresence, EXIFGPSPresence::Position);
}

TEST(ImageInputEXIFScan, AFileWithNoEXIFIsAConfidentNo)
{
  EXIFScanResult result = ScanWhole(JPEGWith(nullptr));

  EXPECT_EQ(result.container, EXIFContainer::JPEG);
  EXPECT_EQ(result.outcome, EXIFScanOutcome::NoEXIF);
  EXPECT_EQ(result.gpsPresence, EXIFGPSPresence::None);
}

TEST(ImageInputEXIFScan, AnUnwalkedContainerIsCountedNotScanned)
{
  nsTArray<uint8_t> heic;
  // Split so that the "f" of "ftyp" is not swallowed into the \x18 escape.
  heic.AppendElements(
      reinterpret_cast<const uint8_t*>("\x00\x00\x00\x18"
                                       "ftypheic"),
      12);
  while (heic.Length() < 32) {
    heic.AppendElement(0);
  }

  EXIFScanResult result = ScanWhole(heic);
  EXPECT_EQ(result.container, EXIFContainer::HEIF);
  EXPECT_EQ(result.outcome, EXIFScanOutcome::UnsupportedContainer);
}

TEST(ImageInputEXIFScan, AnEmptyBufferIsSafeAndSaysNothing)
{
  // A null pointer with a zero length has to be accepted: slice::from_raw_parts
  // is undefined on null even for an empty slice, so the Rust side has to
  // short-circuit before building one.
  EXIFScanResult result = exif_gps_scan(nullptr, 0, 0);

  EXPECT_EQ(result.container, EXIFContainer::Unknown);
  EXPECT_EQ(result.outcome, EXIFScanOutcome::UnsupportedContainer);
  EXPECT_EQ(result.gpsPresence, EXIFGPSPresence::None);
}
