/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_THEBES_FONTPATHSET_H_
#define GFX_THEBES_FONTPATHSET_H_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Atomics.h"
#include "mozilla/TimeStamp.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTHashSet.h"

struct IDWriteFontCollection;

namespace mozilla::gfx {

/**
 * An immutable set of the upper cased file paths of the installed system fonts,
 * as reported by DirectWrite. Building only happens in a process that has
 * called Enable, after which the set is built in the background whenever
 * RebuildFrom is called. The static methods can be called on any thread, unless
 * stated otherwise.
 */
class FontPathSet final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FontPathSet)

  bool Contains(const nsAString& aUpperCasePath) const {
    return mPaths.Contains(aUpperCasePath);
  }

  /**
   * True if a rebuild has been requested since this set was built, in which
   * case it might be missing newly installed fonts.
   */
  bool IsStale() const { return mStale; }

  /**
   * Enables building. The first build happens on the first RebuildFrom, which
   * the font list calls when it fetches the system font collection, so this
   * must be called before the font list is initialized. Main thread only.
   * @return false if building could not be enabled, true if it is enabled,
   *         including by an earlier call.
   */
  [[nodiscard]] static bool Enable();

  /**
   * Marks the current set, if any, as stale and starts a build from
   * aCollection. Does nothing if Enable has not been called.
   */
  static void RebuildFrom(IDWriteFontCollection* aCollection);

  /**
   * Returns the latest set, or null if there is no set yet or after shutdown.
   */
  static already_AddRefed<FontPathSet> GetCurrent();

  /**
   * Waits until a set other than aOld has been built.
   * @param aOld the set already held by the caller, which can be null.
   * @return the newer set, or null if aOld is not stale, aDeadline passes or
   *         on shutdown.
   */
  static already_AddRefed<FontPathSet> GetNewerThan(const FontPathSet* aOld,
                                                    TimeStamp aDeadline);

 private:
  static void Build();

  explicit FontPathSet(nsTHashSet<nsString>&& aPaths)
      : mPaths(std::move(aPaths)) {}
  ~FontPathSet() = default;

  const nsTHashSet<nsString> mPaths;
  Atomic<bool> mStale{false};
};

}  // namespace mozilla::gfx

#endif  // GFX_THEBES_FONTPATHSET_H_
