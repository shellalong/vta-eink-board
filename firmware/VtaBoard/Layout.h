#pragma once
#include <time.h>
#include "Transit.h"
#include "EPD.h"
#include "EPD_GUI.h"

// The vendor font indexes printable ASCII only. Fit text to a fixed column.
inline void textAt(int x, int y, const std::string &raw, int size, int maxWidth) {
  std::string clean;
  for (size_t i = 0; i < raw.size(); ++i) {
    unsigned char c = raw[i];
    if (c >= 32 && c <= 126) clean += char(c);
    else if (c >= 0xC0) clean += '?';
    else if (c < 32) clean += ' ';
  }
  size_t limit = maxWidth / (size / 2);
  if (clean.size() > limit) clean = clean.substr(0, limit > 3 ? limit - 3 : 0) + "...";
  EPD_ShowString(x, y, clean.c_str(), size, BLACK);
}

inline void renderLayout(const transit::Board &board, bool haveSuccess, bool requestOK,
                         time_t updated, time_t now, const char *errorMessage,
                         bool mockData, const char *wifi) {
  EPD_Full(WHITE);
  // Small bus icon.
  EPD_DrawRectangle(13, 19, 37, 42, BLACK, 0);
  EPD_DrawRectangle(16, 22, 34, 31, BLACK, 0);
  EPD_DrawCircle(18, 39, 2, BLACK, 1);
  EPD_DrawCircle(32, 39, 2, BLACK, 1);
  textAt(48, 16, "VTA TRANSIT", 24, 264);
  EPD_DrawCircle(337, 28, 3, BLACK, requestOK && !mockData ? 1 : 0);
  textAt(347, 21, "LIVE", 12, 40);
  textAt(12, 55, "San Jose Diridon / Bay 12", 16, 376);
  EPD_DrawLine(12, 85, 387, 85, BLACK);
  textAt(12, 99, "NEXT ARRIVALS", 16, 376);
  for (unsigned i = 0; i < 3; ++i) {
    int y = 136 + i * 40;
    if (i < board.count) {
      textAt(12, y, "Route " + board.rows[i].route, 16, 88);
      textAt(108, y, board.rows[i].destination, 16, 192);
      std::string due = transit::remaining(board.rows[i], now);
      textAt(388 - due.size() * 8, y, due, 16, 80);
    } else if (i == 0 && haveSuccess) {
      textAt(12, y, "No live departures", 16, 376);
    } else {
      textAt(12, y, "--", 16, 376);
    }
    EPD_DrawLine(12, y + 29, 387, y + 29, BLACK);
  }
  if (!requestOK) textAt(12, 256, std::string(errorMessage) + (haveSuccess ? " - last good data" : ""), 12, 376);
  else if (mockData) textAt(12, 256, "MOCK DATA", 12, 376);
  char stamp[24] = "--";
  if (haveSuccess) {
    struct tm local;
    localtime_r(&updated, &local);
    strftime(stamp, sizeof(stamp), "%I:%M %p", &local);
  }
  textAt(12, 280, std::string("Updated ") + stamp, 12, 198);
  textAt(220, 280, wifi, 12, 168);
}
