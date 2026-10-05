/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <shlobj.h>
#include <windows.h>

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/FontPathSet.h"

using namespace mozilla;
using namespace mozilla::gfx;

static TimeDuration Timeout() { return TimeDuration::FromSeconds(30); }

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

static already_AddRefed<FontPathSet> GetUpToDateSet() {
  FontPathSet::Enable();
  RefPtr<FontPathSet> set = FontPathSet::GetNewerThan(nullptr, Timeout());
  while (set && set->IsStale()) {
    RefPtr<FontPathSet> newer = FontPathSet::GetNewerThan(set, Timeout());
    if (newer == set) {
      break;
    }
    set = newer.forget();
  }
  return set.forget();
}

TEST(FontPathSet, ContainsSystemFont)
{
  RefPtr<FontPathSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  EXPECT_TRUE(set->Contains(GetUpperCaseSystemFontPath(u"ARIAL.TTF"_ns)));
  EXPECT_FALSE(set->Contains(GetUpperCaseSystemFontPath(u"NOTAFONT.TTF"_ns)));
}

TEST(FontPathSet, MarkStaleRebuilds)
{
  RefPtr<FontPathSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  ASSERT_FALSE(set->IsStale());

  FontPathSet::MarkStale();
  EXPECT_TRUE(set->IsStale());

  RefPtr<FontPathSet> newSet = FontPathSet::GetNewerThan(set, Timeout());
  ASSERT_TRUE(newSet);
  EXPECT_NE(newSet, set);
  EXPECT_TRUE(newSet->Contains(GetUpperCaseSystemFontPath(u"ARIAL.TTF"_ns)));
}

TEST(FontPathSet, GetNewerThanTimesOut)
{
  RefPtr<FontPathSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  ASSERT_FALSE(set->IsStale());

  RefPtr<FontPathSet> sameSet =
      FontPathSet::GetNewerThan(set, TimeDuration::FromMilliseconds(50));
  EXPECT_EQ(sameSet, set);
  EXPECT_EQ(RefPtr<FontPathSet>(FontPathSet::GetCurrent()), set);
}
