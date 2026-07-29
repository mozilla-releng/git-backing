/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ImageInputScan_h
#define mozilla_ImageInputScan_h

#include "ImageInputTelemetry.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

namespace mozilla {

namespace dom {
class BlobImpl;
}

// Reads a bounded prefix of one image file off the main thread, decides whether
// its metadata carries a position, and records a single image_input.offered
// event for it.
//
// Deliberately holds no reference to a document, window, event or browsing
// context. Nothing here can keep a page alive, and there is nothing to cancel
// when one goes away: the stream simply fails and the outcome is recorded as
// read_error.
class ImageInputScan final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ImageInputScan)

  // A page can be handed a whole directory of images. Reading a bounded prefix
  // of each is cheap but not free, so only the first few files of any one
  // selection have their bytes read. The rest still produce an event, marked
  // skipped_batch_limit, so nothing is silently dropped.
  static constexpr uint32_t kMaxScansPerAction = 8;

  // A ceiling on events from a single action, so that a page handing over
  // thousands of files cannot flood the IPC payload. Files past this are
  // visible as the gap between the event count and image_input.batch_size.
  static constexpr uint32_t kMaxEventsPerAction = 32;

  // Content process only, main thread. Callers are expected to have checked
  // that; the release assertion in the implementation is a backstop against a
  // future caller that has not.
  //
  // aDeclaredType must outlive the call, which it does because every label is a
  // static string.
  static void Start(dom::BlobImpl* aBlob, ImageInputSource aInputType,
                    const char* aDeclaredType);

  // Records an event for a file whose bytes were deliberately not read, so that
  // it still counts and its reason stays visible.
  static void RecordSkipped(ImageInputSource aInputType,
                            const char* aDeclaredType,
                            const char* aScanOutcome);

 private:
  ImageInputScan(ImageInputSource aInputType, const char* aDeclaredType,
                 uint64_t aFileSize)
      : mInputType(aInputType),
        mDeclaredType(aDeclaredType),
        mFileSize(aFileSize) {}
  ~ImageInputScan() = default;

  void OnBytes(nsresult aStatus, nsTArray<uint8_t>&& aBytes);

  const ImageInputSource mInputType;
  const char* const mDeclaredType;
  const uint64_t mFileSize;
};

}  // namespace mozilla

#endif  // mozilla_ImageInputScan_h
