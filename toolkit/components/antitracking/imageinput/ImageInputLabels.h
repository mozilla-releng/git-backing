/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ImageInputLabels_h
#define mozilla_ImageInputLabels_h

#include "ImageInputTelemetry.h"
#include "mozilla/image/EXIFScanner.h"
#include "nsStringFwd.h"

// The mapping between what the scanner found and the strings that reach
// telemetry. Pure functions, kept apart from the machinery that calls them so
// that the rules below can be tested directly.
//
// Every one of these returns a pointer to a static string, so the result can be
// held without copying and outlives any caller.
namespace mozilla::imageinput {

const char* InputTypeLabel(ImageInputSource aInputType);

const char* DetectedFormatLabel(image::EXIFContainer aContainer);

const char* ScanOutcomeLabel(image::EXIFScanOutcome aOutcome);

// Answers "does this file say where it was taken", honouring the asymmetry the
// scanner documents. A position that was found is conclusive however little of
// the file was read, because reading further cannot take one away. Not finding
// one only means something if the whole EXIF block was seen; otherwise the
// honest answer is that we do not know.
const char* HasGPSLabel(const image::EXIFScanResult& aResult);

// File.type as the platform reported it, mapped onto a closed set so that the
// telemetry key cannot grow unbounded. A page can construct a File with an
// arbitrary type string, so nothing is passed through verbatim.
const char* DeclaredTypeLabel(const nsACString& aType);

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
