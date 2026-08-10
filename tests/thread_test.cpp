#include "thread.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>

namespace aurora::thread {
namespace {
TEST(ThreadTest, DestructorRequestsStopAndJoins) {
  std::mutex mutex;
  std::condition_variable cv;
  bool started = false;
  bool stopped = false;

  {
    Thread worker{{
                      .name = "Aurora thread test",
                      .priority = Priority::Low,
                  },
                  [&](std::stop_token token) {
                    std::stop_callback wakeOnStop{token, [&] { cv.notify_all(); }};
                    std::unique_lock lock{mutex};
                    started = true;
                    cv.notify_all();
                    cv.wait(lock, [&] { return token.stop_requested(); });
                    stopped = true;
                  }};

    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return started; });
  }

  EXPECT_TRUE(stopped);
}
} // namespace
} // namespace aurora::thread
