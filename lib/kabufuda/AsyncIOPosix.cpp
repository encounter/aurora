#include "kabufuda/AsyncIO.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace kabufuda {

AsyncIO::AsyncIO(std::string_view filename, bool truncate) {
  m_fd = open(filename.data(), O_RDWR | O_CREAT | (truncate ? O_TRUNC : 0), 0644);
}

AsyncIO::~AsyncIO() {
  if (*this) {
    aio_cancel(m_fd, nullptr);
    close(m_fd);
  }
}

AsyncIO::AsyncIO(AsyncIO&& other) {
  m_fd = other.m_fd;
  other.m_fd = -1;
  m_queue = std::move(other.m_queue);
  m_maxBlock = other.m_maxBlock;
}

AsyncIO& AsyncIO::operator=(AsyncIO&& other) {
  if (*this) {
    aio_cancel(m_fd, nullptr);
    close(m_fd);
  }
  m_fd = other.m_fd;
  other.m_fd = -1;
  m_queue = std::move(other.m_queue);
  m_maxBlock = other.m_maxBlock;
  return *this;
}

void AsyncIO::_waitForOperation(size_t qIdx) const {
  auto& aio = const_cast<AsyncIO*>(this)->m_queue[qIdx];
  if (aio.first.aio_fildes == 0)
    return;
  const struct aiocb* aiop = &aio.first;
  struct timespec ts = {2, 0};
  while (aio_suspend(&aiop, 1, &ts) && errno == EINTR) {}
  if (aio_error(&aio.first) != EINPROGRESS)
    aio.second = aio_return(&aio.first);
  aio.first.aio_fildes = 0;
}

bool AsyncIO::asyncRead(size_t qIdx, void* buf, size_t length, off_t offset) {
  struct aiocb& aio = m_queue[qIdx].first;
  if (aio.aio_fildes) {
#ifndef NDEBUG
    fprintf(stderr, "WARNING: synchronous kabufuda fallback, check access polling\n");
#endif
    _waitForOperation(qIdx);
  }
  memset(&aio, 0, sizeof(struct aiocb));
  aio.aio_fildes = m_fd;
  aio.aio_offset = offset;
  aio.aio_buf = buf;
  aio.aio_nbytes = length;
  m_maxBlock = std::max(m_maxBlock, qIdx + 1);
  return aio_read(&aio) == 0;
}

bool AsyncIO::asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset) {
  struct aiocb& aio = m_queue[qIdx].first;
  if (aio.aio_fildes) {
#ifndef NDEBUG
    fprintf(stderr, "WARNING: synchronous kabufuda fallback, check access polling\n");
#endif
    _waitForOperation(qIdx);
  }
  memset(&aio, 0, sizeof(struct aiocb));
  aio.aio_fildes = m_fd;
  aio.aio_offset = offset;
  aio.aio_buf = const_cast<void*>(buf);
  aio.aio_nbytes = length;
  m_maxBlock = std::max(m_maxBlock, qIdx + 1);
  return aio_write(&aio) == 0;
}

ECardResult AsyncIO::pollStatus(size_t qIdx, SizeReturn* szRet) const {
  auto& aio = const_cast<AsyncIO*>(this)->m_queue[qIdx];
  if (aio.first.aio_fildes == 0) {
    if (szRet)
      *szRet = aio.second;
    return ECardResult::READY;
  }
  switch (aio_error(&aio.first)) {
  case 0:
    aio.second = aio_return(&aio.first);
    aio.first.aio_fildes = 0;
    if (szRet)
      *szRet = aio.second;
    return ECardResult::READY;
  case EINPROGRESS:
    return ECardResult::BUSY;
  default:
    aio.second = aio_return(&aio.first);
    aio.first.aio_fildes = 0;
    if (szRet)
      *szRet = aio.second;
    return ECardResult::IOERROR;
  }
}

ECardResult AsyncIO::pollStatus() const {
  ECardResult result = ECardResult::READY;
  for (auto it = const_cast<AsyncIO*>(this)->m_queue.begin();
       it != const_cast<AsyncIO*>(this)->m_queue.begin() + m_maxBlock; ++it) {
    auto& aio = *it;
    if (aio.first.aio_fildes == 0)
      continue;
    switch (aio_error(&aio.first)) {
    case 0:
      aio.second = aio_return(&aio.first);
      aio.first.aio_fildes = 0;
      break;
    case EINPROGRESS:
      if (result > ECardResult::BUSY)
        result = ECardResult::BUSY;
      break;
    default:
      aio.second = aio_return(&aio.first);
      aio.first.aio_fildes = 0;
      if (result > ECardResult::IOERROR)
        result = ECardResult::IOERROR;
      break;
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
