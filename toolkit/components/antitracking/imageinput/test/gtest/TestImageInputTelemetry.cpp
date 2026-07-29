/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputLabels.h"
#include "gtest/gtest.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ExpandedPrincipal.h"
#include "mozilla/NullPrincipal.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsNetUtil.h"
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

nsCOMPtr<nsIPrincipal> ContentPrincipal(const nsACString& aSpec) {
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aSpec))) {
    return nullptr;
  }
  // CreateContentPrincipal falls back to a NullPrincipal when it cannot derive
  // an origin, which would make an allowlist test pass for the wrong reason.
  // Callers assert on GetIsContentPrincipal to catch that.
  RefPtr<BasePrincipal> principal =
      BasePrincipal::CreateContentPrincipal(uri, OriginAttributes());
  return principal;
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
    EXPECT_STREQ("true", HasGPSLabel(result).get())
        << "outcome " << ScanOutcomeLabel(outcome).get();
  }
}

TEST(ImageInputTelemetry, AbsenceIsOnlyTrustedWhenTheWholeBlockWasSeen)
{
  EXPECT_STREQ("false", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                               EXIFScanOutcome::Complete,
                                               EXIFGPSPresence::None))
                            .get());
  EXPECT_STREQ("false", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                               EXIFScanOutcome::NoEXIF,
                                               EXIFGPSPresence::None))
                            .get());

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
                                                   EXIFGPSPresence::None))
                                .get())
        << "outcome " << ScanOutcomeLabel(outcome).get();
  }
}

// A GPS IFD with no usable position is what a partially stripped file looks
// like, and is worth telling apart from one that never had a position.
TEST(ImageInputTelemetry, AStrippedPositionIsDistinguishedFromNoneAtAll)
{
  EXPECT_STREQ("ifd_only", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                                  EXIFScanOutcome::Complete,
                                                  EXIFGPSPresence::NoPosition))
                               .get());
  EXPECT_STREQ("unknown", HasGPSLabel(MakeResult(EXIFContainer::JPEG,
                                                 EXIFScanOutcome::EXIFTruncated,
                                                 EXIFGPSPresence::NoPosition))
                              .get());
}

// The parser tells zeroed coordinates apart from a stripped IFD; this metric
// deliberately does not, so the fold has to be asserted somewhere.
TEST(ImageInputTelemetry, ZeroedCoordinatesCountAsAStrippedPosition)
{
  EXPECT_STREQ(
      "ifd_only",
      HasGPSLabel(MakeResult(EXIFContainer::JPEG, EXIFScanOutcome::Complete,
                             EXIFGPSPresence::ZeroPosition))
          .get());
}

// The formats we identify but do not walk must not read as "no GPS".
TEST(ImageInputTelemetry, UnwalkedContainersReportUnknownNotFalse)
{
  for (EXIFContainer container :
       {EXIFContainer::WebP, EXIFContainer::HEIF, EXIFContainer::AVIF}) {
    EXIFScanResult result =
        MakeResult(container, EXIFScanOutcome::UnsupportedContainer,
                   EXIFGPSPresence::None);
    EXPECT_STREQ("unknown", HasGPSLabel(result).get());
    EXPECT_STRNE("unknown", DetectedFormatLabel(container).get());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Vocabularies. Every one of these strings is part of a data contract, so a
// rename shows up here rather than silently in a dashboard.
///////////////////////////////////////////////////////////////////////////////

TEST(ImageInputTelemetry, InputTypeLabels)
{
  EXPECT_STREQ("file_picker",
               InputTypeLabel(ImageInputSource::FilePicker).get());
  EXPECT_STREQ("directory_picker",
               InputTypeLabel(ImageInputSource::DirectoryPicker).get());
  EXPECT_STREQ("drop", InputTypeLabel(ImageInputSource::Drop).get());
  EXPECT_STREQ("paste", InputTypeLabel(ImageInputSource::Paste).get());
}

///////////////////////////////////////////////////////////////////////////////
// Who counts as a web page. This is an allowlist, so what is pinned here is
// mostly the negative: a principal reaching the data that nobody meant to
// collect from. Extension code is the one most easily mistaken for a page.
///////////////////////////////////////////////////////////////////////////////

TEST(ImageInputTelemetry, WebPagesAreRecorded)
{
  for (const nsCString& spec :
       {"https://example.com/"_ns, "http://example.com/"_ns,
        "file:///tmp/photos.html"_ns}) {
    nsCOMPtr<nsIPrincipal> principal = ContentPrincipal(spec);
    ASSERT_TRUE(principal && principal->GetIsContentPrincipal())
    << spec.get();
    EXPECT_TRUE(ShouldRecordFor(principal)) << spec.get();
  }
}

// A sandboxed iframe and a data: document both get an opaque origin, and a
// file handed to either is still readable by page script. The browser tests
// all run on data: URLs, so this is the case they exercise.
TEST(ImageInputTelemetry, OpaqueOriginsAreStillWebContent)
{
  RefPtr<NullPrincipal> principal = NullPrincipal::Create(OriginAttributes());
  ASSERT_TRUE(principal);
  EXPECT_TRUE(ShouldRecordFor(principal));
}

TEST(ImageInputTelemetry, ExtensionPagesAreNotWebPages)
{
  nsCOMPtr<nsIPrincipal> principal = ContentPrincipal(
      "moz-extension://8ea6d31b-916a-46b1-9c95-0b0dd7ff5f0d/options.html"_ns);
  ASSERT_TRUE(principal && principal->GetIsContentPrincipal());
  EXPECT_FALSE(ShouldRecordFor(principal));
}

// A content script sandbox carries an expanded principal, which is a list of
// origins rather than one. Even if it were in scope there would be nothing
// coherent to attribute it to.
TEST(ImageInputTelemetry, ContentScriptSandboxesAreNotWebPages)
{
  nsTArray<nsCOMPtr<nsIPrincipal>> allowList;
  allowList.AppendElement(ContentPrincipal("https://example.com/"_ns));
  RefPtr<ExpandedPrincipal> principal =
      ExpandedPrincipal::Create(allowList, OriginAttributes());
  ASSERT_TRUE(principal);
  EXPECT_FALSE(ShouldRecordFor(principal));
}

TEST(ImageInputTelemetry, ChromeAndAboutPagesAreNotWebPages)
{
  EXPECT_FALSE(ShouldRecordFor(nsContentUtils::GetSystemPrincipal()));

  nsCOMPtr<nsIPrincipal> about = ContentPrincipal("about:preferences"_ns);
  ASSERT_TRUE(about && about->GetIsContentPrincipal());
  EXPECT_FALSE(ShouldRecordFor(about));
}

TEST(ImageInputTelemetry, NoPrincipalIsNotRecorded)
{
  EXPECT_FALSE(ShouldRecordFor(nullptr));
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
