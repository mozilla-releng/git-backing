/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <dwrite.h>
#include <shlobj.h>
#include <windows.h>

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/FontPathSet.h"

using namespace mozilla;
using namespace mozilla::gfx;

static TimeStamp Deadline() {
  return TimeStamp::Now() + TimeDuration::FromSeconds(30);
}

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

// The font list normally does this, but it might not have been initialized.
static void RebuildFromSystemFonts() {
  ASSERT_TRUE(Factory::EnsureDWriteFactory());
  RefPtr<IDWriteFontCollection> systemFonts = Factory::GetDWriteSystemFonts();
  ASSERT_TRUE(systemFonts);
  FontPathSet::RebuildFrom(systemFonts);
}

static already_AddRefed<FontPathSet> GetUpToDateSet() {
  if (!FontPathSet::Enable()) {
    return nullptr;
  }

  RebuildFromSystemFonts();
  TimeStamp deadline = Deadline();
  RefPtr<FontPathSet> set = FontPathSet::GetNewerThan(nullptr, deadline);
  while (set && set->IsStale()) {
    set = FontPathSet::GetNewerThan(set, deadline);
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

TEST(FontPathSet, RebuildFromRebuilds)
{
  RefPtr<FontPathSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  ASSERT_FALSE(set->IsStale());

  RebuildFromSystemFonts();
  EXPECT_TRUE(set->IsStale());

  RefPtr<FontPathSet> newSet = FontPathSet::GetNewerThan(set, Deadline());
  ASSERT_TRUE(newSet);
  EXPECT_NE(newSet, set);
  EXPECT_TRUE(newSet->Contains(GetUpperCaseSystemFontPath(u"ARIAL.TTF"_ns)));
}

TEST(FontPathSet, GetNewerThanNotStale)
{
  RefPtr<FontPathSet> set = GetUpToDateSet();
  ASSERT_TRUE(set);
  ASSERT_FALSE(set->IsStale());

  EXPECT_FALSE(RefPtr<FontPathSet>(FontPathSet::GetNewerThan(set, Deadline())));
  EXPECT_EQ(RefPtr<FontPathSet>(FontPathSet::GetCurrent()), set);
}
