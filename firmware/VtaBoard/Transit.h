#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

namespace transit {
struct Arrival {
  std::string route, destination;
  int minutes = 0;
};
struct Board {
  Arrival rows[3];
  unsigned count = 0;
  int64_t updated = 0;
};

// ISO 8601 with explicit Z or UTC offset; independent of the device's local TZ.
inline int64_t timestamp(const std::string &s) {
  int y, mo, d, h, mi, sec, n = 0;
  if (sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &sec, &n) != 6)
    return 0;
  const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
  if (y < 2000 || mo < 1 || mo > 12 || d < 1 ||
      d > days[mo-1] + (mo == 2 && leap) || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59)
    return 0;
  size_t p = n;
  if (s[p] == '.') {
    ++p;
    size_t start = p;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
    if (p == start) return 0;
  }
  int offset = 0;
  if (s[p] == 'Z' && p + 1 == s.size()) {
    // UTC
  } else if (s[p] == '+' || s[p] == '-') {
    int oh, om, used = 0;
    if (sscanf(s.c_str() + p + 1, "%2d:%2d%n", &oh, &om, &used) != 2 ||
        p + 1 + used != s.size() || oh < 0 || oh > 23 || om < 0 || om > 59) return 0;
    offset = (oh * 60 + om) * 60 * (s[p] == '+' ? 1 : -1);
  } else return 0;
  y -= mo <= 2;
  int era = y / 400;
  unsigned yo = y - era * 400;
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yo * 365 + yo / 4 - yo / 100 + doy;
  return (int64_t(era) * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + sec - offset;
}

// Accept only the small proxy contract. Invalid/error responses never mutate output.
inline bool parse(const char *body, size_t length, Board &out) {
  JsonDocument doc;
  if (deserializeJson(doc, body, length)) return false;
  if (!doc["ok"].is<bool>() || doc["ok"] != true ||
      !doc["stop"].is<const char *>() ||
      doc["stop"] != "San Jose Diridon Bay 12" ||
      !doc["updated"].is<const char *>() ||
      !doc["arrivals"].is<JsonArrayConst>()) return false;
  Board fresh;
  fresh.updated = timestamp(doc["updated"].as<std::string>());
  if (!fresh.updated) return false;
  auto arrivals = doc["arrivals"].as<JsonArrayConst>();
  if (arrivals.size() > 3) return false;
  for (JsonVariantConst item : arrivals) {
    if (!item["route"].is<const char *>() || !item["destination"].is<const char *>() ||
        !item["minutes"].is<int>()) return false;
    Arrival a;
    a.route = item["route"].as<std::string>();
    a.destination = item["destination"].as<std::string>();
    a.minutes = item["minutes"].as<int>();
    if (a.route.empty() || a.route.size() > 64 || a.destination.empty() ||
        a.destination.size() > 256 || a.minutes < 0 || a.minutes > 10080) return false;
    if (fresh.count && a.minutes < fresh.rows[fresh.count - 1].minutes) return false;
    fresh.rows[fresh.count++] = a;
  }
  out = fresh;
  return true;
}
inline std::string remaining(const Arrival &a) {
  return a.minutes == 0 ? "ARRIVING" : std::to_string(a.minutes) + " min";
}
} // namespace transit
