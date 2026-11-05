# VTA Diridon e-paper departure board

Arduino firmware for the **Elecrow CrowPanel ESP32-S3 V1.0 / SSD1683**:
400×300 pixels, black text on white, landscape. Displays San Jose Diridon / Bay 12
and three arrival slots using one 511 StopMonitoring request every 120 seconds.

## Start with mock mode

1. Open `firmware/VtaBoard/VtaBoard.ino` in Arduino IDE.
2. Copy `firmware/VtaBoard/config.h.example` to `firmware/VtaBoard/config.h`.
   Leave `USE_MOCK_DATA` set to `1`. Credentials can stay empty.
3. In Boards Manager install **esp32 by Espressif Systems 3.3.12**.
   In Library Manager install **ArduinoJson by Benoit Blanchon 7.4.3**.
   The Elecrow driver is already included; no other display library is needed.
4. Select **ESP32S3 Dev Module**, **USB CDC On Boot: Enabled**,
   **Flash Size: 8 MB**, **PSRAM: OPI PSRAM**, and
   **Partition Scheme: 8M with spiffs (3MB APP/1.5MB SPIFFS)** (`default_8MB`).
5. Click **Verify** before uploading. Connect the CrowPanel using a USB data cable,
   select its serial port, then click **Upload**. If it does not enumerate, hold
   BOOT, press/release RESET, release BOOT, and select the new port.
6. Check the full layout, orientation, bus icon, all three rows, and footer on the
   actual panel. The footer says `MOCK DATA` and `Wi-Fi: Mock`; the LIVE dot is hollow.

Mock data is synthetic, intentionally unsorted JSON, passed through the same parser
as live data. Route 22 / Palo Alto / 6 min, Route 522 / Eastridge / 14 min, and
Route 500 / Downtown / 21 min reproduce the layout example; they do not assert
that these routes serve Bay 12. Mock time starts at 10:18 AM PST and advances
without NTP. No Wi-Fi connection or API request occurs in mock mode.

## Enable live predictions

1. Obtain your personal API key from https://511.org/open-data/transit.
2. Edit **only the local `config.h`**: set `USE_MOCK_DATA` to `0`, and fill in
   `WIFI_SSID`, `WIFI_PASSWORD`, and `API_KEY`. Use a 2.4 GHz Wi-Fi network.
3. Verify again, then upload. Open Serial Monitor at **115200 baud**.
   Logs show HTTP status and acceptance; they do not print the key or request URL.

The only live transit request is:

```text
https://api.511.org/transit/StopMonitoring?api_key=YOUR_KEY&agency=SC&stopCode=63072&format=json
```

Requests send `Accept: application/json` and `Accept-Encoding: identity`.
Compressed responses are rejected; no inflater is included. HTTPS uses the ESP32
core's built-in CA bundle, and NTP supplies the clock required for certificate
validation and arrival calculations. Local times use America/Los_Angeles's
current US daylight-saving rules. There is no rapid retry: the next attempt is
120 seconds after the previous poll and display update completes.

## Data and screen behavior

- Reads actual `LineRef` and `DestinationName` from `MonitoredVehicleJourney`.
  It does not substitute published route names or hardcode live destinations.
- Reads `ExpectedArrivalTime` and `ExpectedDepartureTime` from `MonitoredCall`.
  Arrival time is preferred; departure is the fallback. If arrival is in the past
  but departure is still future, the waiting bus shows `ARRIVING`.
- Parses UTC, explicit timezone offsets, and fractional seconds. Discards expired
  predictions, explicitly unmonitored/cancelled entries, or entries missing route,
  destination, and both usable expected times. No scheduled-time fallback.
- Sorts soonest first and retains at most three predictions. Under 60 seconds is
  `ARRIVING`; otherwise minutes are rounded down. Unused slots show `--`.
  A successful response with no predictions shows `No live departures`.
- HTTP, JSON, compression, and SIRI service errors show `API error`. Disconnection
  shows `Offline`; clock failure shows `Clock not synced`. Last successful rows,
  their displayed minutes, and their update time remain visible after failures,
  with `last good data` shown explicitly and the LIVE dot hollow.
- `Updated` means the last successful fetch, not the last failed attempt or the
  vehicle's own telemetry timestamp. Cache is in RAM and does not survive reset.
- Draws exactly three row slots on an exact 15,000-byte framebuffer. Long text is
  ellipsized to its column. The included vendor font supports printable ASCII;
  unsupported Unicode characters show `?` instead of indexing outside the font.
- Full refresh occurs every poll by default. Only enable
  `ENABLE_PARTIAL_REFRESH=1` after checking this SSD1683 panel; full updates still
  occur on startup, status changes, and at least every 20 minutes. Partial mode
  uses the full-frame partial waveform with a matching packed buffer.

The labels and stop code are deliberately fixed to Bay 12 for this version.
No maps, vehicle positions, protobuf feeds, or requests for other stops are used.

## Command-line checks

From the project directory:

```sh
sh tools/test.sh
sh tools/build.sh mock
sh tools/build.sh live
```

The build helper explicitly overrides mock/live mode for that compilation without
editing your config. Arduino IDE uses the setting in `config.h`.
Set `ARDUINO_CLI` if your CLI is elsewhere; set `ARDUINOJSON_SRC` if the host tests
need a different ArduinoJson source path. Build products are ignored in `.build/`.
Host tests use Clang AddressSanitizer and UndefinedBehaviorSanitizer. Rendering tests
produce five PBM frames using the same layout function and actual Elecrow font code.

## Verification and remaining checks

Both modes compiled locally with ESP32 core 3.3.12 and ArduinoJson 7.4.3.
Parser tests and five framebuffer render cases passed with sanitizers. Mock and
ARRIVING layouts were visually inspected at 400×300.

The unauthenticated HTTPS endpoint returned HTTP 401 as expected. Successful
authenticated VTA payloads, key permissions, and Bay 12 coverage still require
your key. The actual field values and envelope can only be confirmed with that
response. No ESP32 USB serial device was detected, so nothing has been flashed.
Panel orientation, full/partial waveforms, and ghosting remain hardware checks.
See `VENDOR.md` for the pinned source, verified signatures, and driver limitations.

`config.h` is gitignored. Commit only `config.h.example`; do not paste credentials
into the sketch or build commands.
