/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ImageInputLabels_h
#define mozilla_ImageInputLabels_h

#include "ImageInputTelemetry.h"
#include "mozilla/image/EXIFScanner.h"
#include "nsLiteralString.h"

class nsIPrincipal;

// The mapping between what the scanner found and the strings that reach
// telemetry. Pure functions, kept apart from the machinery that calls them so
// that the rules below can be tested directly.
//
// Every one of these returns a literal, so assigning the result into the
// nsCString the event carries neither allocates nor copies.
namespace mozilla::imageinput {

nsLiteralCString InputTypeLabel(ImageInputSource aInputType);

nsLiteralCString DetectedFormatLabel(image::EXIFContainer aContainer);

nsLiteralCString ScanOutcomeLabel(image::EXIFScanOutcome aOutcome);

// Answers "does this file say where it was taken", honouring the asymmetry the
// scanner documents. A position that was found is conclusive however little of
// the file was read, because reading further cannot take one away. Not finding
// one only means something if the whole EXIF block was seen; otherwise the
// honest answer is that we do not know.
nsLiteralCString HasGPSLabel(const image::EXIFScanResult& aResult);

// Whether a document handing over files is a web page being given a photo.
//
// An allowlist rather than a denylist, because this decides what a probe that
// reads user file bytes will look at: an unfamiliar principal has to be added
// deliberately instead of arriving by default.
//
// Extension code is excluded by construction. A moz-extension: document is a
// content principal whose scheme is not on the list, and the expanded
// principal a content script sandbox carries is not a content principal at
// all. Neither leans on GetIsAddonOrExpandedAddonPrincipal, which needs a
// registered WebExtensionPolicy and so answers differently depending on
// whether the extension is still installed.
bool ShouldRecordFor(nsIPrincipal* aPrincipal);

// Whether a file is worth looking at: the platform called it an image, or its
// name says it is one.
//
// The extension fallback is not redundant. HEIC has no entry in
// netwerk/mime/nsMimeTypes.h and no extension mapping, so on some platforms
// File.type comes back empty for exactly the format most likely to carry a
// position. Gating on the MIME type alone, as the previous version of this
// probe did, made those files invisible.
bool IsCandidateImage(const nsACString& aType, const nsAString& aName);

}  // namespace mozilla::imageinput

#endif  // mozilla_ImageInputLabels_h
