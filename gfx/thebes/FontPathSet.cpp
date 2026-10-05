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
#include "mozilla/gfx/2D.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"

namespace mozilla::gfx {

static StaticMonitor sMonitor;
static StaticRefPtr<FontPathSet> sCurrent MOZ_GUARDED_BY(sMonitor);
static StaticRefPtr<nsISerialEventTarget> sTaskQueue MOZ_GUARDED_BY(sMonitor);
static uint32_t sGeneration MOZ_GUARDED_BY(sMonitor) = 0;
static bool sBuildQueued MOZ_GUARDED_BY(sMonitor) = false;
static bool sShutdown MOZ_GUARDED_BY(sMonitor) = false;

class FontPathSetBuilder final {
 public:
  static void PostBuild() MOZ_REQUIRES(sMonitor) {
    if (sBuildQueued || sShutdown || !sTaskQueue) {
      return;
    }

    sBuildQueued = true;
    (void)sTaskQueue->Dispatch(
        NS_NewRunnableFunction("FontPathSetBuilder::Build", &Build));
  }

  static void MarkStale(FontPathSet* aSet) { aSet->mStale = true; }

 private:
  static void Build() {
    uint32_t generation;
    {
      StaticMonitorAutoLock lock(sMonitor);
      sBuildQueued = false;
      if (sShutdown) {
        return;
      }
      generation = sGeneration;
    }

    TimeStamp start = TimeStamp::Now();
    nsTHashSet<nsString> paths;
    CollectSystemFontPaths(paths);
    // Background task queue threads are not usually profiled, so we add the
    // marker to the main thread.
    PROFILER_MARKER_TEXT(
        "FontPathSet build (background)", GRAPHICS,
        MarkerOptions(MarkerTiming::IntervalUntilNowFrom(start),
                      MarkerThreadId::MainThread()),
        nsPrintfCString("%u paths", paths.Count()));
    RefPtr<FontPathSet> set = new FontPathSet(std::move(paths));

    StaticMonitorAutoLock lock(sMonitor);
    if (sShutdown) {
      return;
    }

    // Fonts changed while we were building, so publish what we have, because
    // it is probably newer than the current set, but build again.
    if (generation != sGeneration) {
      set->mStale = true;
      PostBuild();
    }

    sCurrent = set.forget();
    lock.NotifyAll();
  }

  static void CollectSystemFontPaths(nsTHashSet<nsString>& aPaths) {
    RefPtr<IDWriteFactory> factory = Factory::EnsureDWriteFactory();
    RefPtr<IDWriteFactory3> factory3;
    if (!factory || FAILED(factory->QueryInterface(__uuidof(IDWriteFactory3),
                                                   getter_AddRefs(factory3)))) {
      NS_WARNING("Failed to get IDWriteFactory3.");
      return;
    }

    RefPtr<IDWriteFontSet> fontSet;
    if (FAILED(factory3->GetSystemFontSet(getter_AddRefs(fontSet)))) {
      NS_WARNING("Failed to get DirectWrite system font set.");
      return;
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
      if (FAILED(localLoader->GetFilePathLengthFromKey(key, keySize,
                                                       &pathLength))) {
        continue;
      }

      nsString path;
      path.SetLength(pathLength);
      if (FAILED(localLoader->GetFilePathFromKey(
              key, keySize, reinterpret_cast<WCHAR*>(path.BeginWriting()),
              pathLength + 1))) {
        continue;
      }

      ::CharUpperBuffW(reinterpret_cast<WCHAR*>(path.BeginWriting()),
                       pathLength);
      aPaths.Insert(path);
    }
  }
};

/* static */
void FontPathSet::Enable() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsISerialEventTarget> taskQueue;
  {
    StaticMonitorAutoLock lock(sMonitor);
    if (sTaskQueue || sShutdown) {
      return;
    }
  }

  if (NS_FAILED(NS_CreateBackgroundTaskQueue("FontPathSet",
                                             getter_AddRefs(taskQueue)))) {
    NS_WARNING("Failed to create FontPathSet task queue.");
    return;
  }

  RunOnShutdown(
      [] {
        StaticMonitorAutoLock lock(sMonitor);
        sShutdown = true;
        sCurrent = nullptr;
        sTaskQueue = nullptr;
        lock.NotifyAll();
      },
      ShutdownPhase::XPCOMWillShutdown);

  StaticMonitorAutoLock lock(sMonitor);
  sTaskQueue = taskQueue.forget();
  ++sGeneration;
  FontPathSetBuilder::PostBuild();
}

/* static */
void FontPathSet::MarkStale() {
  StaticMonitorAutoLock lock(sMonitor);
  if (!sTaskQueue) {
    return;
  }

  ++sGeneration;
  if (sCurrent) {
    FontPathSetBuilder::MarkStale(sCurrent);
  }
  FontPathSetBuilder::PostBuild();
}

/* static */
already_AddRefed<FontPathSet> FontPathSet::GetCurrent() {
  StaticMonitorAutoLock lock(sMonitor);
  return do_AddRef(sCurrent.get());
}

/* static */
already_AddRefed<FontPathSet> FontPathSet::GetNewerThan(const FontPathSet* aOld,
                                                        TimeDuration aTimeout) {
  TimeStamp deadline = TimeStamp::Now() + aTimeout;
  StaticMonitorAutoLock lock(sMonitor);
  while (!sShutdown && sCurrent == aOld) {
    TimeDuration remaining = deadline - TimeStamp::Now();
    if (remaining <= TimeDuration()) {
      break;
    }
    lock.Wait(remaining);
  }

  return do_AddRef(sCurrent.get());
}

}  // namespace mozilla::gfx
