/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputTelemetry.h"

#include "ImageInputLabels.h"
#include "ImageInputScan.h"
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

  if (!imageinput::ShouldRecordFor(aPrincipal)) {
    return;
  }

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

  if (!aDataTransfer || !imageinput::ShouldRecordFor(aPrincipal)) {
    return;
  }

  // Enumerate as system, which reads the transfer without claiming the cached
  // FileList: the first content principal to ask becomes mFilesPrincipal, and
  // a later non-subsuming one gets nullptr back (DataTransferItemList::Files),
  // so a probe must not be what claims it. System over-counts in exactly one
  // case, a cross-origin subframe drop, where the page is refused items it
  // never receives (DataTransferItem::Data), so ask as the page there.
  //
  // GetFiles materialises the files, which for a paste reads the clipboard
  // synchronously. DataTransfer::HasFile cannot gate that: it inspects only
  // the first indexed item list, while items.add() puts each file at its own
  // index. The previous probe called GetFiles unconditionally and shipped
  // without a regression.
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
