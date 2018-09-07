#include <util/format.h>

#include <cstdio>
#include <cstdarg>
#include <sstream>

namespace util {

static char p_format_string_buf[4096];

std::string fmt(const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  vsprintf_s(p_format_string_buf, sizeof(p_format_string_buf), fmt, va);
  va_end(va);

  return std::string(p_format_string_buf);
}

void linewrap(const std::string& line, size_t wrap_limit,
  std::function<void(const std::string& line, size_t line_no)> callback)
{
  if(line.length() <= wrap_limit) return callback(line, 0);

  size_t pos = 0, line_no = 0;
  while(pos < line.length()) {
    size_t count = pos+wrap_limit <= line.length() ? wrap_limit : std::string::npos;

    callback(line.substr(pos, count), line_no);

    pos += wrap_limit;
    line_no++;
  }
}

}