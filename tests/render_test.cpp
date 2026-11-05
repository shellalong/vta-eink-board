#include "Layout.h"
#include "MockData.h"
#include <cassert>
#include <fstream>
#include <iostream>

void save(const char *name, const uint8_t *frame) {
  std::ofstream f(name, std::ios::binary);
  f << "P4\n400 300\n";
  // PBM uses 1 for black; the panel uses 0 for black.
  for (size_t i = 0; i < 15000; ++i) f.put(char(~frame[i]));
}
int main() {
  uint8_t frame[15000];
  Paint_NewImage(frame, 400, 300, ROTATE_0, WHITE);
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  auto now = transit::timestamp("2026-11-05T10:18:00-08:00");
  transit::Board b;
  auto data = mockResponse(now);
  assert(transit::parse(data.c_str(), data.size(), b));
  renderLayout(b, true, true, now, "", true, "Wi-Fi: Mock");
  save(".build/mock-layout.pbm", frame);
  renderLayout(b, true, false, now, "API error", false, "Wi-Fi: Connected");
  save(".build/error-layout.pbm", frame);
  b.rows[0].destination = "A very long destination beyond the column";
  b.rows[0].minutes = 0;
  renderLayout(b, true, true, now, "", false, "Wi-Fi: Connected");
  save(".build/arriving-layout.pbm", frame);
  b = transit::Board{};
  renderLayout(b, true, true, now, "", false, "Wi-Fi: Connected");
  save(".build/empty-layout.pbm", frame);
  renderLayout(b, false, false, 0, "Offline", false, "Wi-Fi: Offline");
  save(".build/offline-layout.pbm", frame);
  std::cout << "PASS: five 400x300 frames rendered with the firmware layout and vendor fonts\n";
}
