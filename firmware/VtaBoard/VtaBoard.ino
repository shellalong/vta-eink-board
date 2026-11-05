#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <time.h>
#include "config.h"
#include "Transit.h"
#include "MockData.h"
#include "EPD.h"
#include "EPD_GUI.h"
#include "Layout.h"

static_assert(EPD_W == 400 && EPD_H == 300, "This layout requires 400x300");
constexpr uint32_t POLL_MS = 120000;
constexpr uint32_t FULL_MS = 20 * 60 * 1000;
constexpr size_t MAX_BODY = 128 * 1024;
uint8_t frame[400 * 300 / 8];
transit::Board board;
bool haveSuccess = false, requestOK = false, firstFrame = true;
uint32_t lastPoll = 0, lastFull = 0;
time_t updated = 0;
const char *errorMessage = "";

class BoundedBody : public Stream {
 public:
  String body;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    if (n > MAX_BODY - body.length()) return 0;
    return body.concat(reinterpret_cast<const char *>(data), n) ? n : 0;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

time_t currentTime() {
#if USE_MOCK_DATA
  // 10:18 AM PST on 2026-11-05; mock mode never needs Wi-Fi or NTP.
  return time_t(1793902680) + millis() / 1000;
#else
  return time(nullptr);
#endif
}

bool ensureNetworkAndClock() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) { errorMessage = "Offline"; return false; }
  if (time(nullptr) < 1700000000) {
    configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    uint32_t start = millis();
    while (time(nullptr) < 1700000000 && millis() - start < 10000) delay(100);
  }
  if (time(nullptr) < 1700000000) { errorMessage = "Clock not synced"; return false; }
  return true;
}

bool fetchArrivals() {
#if USE_MOCK_DATA
  auto body = mockResponse(currentTime());
  return transit::parse(body.c_str(), body.size(), currentTime(), board);
#else
  if (!ensureNetworkAndClock()) return false;
  errorMessage = "API error";
  if (!API_KEY[0]) return false;
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle(); // ESP32 Arduino 3.3.12; validates the HTTPS peer.
  tls.setHandshakeTimeout(15);
  HTTPClient http;
  String url = "https://api.511.org/transit/StopMonitoring?api_key=";
  // Percent-encode the key without logging credentials or the request URL.
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(API_KEY); *p; ++p) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') url += char(*p);
    else { char encoded[4]; snprintf(encoded, sizeof(encoded), "%%%02X", *p); url += encoded; }
  }
  url += "&agency=SC&stopCode=63072&format=json";
  if (!http.begin(tls, url)) return false;
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  const char *headers[] = {"Content-Encoding"};
  http.collectHeaders(headers, 1);
  int code = http.GET();
  bool ok = false;
  String encoding = http.header("Content-Encoding");
  encoding.toLowerCase();
  if (code == HTTP_CODE_OK && (encoding.isEmpty() || encoding == "identity") &&
      http.getSize() <= int(MAX_BODY)) {
    BoundedBody body;
    int received = http.writeToStream(&body); // HTTPClient handles chunked transfer framing.
    if (received >= 0 && size_t(received) == body.body.length())
      ok = transit::parse(body.body.c_str(), body.body.length(), currentTime(), board);
  }
  Serial.printf("511 HTTP %d; predictions %s\n", code, ok ? "accepted" : "unavailable");
  http.end();
  if (!ok && WiFi.status() != WL_CONNECTED) errorMessage = "Offline";
  return ok;
#endif
}

void syncOldFrame() {
  // Full and partial vendor display calls write RAM 0x24 only.
  // Keep previous-image RAM 0x26 in sync for the next differential update.
  EPD_Address_Set(0, 0, 399, 299);
  EPD_SetCursor(0, 0);
  EPD_WR_REG(0x26);
  for (size_t i = 0; i < sizeof(frame); ++i) EPD_WR_DATA8(frame[i]);
}

void drawBoard(bool modeChanged) {
  const char *wifi = USE_MOCK_DATA ? "Wi-Fi: Mock" :
    WiFi.status() == WL_CONNECTED ? "Wi-Fi: Connected" : "Wi-Fi: Offline";
  renderLayout(board, haveSuccess, requestOK, updated, requestOK ? currentTime() : updated, errorMessage,
               USE_MOCK_DATA, wifi);
  bool full = firstFrame || modeChanged || !ENABLE_PARTIAL_REFRESH || millis() - lastFull >= FULL_MS;
  if (full) {
    EPD_Init();
    EPD_Display(frame);
    lastFull = millis();
  } else {
    // Aligned, full-width packed buffer, with the panel awake since the last update.
    EPD_Display_Part(0, 0, 400, 300, frame);
  }
  syncOldFrame();
  firstFrame = false;
  // Leave the controller awake for partial updates; do not reset between them.
}

void pollAndDraw() {
  bool previous = requestOK;
  requestOK = fetchArrivals();
  if (requestOK) { haveSuccess = true; updated = currentTime(); errorMessage = ""; }
  // Failed requests never overwrite board or updated.
  drawBoard(previous != requestOK);
}

void setup() {
  Serial.begin(115200);
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH); // V1.0 vendor example's display power enable.
  EPD_GPIOInit();
  Paint_NewImage(frame, 400, 300, ROTATE_0, WHITE);
#if !USE_MOCK_DATA
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#endif
  pollAndDraw();
  lastPoll = millis();
}

void loop() {
  if (millis() - lastPoll >= POLL_MS) {
    pollAndDraw();
    lastPoll = millis();
  }
  delay(50);
}
