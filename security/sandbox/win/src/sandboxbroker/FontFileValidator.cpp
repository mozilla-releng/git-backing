/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontFileValidator.h"

#include <windows.h>

#include "mozilla/RefPtr.h"
#include "mozilla/TextUtils.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/FontPathSet.h"
#include "nsString.h"

namespace mozilla::sandboxing {

using gfx::FontPathSet;

static constexpr std::wstring_view kNTPrefix = LR"(\??\)";

// Maximum time to wait for the font path set to be rebuilt, when a path with a
// font file extension is not found.
static constexpr uint32_t kMaxRebuildWaitSeconds = 3;

bool NormalizeFontFilePath(std::wstring_view aNtPath,
                           nsAString& aUpperCasePath) {
  if (!aNtPath.starts_with(kNTPrefix)) {
    return false;
  }

  std::wstring_view path = aNtPath.substr(kNTPrefix.length());
  if (path.length() < 4 || !IsAsciiAlpha(static_cast<char16_t>(path[0])) ||
      path[1] != L':' || path[2] != L'\\') {
    return false;
  }

  if (path.find(LR"(\..\)") != std::wstring_view::npos ||
      path.ends_with(LR"(\..)")) {
    return false;
  }

  aUpperCasePath.Assign(reinterpret_cast<const char16_t*>(path.data()),
                        path.length());
  ::CharUpperBuffW(reinterpret_cast<WCHAR*>(aUpperCasePath.BeginWriting()),
                   aUpperCasePath.Length());
  return true;
}

bool HasFontFileExtension(const nsAString& aUpperCasePath) {
  static constexpr nsLiteralString kFontFileExtensions[] = {
      u".TTF"_ns, u".OTF"_ns, u".TTC"_ns, u".OTC"_ns, u".FON"_ns, u".FNT"_ns,
  };

  for (const auto& extension : kFontFileExtensions) {
    if (StringEndsWith(aUpperCasePath, extension)) {
      return true;
    }
  }

  return false;
}

bool ValidateFontFile(std::wstring_view aNtPath) {
  nsAutoString path;
  if (!NormalizeFontFilePath(aNtPath, path)) {
    return false;
  }

  RefPtr<FontPathSet> set = FontPathSet::GetCurrent();
  if (set) {
    if (set->Contains(path)) {
      return true;
    }

    if (!set->IsStale()) {
      return false;
    }
  }

  // The set is missing or being rebuilt, so wait for a newer one, but only for
  // paths that are likely to be fonts.
  if (!HasFontFileExtension(path)) {
    return false;
  }

  TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromSeconds(kMaxRebuildWaitSeconds);
  while (true) {
    TimeDuration remaining = deadline - TimeStamp::Now();
    if (remaining <= TimeDuration()) {
      return false;
    }

    RefPtr<FontPathSet> newerSet = FontPathSet::GetNewerThan(set, remaining);
    if (!newerSet || newerSet == set) {
      return false;
    }

    if (newerSet->Contains(path)) {
      return true;
    }

    if (!newerSet->IsStale()) {
      return false;
    }

    set = std::move(newerSet);
  }
}

}  // namespace mozilla::sandboxing
