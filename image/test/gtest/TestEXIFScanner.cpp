/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <utility>

#include "Common.h"
#include "gtest/gtest.h"
#include "mozilla/Assertions.h"
#include "mozilla/image/EXIFScanner.h"
#include "nsIInputStream.h"
#include "nsStreamUtils.h"
#include "nsTArray.h"

using namespace mozilla;
using namespace mozilla::image;

namespace {

///////////////////////////////////////////////////////////////////////////////
// A minimal EXIF block carrying a position, and one carrying none. These are
// the payloads the container builders below wrap; the parser's own coverage
// lives in TestEXIF.cpp, so all that matters here is that a block arrives
// intact and is recognised.
///////////////////////////////////////////////////////////////////////////////

void AppendUInt16LE(nsTArray<uint8_t>& aOut, uint16_t aValue) {
  aOut.AppendElement(uint8_t(aValue));
  aOut.AppendElement(uint8_t(aValue >> 8));
}

void AppendUInt32LE(nsTArray<uint8_t>& aOut, uint32_t aValue) {
  aOut.AppendElement(uint8_t(aValue));
  aOut.AppendElement(uint8_t(aValue >> 8));
  aOut.AppendElement(uint8_t(aValue >> 16));
  aOut.AppendElement(uint8_t(aValue >> 24));
}

void AppendUInt16BE(nsTArray<uint8_t>& aOut, uint16_t aValue) {
  aOut.AppendElement(uint8_t(aValue >> 8));
  aOut.AppendElement(uint8_t(aValue));
}

void AppendUInt32BE(nsTArray<uint8_t>& aOut, uint32_t aValue) {
  aOut.AppendElement(uint8_t(aValue >> 24));
  aOut.AppendElement(uint8_t(aValue >> 16));
  aOut.AppendElement(uint8_t(aValue >> 8));
  aOut.AppendElement(uint8_t(aValue));
}

void AppendLiteral(nsTArray<uint8_t>& aOut, const char* aBytes,
                   size_t aLength) {
  aOut.AppendElements(reinterpret_cast<const uint8_t*>(aBytes), aLength);
}

// A bare TIFF block, little endian, whose GPS IFD holds a real position.
nsTArray<uint8_t> TIFFBlockWithPosition() {
  nsTArray<uint8_t> data;
  AppendLiteral(data, "II*\0", 4);
  AppendUInt32LE(data, 8);  // IFD0 starts immediately after the header.

  // IFD0: one entry, the GPS IFD pointer.
  AppendUInt16LE(data, 1);
  AppendUInt16LE(data, 0x8825);
  AppendUInt16LE(data, 4);  // LONG
  AppendUInt32LE(data, 1);
  AppendUInt32LE(data, 26);  // Offset of the GPS IFD.
  AppendUInt32LE(data, 0);   // No next IFD.

  // GPS IFD at offset 26: latitude and longitude.
  AppendUInt16LE(data, 2);
  AppendUInt16LE(data, 0x0002);
  AppendUInt16LE(data, 5);  // RATIONAL
  AppendUInt32LE(data, 3);
  AppendUInt32LE(data, 56);  // Offset of the latitude rationals.
  AppendUInt16LE(data, 0x0004);
  AppendUInt16LE(data, 5);
  AppendUInt32LE(data, 3);
  AppendUInt32LE(data, 80);  // Offset of the longitude rationals.
  AppendUInt32LE(data, 0);

  // Latitude at 56, longitude at 80: three rationals each.
  for (uint32_t numerator : {51u, 30u, 0u}) {
    AppendUInt32LE(data, numerator);
    AppendUInt32LE(data, 1);
  }
  for (uint32_t numerator : {0u, 7u, 39u}) {
    AppendUInt32LE(data, numerator);
    AppendUInt32LE(data, 1);
  }

  return data;
}

// A bare TIFF block with an orientation tag and no GPS IFD.
nsTArray<uint8_t> TIFFBlockWithoutPosition() {
  nsTArray<uint8_t> data;
  AppendLiteral(data, "II*\0", 4);
  AppendUInt32LE(data, 8);
  AppendUInt16LE(data, 1);
  AppendUInt16LE(data, 0x0112);
  AppendUInt16LE(data, 3);  // SHORT
  AppendUInt32LE(data, 1);
  AppendUInt16LE(data, 1);
  AppendUInt16LE(data, 0);
  AppendUInt32LE(data, 0);
  return data;
}

///////////////////////////////////////////////////////////////////////////////
// Container builders.
///////////////////////////////////////////////////////////////////////////////

class JPEGBuilder {
 public:
  JPEGBuilder() { AppendLiteral(mData, "\xFF\xD8", 2); }

  // Appends an APPn or other length-carrying segment.
  JPEGBuilder& AddSegment(uint8_t aMarker, const nsTArray<uint8_t>& aPayload) {
    mData.AppendElement(0xFF);
    mData.AppendElement(aMarker);
    AppendUInt16BE(mData, uint16_t(aPayload.Length() + 2));
    mData.AppendElements(aPayload);
    return *this;
  }

  JPEGBuilder& AddExifAPP1(const nsTArray<uint8_t>& aTIFFBlock) {
    nsTArray<uint8_t> payload;
    AppendLiteral(payload, "Exif\0\0", 6);
    payload.AppendElements(aTIFFBlock);
    return AddSegment(0xE1, payload);
  }

  // An EXIF segment whose declared payload extends well past the TIFF block, as
  // one holding a thumbnail would.
  JPEGBuilder& AddPaddedExifAPP1(const nsTArray<uint8_t>& aTIFFBlock,
                                 size_t aPayloadLength) {
    nsTArray<uint8_t> payload;
    AppendLiteral(payload, "Exif\0\0", 6);
    payload.AppendElements(aTIFFBlock);
    MOZ_RELEASE_ASSERT(aPayloadLength >= payload.Length());
    payload.SetLength(aPayloadLength);
    return AddSegment(0xE1, payload);
  }

  JPEGBuilder& AddXMPAPP1() {
    nsTArray<uint8_t> payload;
    AppendLiteral(payload, "http://ns.adobe.com/xap/1.0/\0", 29);
    AppendLiteral(payload, "<x:xmpmeta/>", 12);
    return AddSegment(0xE1, payload);
  }

  JPEGBuilder& AddJFIFAPP0() {
    nsTArray<uint8_t> payload;
    AppendLiteral(payload, "JFIF\0", 5);
    payload.SetLength(14);
    return AddSegment(0xE0, payload);
  }

  JPEGBuilder& AddRawBytes(const char* aBytes, size_t aLength) {
    AppendLiteral(mData, aBytes, aLength);
    return *this;
  }

  // Start of scan, after which no metadata segment can appear.
  JPEGBuilder& AddSOS() {
    AppendLiteral(mData, "\xFF\xDA\x00\x02", 4);
    return *this;
  }

  nsTArray<uint8_t> Finish() { return std::move(mData); }

 private:
  nsTArray<uint8_t> mData;
};

class PNGBuilder {
 public:
  PNGBuilder() { AppendLiteral(mData, "\x89PNG\r\n\x1A\n", 8); }

  PNGBuilder& AddChunk(const char (&aType)[5], const nsTArray<uint8_t>& aData) {
    AppendUInt32BE(mData, uint32_t(aData.Length()));
    AppendLiteral(mData, aType, 4);
    mData.AppendElements(aData);
    // The scanner does not verify CRCs, so a placeholder is enough here; a
    // decoder would reject this file, which is fine because we never decode.
    AppendUInt32BE(mData, 0);
    return *this;
  }

  PNGBuilder& AddIHDR() {
    nsTArray<uint8_t> data;
    data.SetLength(13);
    return AddChunk("IHDR", data);
  }

  PNGBuilder& AddIDAT(size_t aLength) {
    nsTArray<uint8_t> data;
    data.SetLength(aLength);
    return AddChunk("IDAT", data);
  }

  PNGBuilder& AddIEND() {
    nsTArray<uint8_t> empty;
    return AddChunk("IEND", empty);
  }

  nsTArray<uint8_t> Finish() { return std::move(mData); }

 private:
  nsTArray<uint8_t> mData;
};

// Scans a complete file.
EXIFScanResult Scan(const nsTArray<uint8_t>& aData) {
  return ScanEXIF(Span<const uint8_t>(aData.Elements(), aData.Length()),
                  aData.Length());
}

// Scans only the first aPrefixLength bytes of a file of aData.Length() bytes,
// which is what reading a bounded prefix of a real upload looks like.
EXIFScanResult ScanPrefix(const nsTArray<uint8_t>& aData,
                          size_t aPrefixLength) {
  size_t available = std::min(aPrefixLength, aData.Length());
  return ScanEXIF(Span<const uint8_t>(aData.Elements(), available),
                  aData.Length());
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// JPEG.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFScanner, JPEGWithPosition)
{
  EXIFScanResult result = Scan(
      JPEGBuilder().AddExifAPP1(TIFFBlockWithPosition()).AddSOS().Finish());
  EXPECT_EQ(EXIFContainer::JPEG, result.container);
  EXPECT_EQ(EXIFScanOutcome::Complete, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

TEST(EXIFScanner, JPEGWithoutPosition)
{
  EXIFScanResult result = Scan(JPEGBuilder()
                                   .AddJFIFAPP0()
                                   .AddExifAPP1(TIFFBlockWithoutPosition())
                                   .AddSOS()
                                   .Finish());
  EXPECT_EQ(EXIFScanOutcome::Complete, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence);
}

TEST(EXIFScanner, JPEGWithNoEXIFAtAll)
{
  EXIFScanResult result = Scan(JPEGBuilder().AddJFIFAPP0().AddSOS().Finish());
  EXPECT_EQ(EXIFScanOutcome::NoEXIF, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence);
}

// APP1 carries both EXIF and XMP, in whichever order the encoder felt like.
// Taking the first APP1 rather than the first EXIF one loses the metadata.
TEST(EXIFScanner, JPEGFindsEXIFAfterAnXMPAPP1)
{
  EXIFScanResult result = Scan(JPEGBuilder()
                                   .AddXMPAPP1()
                                   .AddExifAPP1(TIFFBlockWithPosition())
                                   .AddSOS()
                                   .Finish());
  EXPECT_EQ(EXIFScanOutcome::Complete, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

TEST(EXIFScanner, JPEGFindsEXIFAmongSeveralAPP1s)
{
  EXIFScanResult result = Scan(JPEGBuilder()
                                   .AddJFIFAPP0()
                                   .AddXMPAPP1()
                                   .AddExifAPP1(TIFFBlockWithPosition())
                                   .AddXMPAPP1()
                                   .AddSOS()
                                   .Finish());
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

// Fill bytes are legal between segments.
TEST(EXIFScanner, JPEGToleratesFillBytes)
{
  EXIFScanResult result = Scan(JPEGBuilder()
                                   .AddRawBytes("\xFF\xFF\xFF", 3)
                                   .AddExifAPP1(TIFFBlockWithPosition())
                                   .AddSOS()
                                   .Finish());
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

// TEM and the restart markers carry no length field.
TEST(EXIFScanner, JPEGToleratesStandaloneMarkers)
{
  EXIFScanResult result = Scan(JPEGBuilder()
                                   .AddRawBytes("\xFF\x01\xFF\xD0", 4)
                                   .AddExifAPP1(TIFFBlockWithPosition())
                                   .AddSOS()
                                   .Finish());
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

// The scanner carries the decoded position out, not just the yes/no. The block
// here omits the reference tags, so the magnitudes are unsigned; the sign logic
// is exercised at the parser level in TestEXIF. 51 deg 30 min, 0 deg 7 min 39
// sec.
TEST(EXIFScanner, ExposesDecodedCoordinates)
{
  EXIFScanResult result = Scan(
      JPEGBuilder().AddExifAPP1(TIFFBlockWithPosition()).AddSOS().Finish());
  ASSERT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
  ASSERT_TRUE(result.gpsCoordinates.isSome());
  EXPECT_NEAR(51.5, result.gpsCoordinates->latitudeDegrees, 1e-9);
  EXPECT_NEAR(0.1275, result.gpsCoordinates->longitudeDegrees, 1e-9);
}

TEST(EXIFScanner, JPEGSegmentLengthBelowTwoIsMalformed)
{
  nsTArray<uint8_t> data;
  AppendLiteral(data, "\xFF\xD8\xFF\xE1\x00\x01", 6);
  EXIFScanResult result = Scan(data);
  EXPECT_EQ(EXIFContainer::JPEG, result.container);
  EXPECT_EQ(EXIFScanOutcome::MalformedContainer, result.outcome);
}

TEST(EXIFScanner, JPEGSegmentRunningPastTheEndOfAWholeFileIsMalformed)
{
  nsTArray<uint8_t> data;
  AppendLiteral(data, "\xFF\xD8\xFF\xE0\xFF\xFF", 6);
  EXIFScanResult result = Scan(data);
  EXPECT_EQ(EXIFScanOutcome::MalformedContainer, result.outcome);
}

// The same shape, but where we simply have not read far enough. This must not
// be reported as a confident "no GPS".
TEST(EXIFScanner, JPEGSegmentBeyondThePrefixIsLocationUnknown)
{
  nsTArray<uint8_t> full = JPEGBuilder()
                               .AddJFIFAPP0()
                               .AddExifAPP1(TIFFBlockWithPosition())
                               .AddSOS()
                               .Finish();
  EXIFScanResult result = ScanPrefix(full, 8);
  EXPECT_EQ(EXIFContainer::JPEG, result.container);
  EXPECT_EQ(EXIFScanOutcome::EXIFLocationUnknown, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence);
}

// A position found in a clipped block is still a position: more bytes cannot
// take it away. The outcome still says the block was clipped, so that a
// negative from the same scan would not be mistaken for a confident one.
TEST(EXIFScanner, JPEGPositionInAClippedBlockIsStillReported)
{
  // An EXIF segment whose declared payload runs far past the coordinates.
  nsTArray<uint8_t> full =
      JPEGBuilder().AddPaddedExifAPP1(TIFFBlockWithPosition(), 8192).Finish();

  EXIFScanResult result = ScanPrefix(full, 256);
  EXPECT_EQ(EXIFScanOutcome::EXIFTruncated, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

TEST(EXIFScanner, JPEGTruncationSweepNeverCrashes)
{
  nsTArray<uint8_t> full = JPEGBuilder()
                               .AddJFIFAPP0()
                               .AddXMPAPP1()
                               .AddExifAPP1(TIFFBlockWithPosition())
                               .AddSOS()
                               .Finish();

  // Reading more of a file may reveal a position, but it must never hide one
  // that a shorter prefix already found. Anything else would mean the answer
  // depends on how much we happened to read.
  bool seenPosition = false;
  for (size_t length = 0; length <= full.Length(); ++length) {
    EXIFScanResult result = ScanPrefix(full, length);
    if (result.gpsPresence == EXIFGPSPresence::Position) {
      seenPosition = true;
    } else {
      EXPECT_FALSE(seenPosition)
          << "a position found in a shorter prefix disappeared at " << length;
    }
  }

  EXPECT_TRUE(seenPosition) << "the whole file should yield a position";
}

///////////////////////////////////////////////////////////////////////////////
// PNG.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFScanner, PNGWithPosition)
{
  EXIFScanResult result = Scan(PNGBuilder()
                                   .AddIHDR()
                                   .AddChunk("eXIf", TIFFBlockWithPosition())
                                   .AddIDAT(64)
                                   .AddIEND()
                                   .Finish());
  EXPECT_EQ(EXIFContainer::PNG, result.container);
  EXPECT_EQ(EXIFScanOutcome::Complete, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

TEST(EXIFScanner, PNGWithoutEXIF)
{
  EXIFScanResult result =
      Scan(PNGBuilder().AddIHDR().AddIDAT(64).AddIEND().Finish());
  EXPECT_EQ(EXIFScanOutcome::NoEXIF, result.outcome);
}

// The eXIf chunk is specified to carry a bare TIFF block, but some encoders
// prepend the JPEG identifier code anyway.
TEST(EXIFScanner, PNGToleratesASpuriousExifIdCode)
{
  nsTArray<uint8_t> payload;
  AppendLiteral(payload, "Exif\0\0", 6);
  payload.AppendElements(TIFFBlockWithPosition());

  EXIFScanResult result =
      Scan(PNGBuilder().AddIHDR().AddChunk("eXIf", payload).AddIEND().Finish());
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

TEST(EXIFScanner, PNGEXIFAfterIDATIsStillFound)
{
  EXIFScanResult result = Scan(PNGBuilder()
                                   .AddIHDR()
                                   .AddIDAT(64)
                                   .AddChunk("eXIf", TIFFBlockWithPosition())
                                   .AddIEND()
                                   .Finish());
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

// ImageMagick writes a hex-encoded copy of the whole JPEG APP1 segment into a
// text chunk. Reporting that as "no EXIF" would be a false negative, so it gets
// its own outcome.
TEST(EXIFScanner, PNGRawProfileIsReportedAsUnsupportedEncoding)
{
  nsTArray<uint8_t> text;
  AppendLiteral(text, "Raw profile type exif\0\nexif\n 34\n0x", 34);

  EXIFScanResult result =
      Scan(PNGBuilder().AddIHDR().AddChunk("tEXt", text).AddIEND().Finish());
  EXPECT_EQ(EXIFScanOutcome::UnsupportedEXIFEncoding, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence);
}

TEST(EXIFScanner, PNGChunkLengthWithTheTopBitSetIsMalformed)
{
  nsTArray<uint8_t> data;
  AppendLiteral(data, "\x89PNG\r\n\x1A\n", 8);
  AppendUInt32BE(data, 0x80000000u);
  AppendLiteral(data, "IHDR", 4);
  EXIFScanResult result = Scan(data);
  EXPECT_EQ(EXIFScanOutcome::MalformedContainer, result.outcome);
}

// A large colour profile can push the eXIf chunk past the bytes we read.
TEST(EXIFScanner, PNGEXIFBeyondThePrefixIsLocationUnknown)
{
  nsTArray<uint8_t> profile;
  profile.SetLength(200 * 1024);

  nsTArray<uint8_t> full = PNGBuilder()
                               .AddIHDR()
                               .AddChunk("iCCP", profile)
                               .AddChunk("eXIf", TIFFBlockWithPosition())
                               .AddIEND()
                               .Finish();

  EXIFScanResult result = ScanPrefix(full, 128 * 1024);
  EXPECT_EQ(EXIFContainer::PNG, result.container);
  EXPECT_EQ(EXIFScanOutcome::EXIFLocationUnknown, result.outcome);

  // With the whole file, the same chunk is found.
  EXPECT_EQ(EXIFGPSPresence::Position, Scan(full).gpsPresence);
}

// A block present in full that begins with the TIFF magic but whose IFD0 offset
// runs off its end does not parse, so it says nothing about GPS. It must be
// reported as malformed rather than counted as a confident "no location": the
// parser returns its no-GPS default on structural failure just as it does for a
// real photo without GPS, and the two must not be conflated.
TEST(EXIFScanner, PNGCompleteButUnparseableEXIFIsMalformedNotNoGPS)
{
  nsTArray<uint8_t> exif;
  AppendLiteral(exif, "II*\0", 4);
  AppendUInt32LE(exif, 8);  // IFD0 at offset 8, but the block ends there.

  EXIFScanResult result =
      Scan(PNGBuilder().AddIHDR().AddChunk("eXIf", exif).AddIEND().Finish());
  EXPECT_EQ(EXIFContainer::PNG, result.container);
  EXPECT_EQ(EXIFScanOutcome::MalformedContainer, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence);
}

// A file we have in full whose eXIf chunk declares more bytes than the file
// holds has inconsistent lengths, which is malformed. It must not land in the
// truncated bucket, which means only "we did not read far enough" and is the
// signal used to decide whether the read prefix is large enough.
TEST(EXIFScanner, PNGWholeFileWithOverrunningEXIFLengthIsMalformed)
{
  nsTArray<uint8_t> data;
  AppendLiteral(data, "\x89PNG\r\n\x1A\n", 8);
  AppendUInt32BE(data, 1000);  // Declared eXIf length...
  AppendLiteral(data, "eXIf", 4);
  data.AppendElements(TIFFBlockWithoutPosition());  // ...but far fewer follow.

  EXIFScanResult result = Scan(data);  // Scan reads the whole file.
  EXPECT_EQ(EXIFContainer::PNG, result.container);
  EXPECT_EQ(EXIFScanOutcome::MalformedContainer, result.outcome);
}

///////////////////////////////////////////////////////////////////////////////
// TIFF.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFScanner, BareTIFFWithPosition)
{
  EXIFScanResult result = Scan(TIFFBlockWithPosition());
  EXPECT_EQ(EXIFContainer::TIFF, result.container);
  EXPECT_EQ(EXIFScanOutcome::Complete, result.outcome);
  EXPECT_EQ(EXIFGPSPresence::Position, result.gpsPresence);
}

// A TIFF we have only part of cannot yield a trustworthy "no".
TEST(EXIFScanner, ClippedTIFFWithoutPositionIsNotAConfidentNo)
{
  nsTArray<uint8_t> full = TIFFBlockWithPosition();
  full.SetLength(full.Length() + 4096);

  EXIFScanResult result = ScanPrefix(full, 16);
  EXPECT_EQ(EXIFContainer::TIFF, result.container);
  EXPECT_EQ(EXIFScanOutcome::EXIFTruncated, result.outcome);
}

TEST(EXIFScanner, BigTIFFIsNotMisreadAsTIFF)
{
  for (const char* magic : {"II\x2B\x00", "MM\x00\x2B"}) {
    nsTArray<uint8_t> data;
    AppendLiteral(data, magic, 4);
    data.SetLength(64);

    EXIFScanResult result = Scan(data);
    EXPECT_EQ(EXIFContainer::BigTIFF, result.container);
    EXPECT_EQ(EXIFScanOutcome::UnsupportedContainer, result.outcome);
    EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Sniffing. Formats we do not walk still have to be identified, so that their
// share of uploads stays visible.
///////////////////////////////////////////////////////////////////////////////

TEST(EXIFScanner, RecognisesContainersItDoesNotWalk)
{
  struct Case {
    const char* mSignature;
    size_t mLength;
    EXIFContainer mExpected;
  };

  nsTArray<uint8_t> webp;
  AppendLiteral(webp, "RIFF\x10\x00\x00\x00WEBPVP8X", 16);

  // The escapes are split from the text that follows because "\x18f" would
  // otherwise be read as one over-long hex escape.
  nsTArray<uint8_t> heic;
  AppendLiteral(heic,
                "\x00\x00\x00\x18"
                "ftypheic",
                12);

  nsTArray<uint8_t> avif;
  AppendLiteral(avif,
                "\x00\x00\x00\x1C"
                "ftypavif",
                12);

  EXPECT_EQ(EXIFContainer::WebP, Scan(webp).container);
  EXPECT_EQ(EXIFContainer::HEIF, Scan(heic).container);
  EXPECT_EQ(EXIFContainer::AVIF, Scan(avif).container);

  for (const nsTArray<uint8_t>* data : {&webp, &heic, &avif}) {
    EXPECT_EQ(EXIFScanOutcome::UnsupportedContainer, Scan(*data).outcome);
  }
}

TEST(EXIFScanner, RecognisesFormatsThatCannotCarryEXIF)
{
  nsTArray<uint8_t> gif;
  AppendLiteral(gif, "GIF89a", 6);

  nsTArray<uint8_t> bmp;
  AppendLiteral(bmp, "BM\x00\x00", 4);

  nsTArray<uint8_t> jxl;
  AppendLiteral(jxl, "\xFF\x0A", 2);

  nsTArray<uint8_t> svg;
  AppendLiteral(svg, "<svg xmlns=", 11);

  EXPECT_EQ(EXIFContainer::GIF, Scan(gif).container);
  EXPECT_EQ(EXIFContainer::BMP, Scan(bmp).container);
  EXPECT_EQ(EXIFContainer::JXL, Scan(jxl).container);
  EXPECT_EQ(EXIFContainer::SVG, Scan(svg).container);
}

TEST(EXIFScanner, TinyAndEmptyInputsAreUnknown)
{
  for (size_t length : {size_t(0), size_t(1), size_t(3), size_t(8)}) {
    nsTArray<uint8_t> data;
    data.SetLength(length);

    EXIFScanResult result = Scan(data);
    EXPECT_EQ(EXIFContainer::Unknown, result.container) << "length " << length;
    EXPECT_EQ(EXIFScanOutcome::UnsupportedContainer, result.outcome);
  }
}

TEST(EXIFScanner, RandomBytesAreUnknown)
{
  nsTArray<uint8_t> data;
  AppendLiteral(data, "this is not an image file at all", 32);
  EXPECT_EQ(EXIFContainer::Unknown, Scan(data).container);
}

///////////////////////////////////////////////////////////////////////////////
// Real files.
//
// Everything above is built byte by byte, which is the only way to produce the
// malformed and adversarial shapes, but it also means every one of those inputs
// was written by the same understanding of the format that the code under test
// has. These read files instead: the ones generated by
// generate_exif_fixtures.py, whose containers are real encoder output, and the
// EXIF-bearing images that were already in the tree for other tests.
///////////////////////////////////////////////////////////////////////////////

namespace {

nsTArray<uint8_t> ReadFixture(const char* aName) {
  nsTArray<uint8_t> bytes;
  nsCOMPtr<nsIInputStream> stream = LoadFile(aName);
  if (!stream) {
    return bytes;
  }

  nsCString contents;
  if (NS_FAILED(NS_ConsumeStream(stream, UINT32_MAX, contents))) {
    return bytes;
  }

  bytes.AppendElements(
      reinterpret_cast<const uint8_t*>(contents.BeginReading()),
      contents.Length());
  return bytes;
}

void ExpectFixture(const char* aName, EXIFContainer aContainer,
                   EXIFScanOutcome aOutcome, EXIFGPSPresence aPresence) {
  nsTArray<uint8_t> bytes = ReadFixture(aName);
  ASSERT_FALSE(bytes.IsEmpty())
  << aName << " did not load";

  EXIFScanResult result = Scan(bytes);
  EXPECT_EQ(aContainer, result.container) << aName;
  EXPECT_EQ(aOutcome, result.outcome) << aName;
  EXPECT_EQ(aPresence, result.gpsPresence) << aName;
}

}  // namespace

TEST(EXIFScanner, RealJPEGFiles)
{
  ExpectFixture("exif_gps.jpg", EXIFContainer::JPEG, EXIFScanOutcome::Complete,
                EXIFGPSPresence::Position);
  // The same position written little endian, so both byte orders are covered
  // by a real file and not only by a hand-built block.
  ExpectFixture("exif_gps_le.jpg", EXIFContainer::JPEG,
                EXIFScanOutcome::Complete, EXIFGPSPresence::Position);
  ExpectFixture("exif_nogps.jpg", EXIFContainer::JPEG,
                EXIFScanOutcome::Complete, EXIFGPSPresence::None);
  ExpectFixture("exif_gps_stripped.jpg", EXIFContainer::JPEG,
                EXIFScanOutcome::Complete, EXIFGPSPresence::NoPosition);
  ExpectFixture("exif_gps_zeroed.jpg", EXIFContainer::JPEG,
                EXIFScanOutcome::Complete, EXIFGPSPresence::ZeroPosition);
}

// The false positive that would matter most, against a real file rather than a
// constructed one. See exif_with_interoperability_ifd in the generator.
TEST(EXIFScanner, RealJPEGWithInteroperabilityIFDHasNoPosition)
{
  ExpectFixture("exif_interop.jpg", EXIFContainer::JPEG,
                EXIFScanOutcome::Complete, EXIFGPSPresence::None);
}

TEST(EXIFScanner, RealPNGFiles)
{
  ExpectFixture("exif_gps.png", EXIFContainer::PNG, EXIFScanOutcome::Complete,
                EXIFGPSPresence::Position);
  ExpectFixture("exif_nogps.png", EXIFContainer::PNG, EXIFScanOutcome::NoEXIF,
                EXIFGPSPresence::None);
}

// These two must be counted and named, not written off as having no location.
TEST(EXIFScanner, RealUnwalkedFormatsAreStillIdentified)
{
  ExpectFixture("exif_gps.webp", EXIFContainer::WebP,
                EXIFScanOutcome::UnsupportedContainer, EXIFGPSPresence::None);
  ExpectFixture("exif_gps.heic", EXIFContainer::HEIF,
                EXIFScanOutcome::UnsupportedContainer, EXIFGPSPresence::None);
}

// Images that were in the tree long before this code existed, and that no part
// of this feature had a hand in producing. They are big endian with a real Exif
// sub-IFD, where the generated fixtures above are mostly little endian, so they
// exercise a genuinely different shape.
TEST(EXIFScanner, PreExistingTreeImagesScanCleanly)
{
  for (const char* name : {"exif_resolution.jpg", "green.jpg", "green.png"}) {
    nsTArray<uint8_t> bytes = ReadFixture(name);
    ASSERT_FALSE(bytes.IsEmpty())
    << name << " did not load";

    EXIFScanResult result = Scan(bytes);
    EXPECT_NE(EXIFContainer::Unknown, result.container) << name;
    EXPECT_EQ(EXIFGPSPresence::None, result.gpsPresence) << name;
    // Whatever they contain, the walk has to reach a conclusion rather than
    // running off the end of the file.
    EXPECT_TRUE(result.outcome == EXIFScanOutcome::Complete ||
                result.outcome == EXIFScanOutcome::NoEXIF)
        << name << " outcome " << int(result.outcome);
  }
}
