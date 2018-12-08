#include "kabufuda/AsyncIO.hpp"

namespace kabufuda {

#undef min
#undef max

static void ResetOverlapped(OVERLAPPED& aio, DWORD offset = 0) {
  aio.Internal = 0;
  aio.InternalHigh = 0;
  aio.Offset = offset;
  aio.OffsetHigh = 0;
}

AsyncIO::AsyncIO(SystemStringView filename, bool truncate) {
#if WINDOWS_STORE
  CREATEFILE2_EXTENDED_PARAMETERS parms = {};
  parms.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
  parms.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
  parms.dwFileFlags = FILE_FLAG_OVERLAPPED;
  m_fh = CreateFile2(filename.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     truncate ? CREATE_ALWAYS : OPEN_ALWAYS, &parms);
#else
  m_fh = CreateFileW(filename.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                     truncate ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL, nullptr);
#endif
}

AsyncIO::~AsyncIO() {
  if (*this) {
    if (CancelIoEx(m_fh, nullptr))
      waitForCompletion();
    CloseHandle(m_fh);
  }
}

AsyncIO::AsyncIO(AsyncIO&& other) {
  m_fh = other.m_fh;
  other.m_fh = INVALID_HANDLE_VALUE;
  m_queue = std::move(other.m_queue);
  m_maxBlock = other.m_maxBlock;
}

AsyncIO& AsyncIO::operator=(AsyncIO&& other) {
  if (*this) {
    if (CancelIoEx(m_fh, nullptr))
      waitForCompletion();
    CloseHandle(m_fh);
  }
  m_fh = other.m_fh;
  other.m_fh = INVALID_HANDLE_VALUE;
  m_queue = std::move(other.m_queue);
  m_maxBlock = other.m_maxBlock;
  return *this;
}

void AsyncIO::_waitForOperation(size_t qIdx) const {
  auto& aio = const_cast<AsyncIO*>(this)->m_queue[qIdx];
  if (aio.first.hEvent == 0)
    return;
  GetOverlappedResult(m_fh, &aio.first, &aio.second, TRUE);
  CloseHandle(aio.first.hEvent);
  aio.first.hEvent = 0;
}

bool AsyncIO::asyncRead(size_t qIdx, void* buf, size_t length, off_t offset) {
  OVERLAPPED& aio = m_queue[qIdx].first;
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
  BOOL res = ReadFile(m_fh, buf, length, nullptr, &aio);
  return res == TRUE || GetLastError() == ERROR_IO_PENDING;
}

bool AsyncIO::asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset) {
  OVERLAPPED& aio = m_queue[qIdx].first;
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
  BOOL res = WriteFile(m_fh, buf, length, nullptr, &aio);
  return res == TRUE || GetLastError() == ERROR_IO_PENDING;
}

ECardResult AsyncIO::pollStatus(size_t qIdx, SizeReturn* szRet) const {
  auto& aio = const_cast<AsyncIO*>(this)->m_queue[qIdx];
  if (aio.first.hEvent == 0) {
    if (szRet)
      *szRet = aio.second;
    return ECardResult::READY;
  }
  if (GetOverlappedResult(m_fh, &aio.first, &aio.second, FALSE)) {
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
  for (auto it = const_cast<AsyncIO*>(this)->m_queue.begin();
       it != const_cast<AsyncIO*>(this)->m_queue.begin() + m_maxBlock; ++it) {
    auto& aio = *it;
    if (aio.first.hEvent == 0)
      continue;
    if (GetOverlappedResult(m_fh, &aio.first, &aio.second, FALSE)) {
      CloseHandle(aio.first.hEvent);
      aio.first.hEvent = 0;
    } else {
      if (GetLastError() == ERROR_IO_INCOMPLETE) {
        if (result > ECardResult::BUSY)
          result = ECardResult::BUSY;
      } else {
        _waitForOperation(it - m_queue.cbegin());
        if (result > ECardResult::IOERROR)
          result = ECardResult::IOERROR;
      }
    }
  }
  if (result == ECardResult::READY)
    const_cast<AsyncIO*>(this)->m_maxBlock = 0;
  return result;
}

void AsyncIO::waitForCompletion() const {
  for (size_t i = 0; i < m_maxBlock; ++i)
    _waitForOperation(i);
  const_cast<AsyncIO*>(this)->m_maxBlock = 0;
}

} // namespace kabufuda
