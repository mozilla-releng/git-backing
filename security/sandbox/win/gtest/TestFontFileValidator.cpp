/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <shlobj.h>
#include <windows.h>

#include "gtest/gtest.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/FontPathSet.h"
#include "nsString.h"

// clang-format off
#include "../src/sandboxbroker/FontFileValidator.h"
// clang-format on

using namespace mozilla;
using namespace mozilla::sandboxing;

static std::wstring GetSystemFontNtPath(const wchar_t* aFileName) {
  std::wstring path(LR"(\??\)");
  PWSTR fontsDir = nullptr;
  if (SUCCEEDED(
          ::SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fontsDir))) {
    path += fontsDir;
    path += L'\\';
    path += aFileName;
  }
  ::CoTaskMemFree(fontsDir);
  return path;
}

TEST(SandboxFontFileValidator, NormalizeStripsPrefixAndUpperCases)
{
  nsAutoString path;
  EXPECT_TRUE(
      NormalizeFontFilePath(LR"(\??\c:\Users\Moz\Fonts\font.ttf)", path));
  EXPECT_TRUE(path.EqualsLiteral(uR"(C:\USERS\MOZ\FONTS\FONT.TTF)"));
}

TEST(SandboxFontFileValidator, NormalizeRejectsNonDriveLetterPaths)
{
  nsAutoString path;
  EXPECT_FALSE(NormalizeFontFilePath(LR"(C:\Fonts\font.ttf)", path));
  EXPECT_FALSE(
      NormalizeFontFilePath(LR"(\??\UNC\server\share\font.ttf)", path));
  EXPECT_FALSE(NormalizeFontFilePath(LR"(\??\pipe\font.ttf)", path));
  EXPECT_FALSE(NormalizeFontFilePath(
      LR"(\Device\HarddiskVolume1\Fonts\font.ttf)", path));
  EXPECT_FALSE(NormalizeFontFilePath(LR"(\??\1:\Fonts\font.ttf)", path));
  EXPECT_FALSE(NormalizeFontFilePath(LR"(\??\C:font.ttf)", path));
  EXPECT_FALSE(NormalizeFontFilePath(LR"(\??\C:\)", path));
}

TEST(SandboxFontFileValidator, NormalizeRejectsParentDirs)
{
  nsAutoString path;
  EXPECT_FALSE(
      NormalizeFontFilePath(LR"(\??\C:\Fonts\..\Secret\font.ttf)", path));
  EXPECT_FALSE(NormalizeFontFilePath(LR"(\??\C:\Fonts\..)", path));
}

TEST(SandboxFontFileValidator, FontFileExtensions)
{
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.TTF)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.OTF)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.TTC)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.OTC)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.FON)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.FNT)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.DLL)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\TTF)"_ns));
}

TEST(SandboxFontFileValidator, ValidatesInstalledFonts)
{
  gfx::FontPathSet::Enable();

  EXPECT_TRUE(ValidateFontFile(GetSystemFontNtPath(L"arial.ttf")));
  EXPECT_FALSE(ValidateFontFile(GetSystemFontNtPath(L"notafont.ttf")));
}

TEST(SandboxFontFileValidator, NonFontExtensionMissDoesNotWait)
{
  gfx::FontPathSet::Enable();
  gfx::FontPathSet::MarkStale();

  TimeStamp start = TimeStamp::Now();
  EXPECT_FALSE(ValidateFontFile(GetSystemFontNtPath(L"notafont.dll")));
  EXPECT_LT((TimeStamp::Now() - start).ToSeconds(), 1.0);
}
