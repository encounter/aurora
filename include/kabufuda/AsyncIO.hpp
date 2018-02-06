#ifndef __KABU_ASYNCIO_HPP__
#define __KABU_ASYNCIO_HPP__

#ifndef _WIN32
#include <aio.h>
using SizeReturn = ssize_t;
#else
#include <windows.h>
using SizeReturn = SSIZE_T;
#endif

#include "Util.hpp"
#include <vector>

namespace kabufuda
{

class AsyncIO
{
#ifndef _WIN32
    int m_fd = -1;
    std::vector<std::pair<struct aiocb, SizeReturn>> m_queue;
#else
#endif
    size_t m_maxBlock = 0;
public:
    AsyncIO() = default;
    AsyncIO(SystemStringView filename, bool truncate = false);
    ~AsyncIO();
    AsyncIO(AsyncIO&& other);
    AsyncIO& operator=(AsyncIO&& other);
    AsyncIO(const AsyncIO* other) = delete;
    AsyncIO& operator=(const AsyncIO& other) = delete;
    void resizeQueue(size_t queueSz) { m_queue.resize(queueSz); }
    SizeReturn syncRead(void* buf, size_t length, off_t offset);
    bool asyncRead(size_t qIdx, void* buf, size_t length, off_t offset);
    SizeReturn syncWrite(const void* buf, size_t length, off_t offset);
    bool asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset);
    ECardResult pollStatus(size_t qIdx, SizeReturn* szRet = nullptr) const;
    ECardResult pollStatus() const;
    void waitForCompletion() const;
    operator bool() const { return m_fd != -1; }
};

}

#endif // __KABU_ASYNCIO_HPP__
