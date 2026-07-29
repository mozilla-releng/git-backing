/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputTelemetry.h"

#include "ImageInputLabels.h"
#include "ImageInputScan.h"
// nsIPrincipal only declares IsSystemPrincipal; BasePrincipal.h defines it.
#include "mozilla/BasePrincipal.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/FileList.h"
#include "mozilla/glean/AntitrackingImageinputMetrics.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla {

namespace {

bool ShouldRecordFor(nsIPrincipal* aPrincipal) {
  // The global may have gone away, in which case there is nothing to attribute
  // this to.
  if (!aPrincipal) {
    return false;
  }

  // Not chrome, and not the browser's own pages: dropping a file on
  // about:preferences is not a page being given a photo.
  return !aPrincipal->IsSystemPrincipal() && !aPrincipal->SchemeIs("about");
}

}  // namespace

/* static */
void ImageInputTelemetry::MaybeRecordFiles(
    ImageInputSource aInputType, const nsTArray<RefPtr<dom::File>>& aFiles,
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());

  // Checked before anything else so that flipping it off is enough to stop the
  // mechanism outright, including the release assertion further down.
  if (!StaticPrefs::privacy_imageInputTelemetry_enabled()) {
    return;
  }

  if (!ShouldRecordFor(aPrincipal)) {
    return;
  }

  // Content documents can still run in the parent process in some
  // configurations. Their files must not be read there, but the case is worth
  // seeing in the data rather than silently missing.
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
    if (events >= ImageInputScan::kMaxEventsPerAction) {
      continue;
    }
    ++events;

    const char* declaredType = imageinput::DeclaredTypeLabel(type);

    if (!inContentProcess) {
      ImageInputScan::RecordSkipped(aInputType, declaredType,
                                    "skipped_parent_process");
      continue;
    }

    if (scans >= ImageInputScan::kMaxScansPerAction) {
      ImageInputScan::RecordSkipped(aInputType, declaredType,
                                    "skipped_batch_limit");
      continue;
    }

    ++scans;
    ImageInputScan::Start(file->Impl(), aInputType, declaredType);
  }

  if (candidates > 0) {
    // Recorded uncapped, so that the number of images handed to pages stays
    // exactly countable even when the per-file detail above is truncated.
    AutoTArray<uint64_t, 1> samples{uint64_t(candidates)};
    glean::image_input::batch_size.AccumulateSamples(samples);
  }
}

/* static */
void ImageInputTelemetry::MaybeRecordDataTransfer(
    ImageInputSource aInputType, dom::DataTransfer* aDataTransfer,
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!StaticPrefs::privacy_imageInputTelemetry_enabled()) {
    return;
  }

  if (!aDataTransfer || !ShouldRecordFor(aPrincipal)) {
    return;
  }

  // Which principal we enumerate as decides which files get counted. The system
  // principal sees every file on the transfer; the page's own principal sees
  // only the files the page is allowed to read. For an ordinary drop or a paste
  // these are the same set, because the per-item principal check is applied to
  // exactly one case, a cross-origin subframe drop (see
  // DataTransferItem::Data). That case is where the system principal would
  // over-count files the page is refused and never actually receives, so it --
  // and only it -- is enumerated as the page, keeping the count to what the
  // page is really handed.
  //
  // Asking a transfer for its files forces it to materialise them, which for a
  // paste means reading the system clipboard synchronously.
  // DataTransfer::HasFile would be the cheap way to skip that, but it only
  // inspects the first of the Moz layout's indexed item lists, while
  // DataTransferItemList::Add puts each file at a new index. The two therefore
  // disagree for any transfer built with items.add(), so it cannot be relied on
  // here. The previous version of this probe called GetFiles unconditionally
  // and shipped for several releases without a performance regression, so this
  // does the same; a cheaper pre-check would need a correct helper on
  // DataTransfer itself.
  nsIPrincipal* enumerationPrincipal =
      aDataTransfer->IsCrossDomainSubFrameDrop()
          ? aPrincipal
          : nsContentUtils::GetSystemPrincipal();
  RefPtr<dom::FileList> fileList =
      aDataTransfer->GetFiles(*enumerationPrincipal);
  if (!fileList) {
    return;
  }

  nsTArray<RefPtr<dom::File>> files;
  const uint32_t length = fileList->Length();
  files.SetCapacity(length);
  for (uint32_t i = 0; i < length; ++i) {
    files.AppendElement(fileList->Item(i));
  }

  MaybeRecordFiles(aInputType, files, aPrincipal);
}

}  // namespace mozilla
