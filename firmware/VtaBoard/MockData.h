#pragma once
#include <string>
#include <time.h>
// Synthetic normalized proxy response; no network needed.
inline std::string mockResponse(int64_t now) {
  time_t t = now;
  struct tm utc;
  gmtime_r(&t, &utc);
  char stamp[32];
  strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return std::string(R"({"ok":true,"stop":"San Jose Diridon Bay 12","updated":")") + stamp +
    R"(","arrivals":[{"route":"500","destination":"Berryessa","minutes":6},{"route":"568","destination":"Fremont","minutes":14},{"route":"68","destination":"Gilroy","minutes":21}]})";
}
