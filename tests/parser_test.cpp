#include "Transit.h"
#include "MockData.h"
#include <cassert>
#include <iostream>

using namespace transit;
std::string wrap(const std::string &visits) {
  return "{\"ServiceDelivery\":{\"StopMonitoringDelivery\":{\"MonitoredStopVisit\":" + visits + "}}}";
}
std::string visit(const char *arrival, const char *departure) {
  return std::string(R"({"MonitoredVehicleJourney":{"LineRef":568,"DestinationName":"Gilroy","MonitoredCall":{"ExpectedArrivalTime":")") + arrival +
    R"(","ExpectedDepartureTime":")" + departure + R"("}}})";
}
bool parseString(const std::string &s, int64_t now, Board &b) {
  return parse(s.c_str(), s.size(), now, b);
}
int main() {
  auto now = timestamp("2026-11-05T10:18:00-08:00");
  assert(now == timestamp("2026-11-05T18:18:00Z"));
  assert(now == timestamp("2026-11-05T18:18:00.123Z"));
  assert(timestamp("2026-02-29T10:18:00Z") == 0);
  assert(timestamp("2026-11-05T10:18:00") == 0);
  assert(timestamp("garbage") == 0);
  Board b;
  assert(parseString(mockResponse(now), now, b));
  assert(b.count == 3 && b.rows[0].route == "22" && b.rows[1].route == "522");
  assert(remaining(b.rows[0], now) == "6 min");
  assert(remaining(b.rows[0], now + 301) == "ARRIVING");
  assert(!parseString("{broken", now, b));
  assert(b.count == 3 && b.rows[0].route == "22");
  assert(!parseString(R"({"ServiceDelivery":{"Status":false}})", now, b));
  assert(!parseString("{}", now, b));
  assert(!parseString(R"({"ServiceDelivery":{"StopMonitoringDelivery":{"ErrorCondition":{}}}})", now, b));
  assert(b.count == 3);
  assert(parseString(wrap(visit("", "2026-11-05T18:20:00Z")), now, b));
  assert(b.count == 1 && b.rows[0].route == "568" && remaining(b.rows[0], now) == "2 min");
  assert(b.rows[0].arrival == 0 && b.rows[0].departure == now + 120);
  assert(parseString(wrap(visit("2026-11-05T18:17:00Z", "2026-11-05T18:20:00Z")), now, b));
  assert(b.count == 1 && remaining(b.rows[0], now) == "ARRIVING");
  assert(parseString(wrap(visit("2026-11-05T18:16:00Z", "2026-11-05T18:17:00Z")), now, b));
  assert(b.count == 0);
  assert(parseString(wrap("[]"), now, b) && b.count == 0);
  auto single = wrap(visit("2026-11-05T18:20:00Z", ""));
  assert(parseString(std::string("\xEF\xBB\xBF") + single, now, b) && b.count == 1);
  assert(parseString("{\"Siri\":" + single + "}", now, b) && b.count == 1);
  std::string arrays = R"({"ServiceDelivery":{"StopMonitoringDelivery":[{"MonitoredStopVisit":[)" +
    visit("2026-11-05T18:20:00Z", "") + "]}]}}";
  assert(parseString(arrays, now, b) && b.count == 1);
  std::string many = "[";
  for (int i = 9; i >= 1; --i) {
    if (i != 9) many += ',';
    std::string time = "2026-11-05T18:2" + std::to_string(i) + ":00Z";
    many += visit(time.c_str(), "");
  }
  assert(parseString(wrap(many + "]"), now, b));
  assert(b.count == 3 && b.rows[0].due < b.rows[1].due && b.rows[1].due < b.rows[2].due);
  std::cout << "PASS: mock parsing, sorting, offsets, fractional seconds, fallback, terminal dwell, expired predictions, empty responses, SIRI envelopes, BOM, and error retention\n";
}
