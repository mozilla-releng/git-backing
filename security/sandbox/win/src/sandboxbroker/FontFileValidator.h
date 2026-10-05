/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_FONTFILEVALIDATOR_H_
#define SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_FONTFILEVALIDATOR_H_

#include <string_view>

#include "nsStringFwd.h"

namespace mozilla::sandboxing {

/**
 * Converts an NT path, as sent by a sandboxed process, into the upper cased
 * Win32 path form used by gfx::FontFileSet.
 * @return false if the path is not a plain drive letter path.
 */
bool NormalizeFontFilePath(std::wstring_view aNtPath,
                           nsAString& aUpperCasePath);

/**
 * @return true if an upper cased path has a common font file extension.
 */
bool HasFontFileExtension(const nsAString& aUpperCasePath);

/**
 * sandbox::FontFileValidatorFn that allows access to installed font files.
 * If the path is not found and the font file set is being rebuilt, this waits
 * for a limited time if the path has a font file extension.
 */
bool ValidateFontFile(std::wstring_view aNtPath);

}  // namespace mozilla::sandboxing

#endif  // SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_FONTFILEVALIDATOR_H_
