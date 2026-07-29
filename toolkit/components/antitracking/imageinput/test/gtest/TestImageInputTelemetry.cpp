/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputLabels.h"
#include "gtest/gtest.h"
#include "nsString.h"

using namespace mozilla;
using namespace mozilla::image;
using namespace mozilla::imageinput;

namespace {

EXIFScanResult MakeResult(EXIFContainer aContainer, EXIFScanOutcome aOutcome,
                          EXIFGPSPresence aPresence) {
  EXIFScanResult result;
  result.container = aContainer;
  result.outcome = aOutcome;
  result.gpsPresence = aPresence;
  return result;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// The asymmetry rule. This is the subtlest thing in the whole probe: a position
// that was found is trustworthy however little of the file we read, but not
// finding one only means something if the whole EXIF block was seen. Getting
// this backwards would silently under-report, which is exactly the error the
// data is meant to rule out.
///////////////////////////////////////////////////////////////////////////////

TEST(ImageInputTelemetry, APositionIsReportedUnderEveryOutcome)
{
  const EXIFScanOutcome kAllOutcomes[] = {
      EXIFScanOutcome::Complete,
      EXIFScanOutcome::NoEXIF,
      EXIFScanOutcome::UnsupportedContainer,
      EXIFScanOutcome::MalformedContainer,
      EXIFScanOutcome::UnsupportedEXIFEncoding,
      EXIFScanOutcome::EXIFTruncated,
      EXIFScanOutcome::EXIFLocationUnknown,
  };

  for (EXIFScanOutcome outcome : kAllOutcomes) {
    EXIFScanResult result =
        MakeResult(EXIFContainer::JPEG, outcome, EXIFGPSPresence::Position);
    EXPECT_STREQ("true", HasGPSLabel(result))
        << "outcome " << ScanOutcomeLabel(outcome);
  }
}

TEST(ImageInputTelemetry, AbsenceIsOnlyTrustedWhenTheWholeBlockWasSeen)
{
  EXPECT_STREQ("false", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                               EXIFScanOutcome::Complete,
                                               EXIFGPSPresence::None)));
  EXPECT_STREQ("false", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                               EXIFScanOutcome::NoEXIF,
                                               EXIFGPSPresence::None)));

  // Everything else could not tell, and must say so rather than reporting a
  // confident negative.
  const EXIFScanOutcome kInconclusive[] = {
      EXIFScanOutcome::UnsupportedContainer,
      EXIFScanOutcome::MalformedContainer,
      EXIFScanOutcome::UnsupportedEXIFEncoding,
      EXIFScanOutcome::EXIFTruncated,
      EXIFScanOutcome::EXIFLocationUnknown,
  };

  for (EXIFScanOutcome outcome : kInconclusive) {
    EXPECT_STREQ("unknown", HasGPSLabel(MakeResult(EXIFContainer::JPEG, outcome,
                                                   EXIFGPSPresence::None)))
        << "outcome " << ScanOutcomeLabel(outcome);
  }
}

// A GPS IFD with no usable position is what a partially stripped file looks
// like, and is worth telling apart from one that never had a position.
TEST(ImageInputTelemetry, AStrippedPositionIsDistinguishedFromNoneAtAll)
{
  EXPECT_STREQ("ifd_only", HasGPSLabel(MakeResult(
                               EXIFContainer::JPEG, EXIFScanOutcome::Complete,
                               EXIFGPSPresence::NoPosition)));
  EXPECT_STREQ("unknown", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                                 EXIFScanOutcome::EXIFTruncated,
                                                 EXIFGPSPresence::NoPosition)));
}

// The parser tells zeroed coordinates apart from a stripped IFD; this metric
// deliberately does not, so the fold has to be asserted somewhere.
TEST(ImageInputTelemetry, ZeroedCoordinatesCountAsAStrippedPosition)
{
  EXPECT_STREQ("ifd_only", HasGPSLabel(MakeResult(
                               EXIFContainer::JPEG, EXIFScanOutcome::Complete,
                               EXIFGPSPresence::ZeroPosition)));
}

// The formats we identify but do not walk must not read as "no GPS".
TEST(ImageInputTelemetry, UnwalkedContainersReportUnknownNotFalse)
{
  for (EXIFContainer container :
       {EXIFContainer::WebP, EXIFContainer::HEIF, EXIFContainer::AVIF}) {
    EXIFScanResult result =
        MakeResult(container, EXIFScanOutcome::UnsupportedContainer,
                   EXIFGPSPresence::None);
    EXPECT_STREQ("unknown", HasGPSLabel(result));
    EXPECT_STRNE("unknown", DetectedFormatLabel(container));
  }
}

///////////////////////////////////////////////////////////////////////////////
// Vocabularies. Every one of these strings is part of a data contract, so a
// rename shows up here rather than silently in a dashboard.
///////////////////////////////////////////////////////////////////////////////

TEST(ImageInputTelemetry, InputTypeLabels)
{
  EXPECT_STREQ("file_picker", InputTypeLabel(ImageInputSource::FilePicker));
  EXPECT_STREQ("directory_picker",
               InputTypeLabel(ImageInputSource::DirectoryPicker));
  EXPECT_STREQ("drop", InputTypeLabel(ImageInputSource::Drop));
  EXPECT_STREQ("paste", InputTypeLabel(ImageInputSource::Paste));
}

TEST(ImageInputTelemetry, DeclaredTypeIsMappedOntoAClosedSet)
{
  EXPECT_STREQ("image/jpeg", DeclaredTypeLabel("image/jpeg"_ns));
  EXPECT_STREQ("image/heic", DeclaredTypeLabel("image/heic"_ns));
  EXPECT_STREQ("empty", DeclaredTypeLabel(""_ns));

  // A page can put whatever it likes in a File's type, so nothing unrecognised
  // may be passed through into the telemetry key.
  EXPECT_STREQ("other", DeclaredTypeLabel("image/not-a-real-type"_ns));
  EXPECT_STREQ("other", DeclaredTypeLabel("application/octet-stream"_ns));
  EXPECT_STREQ("other", DeclaredTypeLabel("image/jpeg; charset=evil"_ns));
}

///////////////////////////////////////////////////////////////////////////////
// Candidate selection. The extension fallback is the fix for the blind spot the
// previous version of this probe had: HEIC often arrives with no MIME type at
// all, and gating on the type alone made it invisible.
///////////////////////////////////////////////////////////////////////////////

TEST(ImageInputTelemetry, FilesWithAnImageMIMETypeAreCandidates)
{
  EXPECT_TRUE(IsCandidateImage("image/jpeg"_ns, u"photo.jpg"_ns));
  EXPECT_TRUE(IsCandidateImage("image/png"_ns, u"no-extension"_ns));
}

TEST(ImageInputTelemetry, HEICWithNoMIMETypeIsStillACandidate)
{
  EXPECT_TRUE(IsCandidateImage(""_ns, u"IMG_0001.HEIC"_ns));
  EXPECT_TRUE(IsCandidateImage(""_ns, u"IMG_0001.heic"_ns));
  EXPECT_TRUE(IsCandidateImage(""_ns, u"scan.heif"_ns));
}

TEST(ImageInputTelemetry, ExtensionMatchingIsCaseInsensitive)
{
  EXPECT_TRUE(IsCandidateImage(""_ns, u"PHOTO.JPG"_ns));
  EXPECT_TRUE(IsCandidateImage(""_ns, u"Photo.JpEg"_ns));
}

TEST(ImageInputTelemetry, NonImagesAreNotCandidates)
{
  EXPECT_FALSE(IsCandidateImage("text/plain"_ns, u"notes.txt"_ns));
  EXPECT_FALSE(IsCandidateImage(""_ns, u"archive.zip"_ns));
  EXPECT_FALSE(IsCandidateImage(""_ns, u""_ns));
  // A name that merely contains an extension is not one that ends with it.
  EXPECT_FALSE(IsCandidateImage(""_ns, u"jpg.exe"_ns));
}
