/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ImageInputTelemetry_h
#define mozilla_ImageInputTelemetry_h

#include <stdint.h>

#include "mozilla/RefPtr.h"
#include "nsTArray.h"

class nsIPrincipal;

namespace mozilla {

namespace dom {
class DataTransfer;
class File;
}  // namespace dom

// How a file reached the page.
enum class ImageInputSource : uint8_t {
  FilePicker,
  DirectoryPicker,
  Drop,
  Paste,
};

// Records how many image files web pages are given, in what format, and whether
// their EXIF metadata says where they were taken.
//
// This measures the moment a file is handed to content, which is when page
// script gains read access to its bytes, rather than any later upload. A page
// can read a File's contents with FileReader or Blob.arrayBuffer and never make
// a network request at all, so selection is where the data actually crosses
// over, and it is also the only point at which anything could be stripped.
//
// Reading the file is bounded, asynchronous and off the main thread, and
// happens only in content processes.
class ImageInputTelemetry {
 public:
  // For the file and folder pickers, where the files are already in hand.
  static void MaybeRecordFiles(ImageInputSource aInputType,
                               const nsTArray<RefPtr<dom::File>>& aFiles,
                               nsIPrincipal* aPrincipal);

  // For drop and paste. Materialising a transfer's file list can mean a
  // synchronous read from the system clipboard, so this checks that there is
  // one to materialise before asking for it.
  static void MaybeRecordDataTransfer(ImageInputSource aInputType,
                                      dom::DataTransfer* aDataTransfer,
                                      nsIPrincipal* aPrincipal);
};

}  // namespace mozilla

#endif  // mozilla_ImageInputTelemetry_h
