/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputTelemetry.h"

#include "ImageInputLabels.h"
#include "ImageInputScan.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/DataTransferItem.h"
#include "mozilla/dom/DataTransferItemList.h"
#include "mozilla/dom/File.h"
#include "nsString.h"
#include "mozilla/glean/AntitrackingImageinputMetrics.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsITransferable.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla {

namespace {

bool ShouldRecord(nsIPrincipal* aPrincipal) {
  // Checked before anything else so that flipping it off is enough to stop the
  // mechanism outright, including the release assertion in ImageInputScan.
  if (!StaticPrefs::privacy_imageInputTelemetry_enabled()) {
    return false;
  }
  return imageinput::ShouldRecordFor(aPrincipal);
}

// Records one user action: the files handed over, plus any image items that
// were counted without being turned into files.
void RecordSelection(ImageInputSource aInputType,
                     const nsTArray<RefPtr<dom::File>>& aFiles,
                     uint32_t aUnmaterializedImages) {
  // Nothing reaching here should be in the parent process: web content is
  // always out of process, and extension documents, which are not, never get
  // past ShouldRecordFor. The check stays because the alternative is the
  // release assertion in ImageInputScan::Start firing on a caller added later,
  // and it records rather than returning quietly so that a branch expected
  // never to run cannot fail in silence.
  const bool inContentProcess = XRE_IsContentProcess();

  uint32_t candidates = 0;
  uint32_t events = 0;
  uint32_t scans = 0;

  for (const RefPtr<dom::File>& file : aFiles) {
    if (!file) {
      continue;
    }

    nsAutoString name;
    file->GetName(name);

    nsAutoString type16;
    file->GetType(type16);
    NS_ConvertUTF16toUTF8 type(type16);

    if (!imageinput::IsCandidateImage(type, name)) {
      continue;
    }

    ++candidates;
    // Past the event ceiling a file is counted only through batch_size below.
    if (events >= ImageInputScan::kMaxEventsPerAction) {
      continue;
    }
    ++events;

    if (!inContentProcess) {
      ImageInputScan::RecordSkipped(aInputType, "skipped_parent_process"_ns);
      continue;
    }

    if (scans >= ImageInputScan::kMaxScansPerAction) {
      ImageInputScan::RecordSkipped(aInputType, "skipped_batch_limit"_ns);
      continue;
    }

    ++scans;
    ImageInputScan::Start(file->Impl(), aInputType);
  }

  for (uint32_t i = 0; i < aUnmaterializedImages; ++i) {
    ++candidates;
    if (events >= ImageInputScan::kMaxEventsPerAction) {
      continue;
    }
    ++events;
    ImageInputScan::RecordSkipped(aInputType, "skipped_bitmap"_ns);
  }

  if (candidates > 0) {
    // Recorded uncapped, so that the number of images handed to pages stays
    // exactly countable even when the per-file detail above is truncated.
    AutoTArray<uint64_t, 1> samples{uint64_t(candidates)};
    glean::image_input::batch_size.AccumulateSamples(samples);
  }
}

}  // namespace

/* static */
void ImageInputTelemetry::MaybeRecordFiles(
    ImageInputSource aInputType, const nsTArray<RefPtr<dom::File>>& aFiles,
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!ShouldRecord(aPrincipal)) {
    return;
  }

  RecordSelection(aInputType, aFiles, 0);
}

/* static */
void ImageInputTelemetry::MaybeRecordDataTransfer(
    ImageInputSource aInputType, dom::DataTransfer* aDataTransfer,
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!aDataTransfer || !ShouldRecord(aPrincipal)) {
    return;
  }

  // Read the items as system, which sees everything the page will be handed
  // without touching the FileList the page itself asks for. System over-counts
  // in exactly one case, a cross-origin subframe drop, where the page is
  // refused items it never receives (DataTransferItem::Data), so ask as the
  // page there.
  nsIPrincipal* principal = aDataTransfer->IsCrossDomainSubFrameDrop()
                                ? aPrincipal
                                : nsContentUtils::GetSystemPrincipal();

  // An external transfer's file items are filled in lazily. One that names a
  // file (application/x-moz-file) resolves to a path, which is cheap. One that
  // names an image the platform holds as pixels -- image/png from a screenshot
  // on the clipboard, or an image dragged out of another application -- is
  // produced by decoding those pixels and encoding a PNG, synchronously and,
  // for a paste, behind a synchronous clipboard read. That is far more than a
  // probe may cost, so those are counted from their type and never asked for.
  // A transfer the page built itself already holds its Files, and asking for
  // them costs nothing.
  const bool external = aDataTransfer->IsExternal();
  const nsLiteralString fileMime = NS_LITERAL_STRING_FROM_CSTRING(kFileMime);

  dom::DataTransferItemList* items = aDataTransfer->Items();
  nsTArray<RefPtr<dom::File>> files;
  uint32_t unmaterializedImages = 0;

  const uint32_t length = items->Length();
  for (uint32_t i = 0; i < length; ++i) {
    bool found = false;
    dom::DataTransferItem* item = items->IndexedGetter(i, found);
    if (!found || !item || item->Kind() != dom::DataTransferItem::KIND_FILE) {
      continue;
    }

    if (external && !item->IsInternalType(fileMime)) {
      nsAutoString type16;
      item->GetInternalType(type16);
      if (imageinput::IsCandidateImage(NS_ConvertUTF16toUTF8(type16),
                                       u""_ns)) {
        ++unmaterializedImages;
      }
      continue;
    }

    IgnoredErrorResult rv;
    RefPtr<dom::File> file = item->GetAsFile(*principal, rv);
    if (rv.Failed() || !file) {
      continue;
    }
    files.AppendElement(std::move(file));
  }

  RecordSelection(aInputType, files, unmaterializedImages);
}

}  // namespace mozilla
