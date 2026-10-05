/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <shlobj.h>
#include <windows.h>

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/FontFileSet.h"

using namespace mozilla;
using namespace mozilla::gfx;

static const TimeDuration kTimeout = TimeDuration::FromSeconds(30);

static nsString GetUpperCaseSystemFontPath(const nsAString& aFileName) {
  nsString path;
  PWSTR fontsDir = nullptr;
  if (SUCCEEDED(
          ::SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fontsDir))) {
    path.Assign(reinterpret_cast<const char16_t*>(fontsDir));
    path.Append(u'\\');
    path.Append(aFileName);
    ::CharUpperBuffW(reinterpret_cast<WCHAR*>(path.BeginWriting()),
                     path.Length());
  }
  ::CoTaskMemFree(fontsDir);
  return path;
}

static already_AddRefed<FontFileSet> GetUpToDateSet() {
  FontFileSet::Enable();
  RefPtr<FontFileSet> set = FontFileSet::GetNewerThan(nullptr, kTimeout);
  while (set && set->IsStale()) {
    RefPtr<FontFileSet> newer = FontFileSet::GetNewerThan(set, kTimeout);
    if (newer == set) {
      break;
    }
    set = newer.forget();
  }
  return set.forget();
}

TEST(FontFileSet, ContainsSystemFont)
{
  RefPtr<FontFileSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  EXPECT_TRUE(set->Contains(GetUpperCaseSystemFontPath(u"ARIAL.TTF"_ns)));
  EXPECT_FALSE(set->Contains(GetUpperCaseSystemFontPath(u"NOTAFONT.TTF"_ns)));
}

TEST(FontFileSet, MarkStaleRebuilds)
{
  RefPtr<FontFileSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  ASSERT_FALSE(set->IsStale());

  FontFileSet::MarkStale();
  EXPECT_TRUE(set->IsStale());

  RefPtr<FontFileSet> newSet = FontFileSet::GetNewerThan(set, kTimeout);
  ASSERT_TRUE(newSet);
  EXPECT_NE(newSet, set);
  EXPECT_TRUE(newSet->Contains(GetUpperCaseSystemFontPath(u"ARIAL.TTF"_ns)));
}

TEST(FontFileSet, GetNewerThanTimesOut)
{
  RefPtr<FontFileSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  ASSERT_FALSE(set->IsStale());

  RefPtr<FontFileSet> sameSet =
      FontFileSet::GetNewerThan(set, TimeDuration::FromMilliseconds(50));
  EXPECT_EQ(sameSet, set);
  EXPECT_EQ(RefPtr<FontFileSet>(FontFileSet::GetCurrent()), set);
}
