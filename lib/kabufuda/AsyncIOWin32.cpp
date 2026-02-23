#include "kabufuda/AsyncIO.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdio>

namespace kabufuda {

class WStringConv {
  std::wstring m_sys;

public:
  explicit WStringConv(std::string_view str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.size(), nullptr, 0);
    m_sys.assign(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), str.size(), &m_sys[0], len);
  }
  [[nodiscard]] std::wstring str() const { return m_sys; }
  [[nodiscard]] const wchar_t* c_str() const { return m_sys.c_str(); }
};

int Stat(const char* path, Sstat* statOut) {
  size_t pos;
  WStringConv wpath(path);
  const wchar_t* wpathP = wpath.c_str();
  for (pos = 0; pos < 3 && wpathP[pos] != L'\0'; ++pos) {}
  if (pos == 2 && wpathP[1] == L':') {
    wchar_t fixPath[4] = {wpathP[0], L':', L'/', L'\0'};
    return _wstat64(fixPath, statOut);
  }
  return _wstat64(wpath.c_str(), statOut);
}

struct AsyncIOInner {
  HANDLE m_fh = INVALID_HANDLE_VALUE;
  std::vector<std::pair<OVERLAPPED, SizeReturn>> m_queue;
};

static void ResetOverlapped(OVERLAPPED& aio, DWORD offset = 0) {
  aio.Internal = 0;
  aio.InternalHigh = 0;
  aio.Offset = offset;
  aio.OffsetHigh = 0;
}

AsyncIO::AsyncIO(std::string_view filename, bool truncate) : m_inner(new AsyncIOInner) {
#if WINDOWS_STORE
  CREATEFILE2_EXTENDED_PARAMETERS parms = {};
  parms.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
  parms.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
  parms.dwFileFlags = FILE_FLAG_OVERLAPPED;
  m_inner->m_fh = CreateFile2(filename.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              truncate ? CREATE_ALWAYS : OPEN_ALWAYS, &parms);
#else
  WStringConv wfilename(filename);
  m_inner->m_fh =
      CreateFileW(wfilename.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                  truncate ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL, nullptr);
#endif
}

AsyncIO::~AsyncIO() {
  if (*this) {
    if (CancelIoEx(m_inner->m_fh, nullptr))
      waitForCompletion();
    CloseHandle(m_inner->m_fh);
  }
  delete m_inner;
}

AsyncIO::AsyncIO(AsyncIO&& other) {
  if (*this) {
    if (CancelIoEx(m_inner->m_fh, nullptr))
      waitForCompletion();
    CloseHandle(m_inner->m_fh);
  }
  delete m_inner;
  m_inner = other.m_inner;
  other.m_inner = nullptr;
  m_maxBlock = other.m_maxBlock;
}

AsyncIO& AsyncIO::operator=(AsyncIO&& other) {
  if (*this) {
    if (CancelIoEx(m_inner->m_fh, nullptr))
      waitForCompletion();
    CloseHandle(m_inner->m_fh);
  }
  delete m_inner;
  m_inner = other.m_inner;
  other.m_inner = nullptr;
  m_maxBlock = other.m_maxBlock;
  return *this;
}

void AsyncIO::_waitForOperation(size_t qIdx) const {
  if (m_inner == nullptr) {
    return;
  }
  auto& aio = m_inner->m_queue[qIdx];
  if (aio.first.hEvent == 0)
    return;
  GetOverlappedResult(m_inner->m_fh, &aio.first, &aio.second, TRUE);
  CloseHandle(aio.first.hEvent);
  aio.first.hEvent = 0;
}

bool AsyncIO::asyncRead(size_t qIdx, void* buf, size_t length, off_t offset) {
  OVERLAPPED& aio = m_inner->m_queue[qIdx].first;
  if (aio.hEvent) {
#ifndef NDEBUG
    fprintf(stderr, "WARNING: synchronous kabufuda fallback, check access polling\n");
#endif
    _waitForOperation(qIdx);
  } else {
    aio.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  }
  ResetOverlapped(aio, DWORD(offset));
  m_maxBlock = std::max(m_maxBlock, qIdx + 1);
  BOOL res = ReadFile(m_inner->m_fh, buf, length, nullptr, &aio);
  return res == TRUE || GetLastError() == ERROR_IO_PENDING;
}

bool AsyncIO::asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset) {
  OVERLAPPED& aio = m_inner->m_queue[qIdx].first;
  if (aio.hEvent) {
#ifndef NDEBUG
    fprintf(stderr, "WARNING: synchronous kabufuda fallback, check access polling\n");
#endif
    _waitForOperation(qIdx);
  } else {
    aio.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  }
  ResetOverlapped(aio, DWORD(offset));
  m_maxBlock = std::max(m_maxBlock, qIdx + 1);
  BOOL res = WriteFile(m_inner->m_fh, buf, length, nullptr, &aio);
  return res == TRUE || GetLastError() == ERROR_IO_PENDING;
}

ECardResult AsyncIO::pollStatus(size_t qIdx, SizeReturn* szRet) const {
  auto& aio = m_inner->m_queue[qIdx];
  if (aio.first.hEvent == 0) {
    if (szRet)
      *szRet = aio.second;
    return ECardResult::READY;
  }
  if (GetOverlappedResult(m_inner->m_fh, &aio.first, &aio.second, FALSE)) {
    CloseHandle(aio.first.hEvent);
    aio.first.hEvent = 0;
    if (szRet)
      *szRet = aio.second;
    return ECardResult::READY;
  } else {
    if (GetLastError() == ERROR_IO_INCOMPLETE) {
      return ECardResult::BUSY;
    } else {
      _waitForOperation(qIdx);
      return ECardResult::IOERROR;
    }
  }
}

ECardResult AsyncIO::pollStatus() const {
  ECardResult result = ECardResult::READY;
  for (auto it = m_inner->m_queue.begin(); it != m_inner->m_queue.begin() + m_maxBlock; ++it) {
    auto& aio = *it;
    if (aio.first.hEvent == 0)
      continue;
    if (GetOverlappedResult(m_inner->m_fh, &aio.first, &aio.second, FALSE)) {
      CloseHandle(aio.first.hEvent);
      aio.first.hEvent = 0;
    } else {
      if (GetLastError() == ERROR_IO_INCOMPLETE) {
        if (result > ECardResult::BUSY)
          result = ECardResult::BUSY;
      } else {
        _waitForOperation(it - m_inner->m_queue.cbegin());
        if (result > ECardResult::IOERROR)
          result = ECardResult::IOERROR;
      }
    }
  }
  if (result == ECardResult::READY)
    m_maxBlock = 0;
  return result;
}

void AsyncIO::waitForCompletion() const {
  for (size_t i = 0; i < m_maxBlock; ++i)
    _waitForOperation(i);
  m_maxBlock = 0;
}

void AsyncIO::resizeQueue(size_t queueSz) {
  if (m_inner == nullptr) {
    return;
  }
  m_inner->m_queue.resize(queueSz);
}

AsyncIO::operator bool() const { return m_inner != nullptr && m_inner->m_fh != INVALID_HANDLE_VALUE; }

} // namespace kabufuda
