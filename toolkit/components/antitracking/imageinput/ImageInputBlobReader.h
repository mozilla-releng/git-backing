/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ImageInputBlobReader_h
#define mozilla_ImageInputBlobReader_h

#include <functional>

#include "nsCOMPtr.h"
#include "nsIAsyncInputStream.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

class nsISerialEventTarget;

namespace mozilla {

namespace dom {
class BlobImpl;
}

// Reads a bounded range from the front of a blob without pulling the rest of it
// into memory and without disturbing any other stream over the same blob.
//
// BlobImpl::CreateSlice is what makes this cheap. For the StreamBlobImpl that
// every user-selected file becomes in a content process, it goes through
// nsICloneableInputStreamWithRange, so the range is applied on the far side
// before the stream comes back. CreateInputStream always clones as well, so the
// stream a page will eventually upload is never touched by our reading it.
class ImageInputBlobReader final : public nsIInputStreamCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINPUTSTREAMCALLBACK

  // Invoked on aTarget with the bytes read, or with a failure code and whatever
  // was read before it. Always invoked exactly once.
  using Callback = std::function<void(nsresult, nsTArray<uint8_t>&&)>;

  // Must be called on the main thread. Everything after the initial slicing
  // happens on aTarget, including the read itself.
  static void Read(dom::BlobImpl* aBlob, uint32_t aLength,
                   nsISerialEventTarget* aTarget, Callback&& aCallback);

 private:
  ImageInputBlobReader(uint32_t aLength, nsISerialEventTarget* aTarget,
                       Callback&& aCallback);
  ~ImageInputBlobReader() = default;

  void Finish(nsresult aStatus);

  nsCOMPtr<nsIAsyncInputStream> mStream;
  nsTArray<uint8_t> mBuffer;
  const uint32_t mWanted;
  nsCOMPtr<nsISerialEventTarget> mTarget;
  Callback mCallback;
};

}  // namespace mozilla

#endif  // mozilla_ImageInputBlobReader_h
