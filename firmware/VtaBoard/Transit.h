#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

namespace transit {
struct Arrival {
  std::string route, destination;
  int64_t arrival = 0, departure = 0, due = 0;
};
struct Board {
  Arrival rows[3];
  unsigned count = 0;
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

inline std::string value(JsonVariantConst v) {
  if (v.is<JsonArrayConst>()) return value(v[0]);
  if (v.is<JsonObjectConst>()) return value(v["value"]);
  if (v.is<const char *>()) return v.as<std::string>();
  if (v.is<long>()) return std::to_string(v.as<long>());
  return "";
}
inline bool failed(JsonVariantConst v) {
  return (!v["Status"].isNull() && (v["Status"] == false || v["Status"] == "false")) ||
         !v["ErrorCondition"].isNull();
}
template <typename F> inline void each(JsonVariantConst v, F f) {
  if (v.is<JsonArrayConst>()) for (JsonVariantConst item : v.as<JsonArrayConst>()) f(item);
  else if (v.is<JsonObjectConst>()) f(v);
}
inline void addVisit(JsonVariantConst visit, int64_t now, Board &out) {
  auto j = visit["MonitoredVehicleJourney"];
  auto call = j["MonitoredCall"];
  if (j["Monitored"] == false || j["Cancellation"] == true ||
      call["ArrivalStatus"] == "cancelled" || call["DepartureStatus"] == "cancelled") return;
  Arrival a;
  a.route = value(j["LineRef"]);
  a.destination = value(j["DestinationName"]);
  a.arrival = timestamp(value(call["ExpectedArrivalTime"]));
  a.departure = timestamp(value(call["ExpectedDepartureTime"]));
  a.due = a.arrival ? a.arrival : a.departure;
  if (a.route.empty() || a.destination.empty() || !a.due) return;
  // At a terminal a bus may have arrived but still be waiting to depart.
  if (a.due < now) {
    if (a.departure < now) return;
    a.due = now;
  }
  unsigned pos = 0;
  while (pos < out.count && out.rows[pos].due <= a.due) ++pos;
  if (pos >= 3) return;
  if (out.count < 3) ++out.count;
  for (unsigned i = out.count - 1; i > pos; --i) out.rows[i] = out.rows[i-1];
  out.rows[pos] = a;
}
// Only assign output on success, including a valid empty response.
inline bool parse(const char *body, size_t length, int64_t now, Board &out) {
  if (length >= 3 && uint8_t(body[0]) == 0xEF && uint8_t(body[1]) == 0xBB && uint8_t(body[2]) == 0xBF) {
    body += 3; length -= 3;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body, length)) return false;
  JsonVariantConst service = doc["ServiceDelivery"];
  if (service.isNull()) service = doc["Siri"]["ServiceDelivery"];
  if (!service.is<JsonObjectConst>() || failed(service)) return false;
  auto deliveries = service["StopMonitoringDelivery"];
  if (!deliveries.is<JsonObjectConst>() && !deliveries.is<JsonArrayConst>()) return false;
  bool ok = true;
  Board fresh;
  each(deliveries, [&](JsonVariantConst delivery) {
    if (failed(delivery)) { ok = false; return; }
    auto visits = delivery["MonitoredStopVisit"];
    if (!visits.isNull() && !visits.is<JsonArrayConst>() && !visits.is<JsonObjectConst>()) {
      ok = false; return;
    }
    each(visits, [&](JsonVariantConst visit) { addVisit(visit, now, fresh); });
  });
  if (ok) out = fresh;
  return ok;
}
inline std::string remaining(const Arrival &a, int64_t now) {
  int64_t seconds = a.due - now;
  return seconds < 60 ? "ARRIVING" : std::to_string(seconds / 60) + " min";
}
} // namespace transit
