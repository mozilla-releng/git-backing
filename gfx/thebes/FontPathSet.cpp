/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontPathSet.h"

#include <dwrite_3.h>
#include <windows.h>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticMonitor.h"
#include "mozilla/StaticPtr.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"

namespace mozilla::gfx {

static StaticMonitor sMonitor;
static StaticRefPtr<FontPathSet> sCurrent MOZ_GUARDED_BY(sMonitor);
// Only set while enabled and not shut down, so it is also used to check that.
static StaticRefPtr<nsISerialEventTarget> sTaskQueue MOZ_GUARDED_BY(sMonitor);
// The collection for the next build, which is only set while a build is queued
// and has not started.
static StaticRefPtr<IDWriteFontCollection> sPendingCollection
    MOZ_GUARDED_BY(sMonitor);

static bool CollectFontPaths(IDWriteFontCollection* aCollection,
                             nsTHashSet<nsString>& aPaths) {
  RefPtr<IDWriteFontCollection1> collection;
  RefPtr<IDWriteFontSet> fontSet;
  if (FAILED(aCollection->QueryInterface(__uuidof(IDWriteFontCollection1),
                                         getter_AddRefs(collection))) ||
      FAILED(collection->GetFontSet(getter_AddRefs(fontSet)))) {
    NS_WARNING("Failed to get DirectWrite font set.");
    return false;
  }

  UINT32 fontCount = fontSet->GetFontCount();
  for (UINT32 i = 0; i < fontCount; ++i) {
    RefPtr<IDWriteFontFaceReference> faceRef;
    RefPtr<IDWriteFontFile> fontFile;
    RefPtr<IDWriteFontFileLoader> loader;
    RefPtr<IDWriteLocalFontFileLoader> localLoader;
    const void* key;
    UINT32 keySize;
    if (FAILED(fontSet->GetFontFaceReference(i, getter_AddRefs(faceRef))) ||
        FAILED(faceRef->GetFontFile(getter_AddRefs(fontFile))) ||
        FAILED(fontFile->GetReferenceKey(&key, &keySize)) ||
        FAILED(fontFile->GetLoader(getter_AddRefs(loader))) ||
        FAILED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                      getter_AddRefs(localLoader)))) {
      continue;
    }

    UINT32 pathLength;
    if (FAILED(
            localLoader->GetFilePathLengthFromKey(key, keySize, &pathLength))) {
      continue;
    }

    nsString path;
    path.SetLength(pathLength);
    if (FAILED(localLoader->GetFilePathFromKey(
            key, keySize, reinterpret_cast<WCHAR*>(path.BeginWriting()),
            pathLength + 1))) {
      continue;
    }

    ::CharUpperBuffW(reinterpret_cast<WCHAR*>(path.BeginWriting()), pathLength);
    aPaths.Insert(path);
  }

  return true;
}

/* static */
void FontPathSet::Build() {
  RefPtr<IDWriteFontCollection> collection;
  {
    StaticMonitorAutoLock lock(sMonitor);
    collection = sPendingCollection.forget();
    if (!sTaskQueue) {
      return;
    }
  }
  MOZ_ASSERT(collection);

  TimeStamp start = TimeStamp::Now();
  nsTHashSet<nsString> paths;
  bool collected = CollectFontPaths(collection, paths);
  // Background task queue threads are not usually profiled, so we add the
  // marker to the main thread.
  PROFILER_MARKER_TEXT("FontPathSet build (background)", GRAPHICS,
                       MarkerOptions(MarkerTiming::IntervalUntilNowFrom(start),
                                     MarkerThreadId::MainThread()),
                       collected ? nsPrintfCString("%u paths", paths.Count())
                                 : nsPrintfCString("failed"));

  RefPtr<FontPathSet> set =
      collected ? new FontPathSet(std::move(paths)) : nullptr;

  StaticMonitorAutoLock lock(sMonitor);
  if (!sTaskQueue) {
    return;
  }

  // On failure we keep the current set.
  if (set) {
    sCurrent = set.forget();
  }

  // If RebuildFrom was called during the build, another build will have been
  // queued, so the current set is still stale.
  if (sCurrent) {
    sCurrent->mStale = !!sPendingCollection;
  }
  lock.NotifyAll();
}

/* static */
bool FontPathSet::Enable() {
  MOZ_ASSERT(NS_IsMainThread());

  {
    StaticMonitorAutoLock lock(sMonitor);
    if (sTaskQueue) {
      return true;
    }
  }

  nsCOMPtr<nsISerialEventTarget> taskQueue;
  if (NS_FAILED(NS_CreateBackgroundTaskQueue("FontPathSet",
                                             getter_AddRefs(taskQueue)))) {
    NS_WARNING("Failed to create FontPathSet task queue.");
    return false;
  }

  RunOnShutdown(
      [] {
        StaticMonitorAutoLock lock(sMonitor);
        sCurrent = nullptr;
        sTaskQueue = nullptr;
        sPendingCollection = nullptr;
        lock.NotifyAll();
      },
      ShutdownPhase::XPCOMWillShutdown);

  StaticMonitorAutoLock lock(sMonitor);
  sTaskQueue = taskQueue.forget();
  return true;
}

/* static */
void FontPathSet::RebuildFrom(IDWriteFontCollection* aCollection) {
  MOZ_ASSERT(aCollection);
  if (!aCollection) {
    return;
  }

  StaticMonitorAutoLock lock(sMonitor);
  if (!sTaskQueue) {
    return;
  }

  if (sCurrent) {
    sCurrent->mStale = true;
  }

  bool buildQueued = !!sPendingCollection;
  sPendingCollection = aCollection;
  if (!buildQueued) {
    (void)sTaskQueue->Dispatch(
        NS_NewRunnableFunction("FontPathSet::Build", &FontPathSet::Build));
  }
}

/* static */
already_AddRefed<FontPathSet> FontPathSet::GetCurrent() {
  StaticMonitorAutoLock lock(sMonitor);
  return do_AddRef(sCurrent.get());
}

/* static */
already_AddRefed<FontPathSet> FontPathSet::GetNewerThan(const FontPathSet* aOld,
                                                        TimeStamp aDeadline) {
  StaticMonitorAutoLock lock(sMonitor);
  while (sCurrent == aOld) {
    if (!sTaskQueue || (aOld && !aOld->IsStale())) {
      return nullptr;
    }

    TimeDuration remaining = aDeadline - TimeStamp::Now();
    if (remaining <= TimeDuration()) {
      return nullptr;
    }

    lock.Wait(remaining);
  }

  return do_AddRef(sCurrent.get());
}

}  // namespace mozilla::gfx
