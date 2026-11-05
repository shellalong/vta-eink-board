#include "Transit.h"
#include "MockData.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace transit;
std::string response(const std::string &arrivals) {
  return R"({"ok":true,"stop":"San Jose Diridon Bay 12","updated":"2026-11-05T10:18:00-08:00","arrivals":)" + arrivals + "}";
}
bool parseString(const std::string &s, Board &b) {
  return parse(s.c_str(), s.size(), b);
}
int main(int argc, char **argv) {
  const auto now = timestamp("2026-11-05T10:18:00-08:00");
  assert(now == timestamp("2026-11-05T18:18:00Z"));
  assert(timestamp("2026-02-29T10:18:00Z") == 0);
  Board b;
  assert(parseString(mockResponse(now), b));
  assert(b.count == 3 && b.updated == now && b.rows[0].route == "500");
  assert(remaining(b.rows[0]) == "6 min");
  for (const auto &bad : {
      "{broken", "{}", R"({"ok":false,"error":"upstream_api_error"})",
      R"({"ServiceDelivery":{"StopMonitoringDelivery":{}}})"}) {
    assert(!parseString(bad, b));
    assert(b.count == 3 && b.rows[0].route == "500" && b.updated == now);
  }
  for (const auto &bad : {
      R"([{"route":"500","destination":"Berryessa","minutes":-1}])",
      R"([{"route":"500","destination":"Berryessa","minutes":"6"}])",
      R"([{"route":"500","destination":"Berryessa","minutes":1.5}])",
      R"([{"route":"500","destination":"Berryessa"}])",
      R"([{"route":"500","destination":"","minutes":6}])",
      R"([{"route":"500","destination":"Berryessa","minutes":8},{"route":"68","destination":"Gilroy","minutes":2}])",
      R"([null])"}) {
    assert(!parseString(response(bad), b));
    assert(b.count == 3 && b.rows[0].route == "500");
  }
  assert(parseString(response(R"([{"route":"568","destination":"Gilroy","minutes":0}])"), b));
  assert(b.count == 1 && remaining(b.rows[0]) == "ARRIVING");
  assert(parseString(response("[]"), b) && b.count == 0 && b.updated == now);
  assert(parseString(mockResponse(now), b));
  auto invalid = mockResponse(now);
  invalid.replace(invalid.find("2026-11-05"), 10, "2026-02-30");
  assert(!parseString(invalid, b) && b.count == 3);
  // Cross-language fixture produced by the real proxy handler in the JS tests.
  if (argc > 1) {
    std::ifstream f(argv[1]);
    assert(f.good());
    const std::string actual((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    assert(parseString(actual, b));
    assert(b.count == 3 && b.rows[0].route == "500" && b.rows[0].minutes == 0);
    assert(b.rows[1].minutes == 6 && b.rows[2].minutes == 14);
  }
  std::cout << "PASS: normalized JSON, mock data, updated timestamp, empty results, ARRIVING, schema errors and cache retention\n";
}
