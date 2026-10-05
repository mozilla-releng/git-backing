/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_FONTFILESET_H_
#define MOZILLA_GFX_FONTFILESET_H_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Atomics.h"
#include "mozilla/TimeStamp.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTHashSet.h"

namespace mozilla::gfx {

/**
 * An immutable set of the upper cased file paths of the installed system fonts,
 * as reported by DirectWrite. Building only happens in a process that has
 * called Enable, after which the set is rebuilt in the background whenever
 * MarkStale is called. The static methods can be called on any thread, unless
 * stated otherwise.
 */
class FontFileSet final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FontFileSet)

  bool Contains(const nsAString& aUpperCasePath) const {
    return mPaths.Contains(aUpperCasePath);
  }

  /**
   * True if a rebuild has been requested since this set was built, in which
   * case it might be missing newly installed fonts.
   */
  bool IsStale() const { return mStale; }

  /**
   * Enables building and starts the initial build. Main thread only.
   */
  static void Enable();

  /**
   * Marks the current set as stale and starts a rebuild. Does nothing if
   * Enable has not been called.
   */
  static void MarkStale();

  /**
   * Returns the latest set, or null if there is no set yet or after shutdown.
   */
  static already_AddRefed<FontFileSet> GetCurrent();

  /**
   * Waits until a set other than aOld has been built, until aTimeout has
   * passed or until shutdown, then returns the latest set as GetCurrent does.
   * @param aOld the set already held by the caller, which can be null.
   */
  static already_AddRefed<FontFileSet> GetNewerThan(const FontFileSet* aOld,
                                                    TimeDuration aTimeout);

 private:
  friend class FontFileSetBuilder;

  explicit FontFileSet(nsTHashSet<nsString>&& aPaths)
      : mPaths(std::move(aPaths)) {}
  ~FontFileSet() = default;

  const nsTHashSet<nsString> mPaths;
  Atomic<bool> mStale{false};
};

}  // namespace mozilla::gfx

#endif  // MOZILLA_GFX_FONTFILESET_H_
