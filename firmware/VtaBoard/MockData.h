#pragma once
#include <string>
#include <time.h>
// Synthetic layout examples only; these are not claims about Bay 12 service.
inline std::string mockResponse(int64_t now) {
  const char *routes[] = {"500", "22", "522"};
  const char *destinations[] = {"Downtown", "Palo Alto", "Eastridge"};
  const int minutes[] = {21, 6, 14}; // Deliberately unsorted to exercise the parser.
  std::string json = R"({"ServiceDelivery":{"Status":true,"StopMonitoringDelivery":{"MonitoredStopVisit":[)";
  for (int i = 0; i < 3; ++i) {
    time_t t = now + minutes[i] * 60;
    struct tm utc;
    gmtime_r(&t, &utc);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    if (i) json += ',';
    json += std::string(R"({"MonitoredVehicleJourney":{"LineRef":")") + routes[i] +
      R"(","DestinationName":")" + destinations[i] +
      R"(","MonitoredCall":{"ExpectedArrivalTime":")" + stamp +
      R"(","ExpectedDepartureTime":")" + stamp + R"("}}})";
  }
  return json + "]}}}";
}
