#include <dolphin/os.h>
#include <dolphin/gx/GXStruct.h>

#include "fmt/base.h"
#include "fmt/printf.h"

#include "../../logging.hpp"

#include <cstdarg>

static aurora::Module reporter("aurora::os::report");

void OSReport(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  OSVReport(msg, args);
  va_end(args);
}

static std::string FormatToString(const char* msg, va_list list) {
  int ret = vsnprintf(nullptr, 0, msg, list);
  std::string buf(ret, '\0');
  vsnprintf(buf.data(), buf.size(), msg, list);
  return buf;
}

void OSVReport(const char* msg, va_list list) {
  reporter.info("{}", FormatToString(msg, list));
}

void OSPanic(const char* file, int line, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  reporter.fatal("PANIC {}:{}: {}", file, line, FormatToString(msg, args));
  va_end(args);
}

void OSFatal(GXColor fg, GXColor bg, const char* msg) {
  reporter.fatal("{}", msg);
}

