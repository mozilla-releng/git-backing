/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../src/sandboxbroker/ChildOutputManager.h"
#include "gtest/gtest.h"
#include "mozilla/Sprintf.h"
#include "mozilla/UniquePtrExtensions.h"

namespace mozilla {

namespace {

bool Contains(const std::vector<HANDLE>& aHandles, HANDLE aHandle) {
  return std::find(aHandles.begin(), aHandles.end(), aHandle) != aHandles.end();
}

class TestChildOutputManager : public ChildOutputManager {
 public:
  TestChildOutputManager(HANDLE aStdout, HANDLE aStderr,
                         const std::vector<HANDLE>& aConsoles)
      : ChildOutputManager(aStdout, aStderr, Contains(aConsoles, aStdout),
                           Contains(aConsoles, aStderr)) {}

  using ChildOutputManager::ChildStderr;
  using ChildOutputManager::ChildStdout;
  using ChildOutputManager::IsConsoleHandle;
  using ChildOutputManager::IsRelaying;
};

struct Pipe {
  UniqueFileHandle mRead;
  UniqueFileHandle mWrite;
};

}  // namespace

static HANDLE Raw(const UniqueFileHandle& aHandle) { return aHandle.get(); }

static Pipe CreateTestPipe() {
  HANDLE read;
  HANDLE write;
  if (!::CreatePipe(&read, &write, nullptr, 0)) {
    return {};
  }
  return {UniqueFileHandle(read), UniqueFileHandle(write)};
}

static bool WriteAll(HANDLE aHandle, const std::string& aData) {
  DWORD offset = 0;
  while (offset < aData.size()) {
    DWORD written;
    if (!::WriteFile(aHandle, aData.data() + offset,
                     static_cast<DWORD>(aData.size()) - offset, &written,
                     nullptr)) {
      return false;
    }
    offset += written;
  }
  return true;
}

static std::string ReadExactly(HANDLE aHandle, size_t aLength) {
  std::string data(aLength, '\0');
  size_t offset = 0;
  while (offset < aLength) {
    DWORD read;
    if (!::ReadFile(aHandle, data.data() + offset,
                    static_cast<DWORD>(aLength - offset), &read, nullptr)) {
      break;
    }
    offset += read;
  }
  data.resize(offset);
  return data;
}

static bool SameObject(HANDLE aFirst, HANDLE aSecond) {
  using CompareObjectHandlesFn = BOOL(WINAPI*)(HANDLE, HANDLE);
  static auto compareObjectHandles =
      reinterpret_cast<CompareObjectHandlesFn>(::GetProcAddress(
          ::GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles"));
  return compareObjectHandles && compareObjectHandles(aFirst, aSecond);
}

static DWORD BytesAvailable(HANDLE aPipeRead) {
  DWORD available = 0;
  ::PeekNamedPipe(aPipeRead, nullptr, 0, nullptr, &available, nullptr);
  return available;
}

TEST(ChildOutputManager, IsConsoleHandleRejectsNonConsoles)
{
  EXPECT_FALSE(TestChildOutputManager::IsConsoleHandle(nullptr));
  EXPECT_FALSE(TestChildOutputManager::IsConsoleHandle(INVALID_HANDLE_VALUE));

  // NUL is a character device like a console, so must be told apart.
  UniqueFileHandle nul(::CreateFileW(L"NUL", GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr));
  ASSERT_TRUE(nul);
  EXPECT_EQ(::GetFileType(nul.get()), static_cast<DWORD>(FILE_TYPE_CHAR));
  EXPECT_FALSE(TestChildOutputManager::IsConsoleHandle(nul.get()));

  Pipe pipe = CreateTestPipe();
  ASSERT_TRUE(pipe.mWrite);
  EXPECT_FALSE(TestChildOutputManager::IsConsoleHandle(pipe.mWrite.get()));

  wchar_t tempDir[MAX_PATH];
  wchar_t tempFile[MAX_PATH];
  ASSERT_NE(::GetTempPathW(MAX_PATH, tempDir), 0u);
  ASSERT_NE(::GetTempFileNameW(tempDir, L"cor", 0, tempFile), 0u);
  UniqueFileHandle file(::CreateFileW(tempFile, GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE,
                                      nullptr));
  ASSERT_TRUE(file);
  EXPECT_FALSE(TestChildOutputManager::IsConsoleHandle(file.get()));
}

TEST(ChildOutputManager, IsConsoleHandleAcceptsConsole)
{
  // Use the process's console if it has one, otherwise create a hidden one,
  // restoring the std handles that AllocConsole replaces afterwards.
  HANDLE savedStdHandles[] = {::GetStdHandle(STD_INPUT_HANDLE),
                              ::GetStdHandle(STD_OUTPUT_HANDLE),
                              ::GetStdHandle(STD_ERROR_HANDLE)};
  bool allocated = false;
  if (!::GetConsoleWindow()) {
    if (!::AllocConsole()) {
      GTEST_SKIP() << "Unable to create a console";
    }
    allocated = true;
    ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
  }

  UniqueFileHandle console(
      ::CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, 0, nullptr));
  bool isConsole =
      console && TestChildOutputManager::IsConsoleHandle(console.get());
  console = nullptr;

  if (allocated) {
    ::FreeConsole();
    ::SetStdHandle(STD_INPUT_HANDLE, savedStdHandles[0]);
    ::SetStdHandle(STD_OUTPUT_HANDLE, savedStdHandles[1]);
    ::SetStdHandle(STD_ERROR_HANDLE, savedStdHandles[2]);
  }

  EXPECT_TRUE(isConsole);
}

static void ExpectInheritable(HANDLE aHandle) {
  DWORD flags = 0;
  ASSERT_TRUE(::GetHandleInformation(aHandle, &flags));
  EXPECT_TRUE(flags & HANDLE_FLAG_INHERIT);
}

static void ExpectPipeHandleCanOnlyWrite(HANDLE aHandle) {
  ExpectInheritable(aHandle);

  DWORD mode = PIPE_NOWAIT;
  EXPECT_FALSE(::SetNamedPipeHandleState(aHandle, &mode, nullptr, nullptr));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_ACCESS_DENIED));

  char buffer[1];
  DWORD read;
  EXPECT_FALSE(::ReadFile(aHandle, buffer, sizeof(buffer), &read, nullptr));
}

TEST(ChildOutputManager, RejectsInvalidHandles)
{
  TestChildOutputManager relay(nullptr, INVALID_HANDLE_VALUE, {});
  EXPECT_FALSE(relay.IsRelaying());
  EXPECT_EQ(relay.ChildStdout(), nullptr);
  EXPECT_EQ(relay.ChildStderr(), nullptr);
}

TEST(ChildOutputManager, RejectsCharacterDeviceHandles)
{
  UniqueFileHandle nul(::CreateFileW(L"NUL", GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr));
  ASSERT_TRUE(nul);

  TestChildOutputManager relay(nul.get(), nul.get(), {});
  EXPECT_FALSE(relay.IsRelaying());
  EXPECT_EQ(relay.ChildStdout(), nullptr);
  EXPECT_EQ(relay.ChildStderr(), nullptr);
}

TEST(ChildOutputManager, RestrictsPipeHandles)
{
  Pipe stdoutPipe = CreateTestPipe();
  Pipe stderrPipe = CreateTestPipe();
  ASSERT_TRUE(stdoutPipe.mWrite && stderrPipe.mWrite);

  TestChildOutputManager relay(stdoutPipe.mWrite.get(), stderrPipe.mWrite.get(),
                               {});
  EXPECT_FALSE(relay.IsRelaying());
  ASSERT_TRUE(relay.ChildStdout());
  ASSERT_TRUE(relay.ChildStderr());
  EXPECT_NE(relay.ChildStdout(), Raw(stdoutPipe.mWrite));
  EXPECT_NE(relay.ChildStderr(), Raw(stderrPipe.mWrite));

  const std::string outMessage = "to stdout";
  const std::string errMessage = "to stderr";
  EXPECT_TRUE(WriteAll(relay.ChildStdout(), outMessage));
  EXPECT_TRUE(WriteAll(relay.ChildStderr(), errMessage));
  EXPECT_EQ(ReadExactly(stdoutPipe.mRead.get(), outMessage.size()), outMessage);
  EXPECT_EQ(ReadExactly(stderrPipe.mRead.get(), errMessage.size()), errMessage);

  ExpectPipeHandleCanOnlyWrite(relay.ChildStdout());
  ExpectPipeHandleCanOnlyWrite(relay.ChildStderr());
}

TEST(ChildOutputManager, SharesChildHandleForSameParentHandle)
{
  Pipe pipe = CreateTestPipe();
  ASSERT_TRUE(pipe.mWrite);

  TestChildOutputManager relay(pipe.mWrite.get(), pipe.mWrite.get(), {});
  ASSERT_TRUE(relay.ChildStdout());
  EXPECT_NE(relay.ChildStdout(), Raw(pipe.mWrite));
  EXPECT_TRUE(SameObject(relay.ChildStdout(), relay.ChildStderr()));
  ExpectPipeHandleCanOnlyWrite(relay.ChildStderr());
}

static void TestFileHandleCanOnlyAppend(DWORD aParentAccess) {
  wchar_t tempDir[MAX_PATH];
  wchar_t tempFile[MAX_PATH];
  ASSERT_NE(::GetTempPathW(MAX_PATH, tempDir), 0u);
  ASSERT_NE(::GetTempFileNameW(tempDir, L"cor", 0, tempFile), 0u);
  constexpr DWORD kShareAll =
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  UniqueFileHandle parentFile(
      ::CreateFileW(tempFile, aParentAccess, kShareAll, nullptr, CREATE_ALWAYS,
                    FILE_FLAG_DELETE_ON_CLOSE, nullptr));
  ASSERT_TRUE(parentFile);

  TestChildOutputManager relay(parentFile.get(), parentFile.get(), {});
  HANDLE childFile = relay.ChildStdout();
  ASSERT_TRUE(childFile);
  EXPECT_NE(childFile, Raw(parentFile));
  ExpectInheritable(childFile);

  // The parent and child share the file position, so their output interleaves.
  EXPECT_TRUE(WriteAll(parentFile.get(), "parent1 "));
  EXPECT_TRUE(WriteAll(childFile, "child1 "));
  EXPECT_TRUE(WriteAll(parentFile.get(), "parent2 "));

  EXPECT_NE(::SetFilePointer(childFile, 0, nullptr, FILE_BEGIN),
            INVALID_SET_FILE_POINTER);
  EXPECT_TRUE(WriteAll(childFile, "child2"));
  EXPECT_NE(::SetFilePointer(childFile, 0, nullptr, FILE_BEGIN),
            INVALID_SET_FILE_POINTER);
  EXPECT_FALSE(::SetEndOfFile(childFile));

  char buffer[1];
  DWORD read;
  EXPECT_FALSE(::ReadFile(childFile, buffer, sizeof(buffer), &read, nullptr));

  UniqueFileHandle reader(::CreateFileW(tempFile, GENERIC_READ, kShareAll,
                                        nullptr, OPEN_EXISTING, 0, nullptr));
  ASSERT_TRUE(reader);
  const std::string expected = "parent1 child1 parent2 child2";
  EXPECT_EQ(::GetFileSize(reader.get(), nullptr), expected.size());
  EXPECT_EQ(ReadExactly(reader.get(), expected.size()), expected);
}

TEST(ChildOutputManager, RestrictsFileHandleToAppend)
{
  TestFileHandleCanOnlyAppend(GENERIC_READ | GENERIC_WRITE);
}

TEST(ChildOutputManager, RestrictsWriteOnlyFileHandleToAppend)
{
  TestFileHandleCanOnlyAppend(GENERIC_WRITE);
}

TEST(ChildOutputManager, ReplacesOnlyConsoleHandles)
{
  Pipe console = CreateTestPipe();
  Pipe other = CreateTestPipe();
  ASSERT_TRUE(console.mWrite && other.mWrite);

  TestChildOutputManager relay(other.mWrite.get(), console.mWrite.get(),
                               {console.mWrite.get()});
  ASSERT_TRUE(relay.IsRelaying());
  ASSERT_TRUE(relay.ChildStdout());
  ASSERT_TRUE(relay.ChildStderr());
  EXPECT_FALSE(SameObject(relay.ChildStdout(), relay.ChildStderr()));

  const std::string outMessage = "direct";
  const std::string errMessage = "relayed";
  EXPECT_TRUE(WriteAll(relay.ChildStdout(), outMessage));
  EXPECT_TRUE(WriteAll(relay.ChildStderr(), errMessage));
  EXPECT_EQ(ReadExactly(other.mRead.get(), outMessage.size()), outMessage);
  EXPECT_EQ(ReadExactly(console.mRead.get(), errMessage.size()), errMessage);
}

TEST(ChildOutputManager, RelayHandleCanOnlyWrite)
{
  Pipe console = CreateTestPipe();
  ASSERT_TRUE(console.mWrite);

  TestChildOutputManager relay(console.mWrite.get(), console.mWrite.get(),
                               {console.mWrite.get()});
  ASSERT_TRUE(relay.IsRelaying());
  ASSERT_TRUE(relay.ChildStdout());

  const std::string message = "write works";
  EXPECT_TRUE(WriteAll(relay.ChildStdout(), message));
  EXPECT_EQ(ReadExactly(console.mRead.get(), message.size()), message);

  ExpectPipeHandleCanOnlyWrite(relay.ChildStdout());
}

TEST(ChildOutputManager, PrefersStderrConsole)
{
  Pipe stdoutConsole = CreateTestPipe();
  Pipe stderrConsole = CreateTestPipe();
  ASSERT_TRUE(stdoutConsole.mWrite && stderrConsole.mWrite);

  const std::string message = "to stderr";
  {
    TestChildOutputManager relay(
        stdoutConsole.mWrite.get(), stderrConsole.mWrite.get(),
        {stdoutConsole.mWrite.get(), stderrConsole.mWrite.get()});
    ASSERT_TRUE(relay.IsRelaying());
    ASSERT_TRUE(relay.ChildStdout());
    EXPECT_TRUE(SameObject(relay.ChildStdout(), relay.ChildStderr()));
    EXPECT_TRUE(WriteAll(relay.ChildStdout(), message));
    EXPECT_EQ(ReadExactly(stderrConsole.mRead.get(), message.size()), message);
  }

  EXPECT_EQ(BytesAvailable(stdoutConsole.mRead.get()), 0u);
}

TEST(ChildOutputManager, RelaysLargeOutputInOrder)
{
  Pipe console = CreateTestPipe();
  ASSERT_TRUE(console.mWrite);

  // Larger than the relay's pipe and read buffers, so that both fill up.
  std::string expected(256 * 1024, '\0');
  for (size_t i = 0; i < expected.size(); ++i) {
    expected[i] = static_cast<char>('a' + (i % 26));
  }

  TestChildOutputManager relay(nullptr, console.mWrite.get(),
                               {console.mWrite.get()});
  HANDLE childHandle = relay.ChildStderr();
  ASSERT_TRUE(childHandle);

  bool writeOk = false;
  std::thread writer([&] {
    for (size_t offset = 0; offset < expected.size(); offset += 1000) {
      if (!WriteAll(childHandle, expected.substr(offset, 1000))) {
        return;
      }
    }
    writeOk = true;
  });
  std::string received = ReadExactly(console.mRead.get(), expected.size());
  writer.join();

  EXPECT_TRUE(writeOk);
  EXPECT_EQ(received.size(), expected.size());
  EXPECT_TRUE(received == expected);
}

TEST(ChildOutputManager, RelaysOutputFromMultipleChildren)
{
  Pipe console = CreateTestPipe();
  ASSERT_TRUE(console.mWrite);

  TestChildOutputManager relay(nullptr, console.mWrite.get(),
                               {console.mWrite.get()});
  HANDLE childHandle = relay.ChildStderr();
  ASSERT_TRUE(childHandle);

  constexpr int kChildren = 2;
  constexpr int kLines = 1000;
  // Each line is "<child> <line number>\n" padded to a fixed length.
  constexpr size_t kLineLength = 16;

  std::vector<std::thread> writers;
  for (int i = 0; i < kChildren; ++i) {
    writers.emplace_back([&, i] {
      for (int line = 0; line < kLines; ++line) {
        char buffer[kLineLength + 1];
        SprintfLiteral(buffer, "%d %-13d\n", i, line);
        if (!WriteAll(childHandle, buffer)) {
          return;
        }
      }
    });
  }
  std::string received =
      ReadExactly(console.mRead.get(), kChildren * kLines * kLineLength);
  for (auto& writer : writers) {
    writer.join();
  }

  ASSERT_EQ(received.size(), kChildren * kLines * kLineLength);
  int nextLine[kChildren] = {};
  for (size_t offset = 0; offset < received.size(); offset += kLineLength) {
    int child = -1;
    int line = -1;
    ASSERT_EQ(sscanf(received.c_str() + offset, "%d %d", &child, &line), 2);
    ASSERT_GE(child, 0);
    ASSERT_LT(child, kChildren);
    EXPECT_EQ(line, nextLine[child]);
    nextLine[child] = line + 1;
  }
  for (int i = 0; i < kChildren; ++i) {
    EXPECT_EQ(nextLine[i], kLines);
  }
}

}  // namespace mozilla
