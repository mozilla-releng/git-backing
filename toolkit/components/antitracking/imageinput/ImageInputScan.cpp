/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputScan.h"

#include <algorithm>

#include "ImageInputBlobReader.h"
#include "ImageInputLabels.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/glean/AntitrackingImageinputMetrics.h"
#include "mozilla/image/EXIFScanner.h"
#include "nsISerialEventTarget.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla {

namespace {

// How much of the front of a file we read.
//
// The size that has to fit is a JPEG's EXIF segment. An APP1 segment's length
// field allows 65535 bytes, and the segment can sit behind an SOI and a minimal
// JFIF APP0, putting the last byte of EXIF 65557 bytes in. That makes 64 KiB
// provably too small. It is not a ceiling -- a JFIF APP0 may itself carry a
// thumbnail -- so this leaves room on top for whatever else an encoder puts in
// front. See EXIF Section 4.7.2 and ITU-T T.871 Section 10.1.
constexpr uint32_t kPrefixLength = 128 * 1024;

// The queue every scan runs on. One shared serial queue rather than one per
// file, so that a large selection cannot spawn a thread per image, and so that
// the parsing is naturally rate limited.
StaticRefPtr<nsISerialEventTarget> sScanQueue;

already_AddRefed<nsISerialEventTarget> GetScanQueue() {
  MOZ_ASSERT(NS_IsMainThread());

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown)) {
    return nullptr;
  }

  if (!sScanQueue) {
    nsCOMPtr<nsISerialEventTarget> queue;
    if (NS_FAILED(NS_CreateBackgroundTaskQueue("ImageInputScan",
                                               getter_AddRefs(queue)))) {
      return nullptr;
    }
    sScanQueue = queue;
    ClearOnShutdown(&sScanQueue);
  }

  nsCOMPtr<nsISerialEventTarget> queue = sScanQueue.get();
  return queue.forget();
}

void RecordEvent(ImageInputSource aInputType, const char* aDeclaredType,
                 const char* aDetectedFormat, const char* aHasGPS,
                 const char* aScanOutcome) {
  glean::image_input::OfferedExtra extra = {
      .declaredType = Some(nsCString(aDeclaredType)),
      .detectedFormat = Some(nsCString(aDetectedFormat)),
      .hasGps = Some(nsCString(aHasGPS)),
      .inputType = Some(nsCString(imageinput::InputTypeLabel(aInputType))),
      .scanOutcome = Some(nsCString(aScanOutcome)),
  };
  glean::image_input::offered.Record(Some(extra));
}

}  // namespace

/* static */
void ImageInputScan::RecordSkipped(ImageInputSource aInputType,
                                   const char* aDeclaredType,
                                   const char* aScanOutcome) {
  RecordEvent(aInputType, aDeclaredType, "not_scanned", "unknown",
              aScanOutcome);
}

/* static */
void ImageInputScan::Start(dom::BlobImpl* aBlob, ImageInputSource aInputType,
                           const char* aDeclaredType) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aBlob);

  // The bytes of a file a user has handed to a web page must never be read in
  // the parent process. Callers gate on the process type before reaching here,
  // so this is a backstop against a caller added later that does not; the
  // privacy.imageInputTelemetry.enabled pref turns the whole mechanism off if
  // it ever fires in the wild.
  MOZ_RELEASE_ASSERT(XRE_IsContentProcess(),
                     "Image input files must only be read in a content "
                     "process");

  IgnoredErrorResult rv;
  uint64_t size = aBlob->GetSize(rv);
  if (rv.Failed()) {
    RecordSkipped(aInputType, aDeclaredType, "read_error");
    return;
  }
  if (size == 0) {
    RecordSkipped(aInputType, aDeclaredType, "skipped_empty_file");
    return;
  }

  nsCOMPtr<nsISerialEventTarget> queue = GetScanQueue();
  if (!queue) {
    RecordSkipped(aInputType, aDeclaredType, "read_error");
    return;
  }

  // A blob can report an unknown size, so clamp rather than trusting it.
  const uint32_t toRead = uint32_t(std::min<uint64_t>(size, kPrefixLength));

  RefPtr<ImageInputScan> scan =
      new ImageInputScan(aInputType, aDeclaredType, size);

  ImageInputBlobReader::Read(
      aBlob, toRead, queue,
      [scan](nsresult aStatus, nsTArray<uint8_t>&& aBytes) {
        scan->OnBytes(aStatus, std::move(aBytes));
      });
}

void ImageInputScan::OnBytes(nsresult, nsTArray<uint8_t>&& aBytes) {
  MOZ_RELEASE_ASSERT(XRE_IsContentProcess(),
                     "Image input files must only be parsed in a content "
                     "process");

  // A short read is not a failure. What we are looking for sits at the front of
  // the file, so whatever arrived is worth scanning, and the scanner is told
  // the real file size so it knows the difference between the file ending and
  // our buffer ending. Only a read that produced nothing at all is fatal.
  if (aBytes.IsEmpty()) {
    RecordSkipped(mInputType, mDeclaredType, "read_error");
    return;
  }

  image::EXIFScanResult result = image::ScanEXIF(
      Span<const uint8_t>(aBytes.Elements(), aBytes.Length()), mFileSize);

  // The scan may have decoded an actual position into result.gpsCoordinates.
  // This is the point where that is reduced to a boolean and thrown away:
  // HasGPSLabel reads only the presence classification, and the event has no
  // field that could carry a coordinate. The location is never recorded,
  // logged, or forwarded -- only whether one was present. (Bug 2058913.)
  RecordEvent(mInputType, mDeclaredType,
              imageinput::DetectedFormatLabel(result.container),
              imageinput::HasGPSLabel(result),
              imageinput::ScanOutcomeLabel(result.outcome));
}

}  // namespace mozilla
