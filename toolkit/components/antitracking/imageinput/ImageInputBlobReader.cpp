/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputBlobReader.h"

#include <algorithm>

#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BlobImpl.h"
#include "nsIInputStream.h"
#include "nsISerialEventTarget.h"
#include "nsStreamUtils.h"
#include "nsThreadUtils.h"

namespace mozilla {

NS_IMPL_ISUPPORTS(ImageInputBlobReader, nsIInputStreamCallback)

ImageInputBlobReader::ImageInputBlobReader(uint32_t aLength,
                                           nsISerialEventTarget* aTarget,
                                           Callback&& aCallback)
    : mWanted(aLength), mTarget(aTarget), mCallback(std::move(aCallback)) {}

/* static */
void ImageInputBlobReader::Read(dom::BlobImpl* aBlob, uint32_t aLength,
                                nsISerialEventTarget* aTarget,
                                Callback&& aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aBlob);
  MOZ_ASSERT(aTarget);

  RefPtr<ImageInputBlobReader> reader =
      new ImageInputBlobReader(aLength, aTarget, std::move(aCallback));

  // Slicing first is the whole point: for a blob backed by a file in another
  // process this turns into a ranged request, so only the bytes we asked for
  // are ever produced.
  // The content type of the slice is irrelevant: nothing but its bytes is
  // read, and the format is determined from those bytes rather than from any
  // type a caller might attach.
  IgnoredErrorResult rv;
  RefPtr<dom::BlobImpl> slice = aBlob->CreateSlice(0, aLength, u""_ns, rv);
  if (rv.Failed() || !slice) {
    reader->Finish(NS_ERROR_FAILURE);
    return;
  }

  nsCOMPtr<nsIInputStream> stream;
  slice->CreateInputStream(getter_AddRefs(stream), rv);
  if (rv.Failed() || !stream) {
    reader->Finish(NS_ERROR_FAILURE);
    return;
  }

  // A blob may hand back either a synchronous stream, when its bytes are
  // already in this process, or an asynchronous one that has to fetch them.
  // This normalises the two so the read loop below only has to handle the
  // asynchronous case.
  nsresult nrv = NS_MakeAsyncNonBlockingInputStream(
      stream.forget(), getter_AddRefs(reader->mStream));
  if (NS_FAILED(nrv) || !reader->mStream) {
    reader->Finish(NS_FAILED(nrv) ? nrv : NS_ERROR_FAILURE);
    return;
  }

  // From here on everything, including the read itself, is off the main thread.
  nrv = reader->mStream->AsyncWait(reader, 0, aLength, aTarget);
  if (NS_FAILED(nrv)) {
    reader->Finish(nrv);
  }
}

NS_IMETHODIMP
ImageInputBlobReader::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  MOZ_ASSERT(mStream == aStream);

  // AsyncWait only calls back once the stream has data or has closed, so
  // nothing being available here means the file ended. That is ordinary: a file
  // shorter than the range we asked for is still perfectly readable.
  uint64_t available = 0;
  nsresult rv = aStream->Available(&available);
  if (NS_FAILED(rv) || available == 0) {
    Finish(rv == NS_BASE_STREAM_CLOSED ? NS_OK : rv);
    return NS_OK;
  }

  const uint32_t remaining = mWanted - uint32_t(mBuffer.Length());
  const uint32_t wanted = uint32_t(std::min<uint64_t>(available, remaining));

  // Grow the buffer and read straight into the new tail, then shrink to what
  // was actually read. NS_ConsumeStream is deliberately not used: on a failed
  // Read it leaves the tail it had already grown filled with indeterminate
  // bytes, and those would then be parsed. Reading in a single pass and
  // truncating to the returned count keeps only bytes that came from the file.
  // One Available may still take several passes to drain, which is why this
  // loops on AsyncWait.
  const size_t start = mBuffer.Length();
  if (!mBuffer.SetLength(start + wanted, fallible)) {
    Finish(NS_ERROR_OUT_OF_MEMORY);
    return NS_OK;
  }

  uint32_t read = 0;
  rv = aStream->Read(reinterpret_cast<char*>(mBuffer.Elements() + start),
                     wanted, &read);
  mBuffer.TruncateLength(start + (NS_SUCCEEDED(rv) ? read : 0));

  if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
    Finish(rv == NS_BASE_STREAM_CLOSED ? NS_OK : rv);
    return NS_OK;
  }

  if (mBuffer.Length() >= mWanted) {
    Finish(NS_OK);
    return NS_OK;
  }

  rv = mStream->AsyncWait(this, 0, mWanted - uint32_t(mBuffer.Length()),
                          mTarget);
  if (NS_FAILED(rv)) {
    Finish(rv);
  }
  return NS_OK;
}

void ImageInputBlobReader::Finish(nsresult aStatus) {
  if (mStream) {
    mStream->CloseWithStatus(NS_BASE_STREAM_CLOSED);
    mStream = nullptr;
  }

  if (!mCallback) {
    return;
  }

  Callback callback = std::move(mCallback);
  mCallback = nullptr;

  // Read() can fail before anything has been dispatched, in which case we are
  // still on the main thread and the caller expects to hear back on aTarget.
  if (mTarget->IsOnCurrentThread()) {
    callback(aStatus, std::move(mBuffer));
    return;
  }

  RefPtr<ImageInputBlobReader> self = this;
  nsTArray<uint8_t> buffer = std::move(mBuffer);
  mTarget->Dispatch(
      NS_NewRunnableFunction("ImageInputBlobReader::Finish",
                             [self, aStatus, callback = std::move(callback),
                              buffer = std::move(buffer)]() mutable {
                               callback(aStatus, std::move(buffer));
                             }));
}

}  // namespace mozilla
