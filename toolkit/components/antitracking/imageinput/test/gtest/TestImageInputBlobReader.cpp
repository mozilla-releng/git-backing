/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageInputBlobReader.h"

#include "gtest/gtest.h"

#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/dom/BaseBlobImpl.h"
#include "nsIAsyncInputStream.h"
#include "nsISerialEventTarget.h"
#include "nsStreamUtils.h"
#include "nsThreadUtils.h"

// The reader's read loop only runs a second time when a stream hands its bytes
// over in more than one piece: a file backed by memory answers the whole
// request in one Read, so the crate's fixtures, all tens of bytes, never
// exercise it. This drives the reader with a stream that releases a fixed chunk
// per AsyncWait and asserts the bytes are reassembled whole and in order.

using namespace mozilla;
using namespace mozilla::dom;

namespace {

// An async stream that releases at most mChunk bytes per read, so a reader
// draining it has to come back through AsyncWait once per chunk.
class ChunkedAsyncStream final : public nsIAsyncInputStream {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  ChunkedAsyncStream(const nsACString& aBytes, uint32_t aChunk)
      : mMutex("ChunkedAsyncStream"), mBytes(aBytes), mChunk(aChunk) {}

  NS_IMETHOD Available(uint64_t* aResult) override {
    MutexAutoLock lock(mMutex);
    if (mClosed) {
      return NS_BASE_STREAM_CLOSED;
    }
    *aResult = std::min<uint64_t>(mChunk, mBytes.Length() - mPos);
    return NS_OK;
  }

  NS_IMETHOD StreamStatus() override {
    MutexAutoLock lock(mMutex);
    return mClosed ? NS_BASE_STREAM_CLOSED : NS_OK;
  }

  NS_IMETHOD Read(char* aBuffer, uint32_t aCount, uint32_t* aRead) override {
    MutexAutoLock lock(mMutex);
    if (mClosed) {
      return NS_BASE_STREAM_CLOSED;
    }
    uint32_t available = std::min<uint32_t>(mChunk, mBytes.Length() - mPos);
    uint32_t n = std::min(aCount, available);
    memcpy(aBuffer, mBytes.BeginReading() + mPos, n);
    mPos += n;
    *aRead = n;
    return NS_OK;
  }

  NS_IMETHOD ReadSegments(nsWriteSegmentFun, void*, uint32_t,
                          uint32_t*) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD IsNonBlocking(bool* aNonBlocking) override {
    *aNonBlocking = true;
    return NS_OK;
  }

  NS_IMETHOD Close() override { return CloseWithStatus(NS_OK); }

  NS_IMETHOD CloseWithStatus(nsresult aStatus) override {
    MutexAutoLock lock(mMutex);
    mClosed = true;
    return NS_OK;
  }

  NS_IMETHOD AsyncWait(nsIInputStreamCallback* aCallback, uint32_t, uint32_t,
                       nsIEventTarget* aEventTarget) override {
    {
      MutexAutoLock lock(mMutex);
      mCallback = aCallback;
    }
    if (!aCallback) {
      return NS_OK;
    }

    // Bytes are always ready here, so the wait is satisfied at once, on the
    // requested target the way a real async stream calls back.
    RefPtr<ChunkedAsyncStream> self = this;
    nsCOMPtr<nsIRunnable> notify = NS_NewRunnableFunction(
        "ChunkedAsyncStream::Notify", [self]() { self->Notify(); });
    if (aEventTarget) {
      return aEventTarget->Dispatch(notify.forget(), NS_DISPATCH_NORMAL);
    }
    return NS_DispatchToCurrentThread(notify.forget());
  }

 private:
  ~ChunkedAsyncStream() = default;

  void Notify() {
    nsCOMPtr<nsIInputStreamCallback> callback;
    {
      MutexAutoLock lock(mMutex);
      callback = mCallback.forget();
    }
    if (callback) {
      callback->OnInputStreamReady(this);
    }
  }

  Mutex mMutex;
  const nsCString mBytes MOZ_GUARDED_BY(mMutex);
  const uint32_t mChunk;
  size_t mPos MOZ_GUARDED_BY(mMutex) = 0;
  bool mClosed MOZ_GUARDED_BY(mMutex) = false;
  nsCOMPtr<nsIInputStreamCallback> mCallback MOZ_GUARDED_BY(mMutex);
};

NS_IMPL_ISUPPORTS(ChunkedAsyncStream, nsIInputStream, nsIAsyncInputStream)

// A blob whose stream is a ChunkedAsyncStream, so the reader sees a file that
// arrives in pieces.
class ChunkedBlobImpl final : public BaseBlobImpl {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(ChunkedBlobImpl, BaseBlobImpl)

  ChunkedBlobImpl(const nsACString& aBytes, uint32_t aChunk)
      : BaseBlobImpl(u"application/octet-stream"_ns, aBytes.Length()),
        mBytes(aBytes),
        mChunk(aChunk) {}

  void CreateInputStream(nsIInputStream** aStream,
                         ErrorResult& aRv) const override {
    RefPtr<ChunkedAsyncStream> stream = new ChunkedAsyncStream(mBytes, mChunk);
    stream.forget(aStream);
  }

  already_AddRefed<BlobImpl> CreateSlice(uint64_t aStart, uint64_t aLength,
                                         const nsAString&,
                                         ErrorResult&) const override {
    RefPtr<BlobImpl> slice = new ChunkedBlobImpl(
        Substring(mBytes, aStart, aLength), mChunk);
    return slice.forget();
  }

 private:
  ~ChunkedBlobImpl() override = default;

  const nsCString mBytes;
  const uint32_t mChunk;
};

struct ReadResult {
  Atomic<bool> mDone{false};
  nsresult mStatus = NS_ERROR_NOT_INITIALIZED;
  nsTArray<uint8_t> mBytes;
};

// Reads aRequested bytes from a aTotal-byte blob delivered aChunk at a time, and
// returns what the callback received once it has run.
void ReadChunked(uint32_t aTotal, uint32_t aRequested, uint32_t aChunk,
                 ReadResult& aResult) {
  nsCString bytes;
  bytes.SetLength(aTotal);
  for (uint32_t i = 0; i < aTotal; ++i) {
    bytes.BeginWriting()[i] = char(i & 0xFF);
  }

  RefPtr<BlobImpl> blob = new ChunkedBlobImpl(bytes, aChunk);

  nsCOMPtr<nsISerialEventTarget> queue;
  ASSERT_TRUE(NS_SUCCEEDED(
      NS_CreateBackgroundTaskQueue("TestImageInputBlobReader",
                                   getter_AddRefs(queue))));

  ImageInputBlobReader::Read(
      blob, aRequested, queue,
      [&aResult](nsresult aStatus, nsTArray<uint8_t>&& aBytes) {
        aResult.mStatus = aStatus;
        aResult.mBytes = std::move(aBytes);
        aResult.mDone = true;
      });

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil("TestImageInputBlobReader"_ns,
                                     [&aResult]() { return bool(aResult.mDone); }));
}

}  // namespace

// The whole file, handed over ten bytes at a time, comes back whole and ordered.
TEST(ImageInputBlobReader, ReassemblesAcrossManyChunks)
{
  ReadResult result;
  const uint32_t kTotal = 40000;
  ReadChunked(kTotal, kTotal, 4096, result);

  EXPECT_TRUE(NS_SUCCEEDED(result.mStatus));
  ASSERT_EQ(result.mBytes.Length(), kTotal);
  for (uint32_t i = 0; i < kTotal; ++i) {
    ASSERT_EQ(result.mBytes[i], uint8_t(i & 0xFF)) << "byte " << i;
  }
}

// Asking for less than the file holds stops at the requested length rather than
// draining the stream: the prefix read the scanner actually does.
TEST(ImageInputBlobReader, StopsAtTheRequestedPrefix)
{
  ReadResult result;
  const uint32_t kRequested = 16384;
  ReadChunked(200 * 1024, kRequested, 4096, result);

  EXPECT_TRUE(NS_SUCCEEDED(result.mStatus));
  ASSERT_EQ(result.mBytes.Length(), kRequested);
  for (uint32_t i = 0; i < kRequested; ++i) {
    ASSERT_EQ(result.mBytes[i], uint8_t(i & 0xFF)) << "byte " << i;
  }
}

// A file shorter than the prefix ends the read at the file's end, and the short
// result is not an error: a small image is perfectly readable.
TEST(ImageInputBlobReader, AShortFileEndsCleanly)
{
  ReadResult result;
  const uint32_t kTotal = 100;
  ReadChunked(kTotal, 128 * 1024, 4096, result);

  EXPECT_TRUE(NS_SUCCEEDED(result.mStatus));
  EXPECT_EQ(result.mBytes.Length(), kTotal);
}
