# VTA Diridon departure board with Vercel proxy

The CrowPanel ESP32-S3 requests **only your HTTPS Vercel `/api/arrivals` endpoint**.
Vercel calls 511 StopMonitoring for VTA `SC`, stop `63072`, filters and simplifies
that response, and returns at most three arrivals. The **511 key is stored only
in Vercel's server environment**. It is absent from the firmware configuration.

The Elecrow V1.0 SSD1683 display remains exactly 400×300, black on white,
landscape, with a bus icon, `VTA TRANSIT`, `San Jose Diridon • Bay 12`, LIVE,
three row slots, last successful update time, and Wi-Fi status.

## Files

- `api/arrivals.js`: Vercel Node.js function at `/api/arrivals`.
- `server/arrivals.js`: server-only parsing, upstream request, cache, and errors.
- `firmware/VtaBoard/`: sketch, normalized-response parser, mock data, display code.
- `tests/`: Node API tests and C++ parser/framebuffer tests.
- `public/`: the only static output directory. Firmware and local configuration
  are also excluded from CLI uploads by `.vercelignore`.

## Deploy the API to Vercel

Use Node.js **22.x**. The API uses built-in Node APIs and has no npm dependencies.
This repository is ready to deploy; no deployment URL is assumed or hardcoded.

From the project root:

```sh
npx vercel login
npx vercel link
```

Create or select a project; use this repository's root directory. Framework is
**Other**, build command is empty, output directory is **public**, and Node version
is **22.x**. `vercel.json` and `package.json` supply those settings.

In **Vercel → Project → Settings → Environment Variables**, add:

| Name | Value |
| --- | --- |
| `VTA_511_API_KEY` | Your personal 511 key, entered in Vercel only |
| `VTA_AGENCY` | `SC` |
| `VTA_STOP_CODE` | `63072` |

Set the variables for **Production**, and also **Preview** if you want to test
preview deployments. `.env.example` documents the names without a key. Do not
paste the real key into a committed file or firmware configuration. Do not use a
public/client-prefixed variable such as `NEXT_PUBLIC_...` for the key.

Then deploy:

```sh
npx vercel --prod
```

Copy the real production URL printed by Vercel. After environment-variable
changes, redeploy to apply them. The ESP32 must be able to access the production
endpoint without an interactive Vercel login: check Deployment Protection if it
receives an authentication page instead of JSON.

## Test the deployed endpoint

Open `https://YOUR-PROJECT.vercel.app/api/arrivals` in a browser, or run:

```sh
curl --fail-with-body -i https://YOUR-PROJECT.vercel.app/api/arrivals
```

A successful response has this shape (values here are illustrative):

```json
{
  "ok": true,
  "stop": "San Jose Diridon Bay 12",
  "updated": "2026-11-05T18:18:00.000Z",
  "arrivals": [
    { "route": "500", "destination": "Berryessa", "minutes": 6 },
    { "route": "568", "destination": "Fremont", "minutes": 14 },
    { "route": "68", "destination": "Gilroy", "minutes": 21 }
  ]
}
```

`minutes` is always a nonnegative integer; **0 means ARRIVING** (less than 60
seconds). A valid empty result is `"arrivals": []`. `updated` records the last
successful upstream retrieval in ISO 8601 UTC, not a vehicle telemetry timestamp.
The firmware converts it to Pacific time, including daylight saving time.

Errors have non-200 HTTP status and a small sanitized JSON body:

```json
{"ok":false,"error":"upstream_api_error","message":"Unable to retrieve live arrivals"}
```

- **502**: 511/network failure, timeout, invalid JSON/schema, or oversized response.
- **503**: missing key or agency/stop settings other than `SC` / `63072`.
- **405**: method other than GET.

Errors never include the upstream response, URL, key, or exception details. The
application has no request/exception logging. Request query parameters cannot
change the stop, agency, or upstream URL, and are not forwarded to 511.

## What Vercel does

The server makes this fixed upstream request, with the key inserted only there:

```text
http://api.511.org/transit/StopMonitoring?api_key=KEY&agency=SC&stopCode=63072&format=json
```

It reads the actual `LineRef`, `DestinationName`, and `ExpectedArrivalTime`.
`ExpectedDepartureTime` is used only when arrival time is missing. An explicitly
past arrival is removed even if its departure is future. Invalid timestamps,
missing names/times, unmonitored and cancelled predictions are excluded. Remaining
visits are sorted by expected time, limited to three, and converted to whole
minutes rounded down. No route names or destinations are substituted.

Requests use `Accept-Encoding: identity`, a 12-second timeout covering fetch and
body reading, a 256 KB body limit, and no redirect following. A 30-second warm
instance cache coalesces concurrent requests and recalculates minutes/filters
past predictions when reused. Successful responses also request up to 30 seconds
of Vercel CDN caching; its remaining TTL is shortened on warm-cache hits. Errors
use `no-store`. There is no stale-on-error success response. CDN responses can
contain countdowns up to 30 seconds old; the original `updated` time is retained.
This is a short cache, not a global quota or rate limiter across all instances,
regions, or deployments. A single ESP32 polling every 120 seconds uses about
30 requests/hour; multiple clients can increase upstream usage.

The function follows [Vercel's Node.js function conventions](https://vercel.com/docs/functions/runtimes/node-js)
and uses its documented [CDN cache headers](https://vercel.com/docs/caching/cache-control-headers).
Upstream reference: [511 transit API documentation](https://511.org/open-data/transit).

## Configure and test the CrowPanel

Copy the example beside the sketch:

```sh
cp firmware/VtaBoard/config.h.example firmware/VtaBoard/config.h
```

For a physical layout test, leave **`USE_MOCK_DATA=1`**. Mock mode uses a synthetic
normalized JSON response and a simulated 10:18 AM Pacific clock. It needs no
Wi-Fi, deployed endpoint, or transit key. It shows `MOCK DATA`, `Wi-Fi: Mock`, and
a hollow LIVE dot. Mock destinations/routes are illustrative, not proof of
current Bay 12 service.

For live mode edit the gitignored `config.h`:

```cpp
#define USE_MOCK_DATA 0
#define ENABLE_PARTIAL_REFRESH 0
static const char WIFI_SSID[] = "YOUR_2_4_GHZ_WIFI";
static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static const char VTA_API_URL[] = "https://YOUR-PROJECT.vercel.app/api/arrivals";
```

Edit the existing definitions rather than adding duplicates. There is no 511
credential in this file. The local configuration was migrated to remove the old
transit-key definition while preserving Wi-Fi settings. Fill in your actual
production endpoint after deployment.

The ESP32 reads only the normalized `ok`, `stop`, `updated`, and `arrivals` fields.
It rejects error/malformed responses without replacing the previous rows or
successful update time. After a failure, the footer says `API error - last good
data` (or `Offline - last good data`) and the LIVE dot is hollow. Last successful
numeric minutes remain frozen until a new successful response. Cache is in RAM
and does not survive a reset. Empty successful results show `No live departures`;
unused row slots show `--`.

Polling remains every 120 seconds after a poll/display update completes. Full
refresh runs each poll by default. **Leave partial refresh disabled until physical
panel testing**; if later enabled, startup, status transitions, and a 20-minute
maximum interval still force full refresh. Vendor driver files and signatures
remain unchanged; see `VENDOR.md`.

### TLS and transport limitations

ESP32 → Vercel uses **WiFiClientSecure with certificate validation enabled** via
ESP32 Arduino 3.3.12's built-in CA bundle. There is no `setInsecure()` and no
HTTP fallback. NTP must work for certificate validation; otherwise the footer
shows `Clock not synced`. Certificate validation depends on the CA bundle in the
installed ESP32 core and the device's clock; an outdated bundle can require a
core update. The actual Vercel certificate chain still needs a hardware check.
The firmware requests uncompressed JSON, rejects compressed replies, and limits
its response body to 4096 bytes.

Vercel → 511 uses **HTTP exactly as requested above**, so the key in that upstream
query is **not encrypted in transit on that leg**. It is not returned to the
ESP32/browser or logged by this application. Server-side storage alone does not
protect that HTTP network hop; switching the upstream URL to HTTPS would address
that transport limitation.

## Compile and upload

In Arduino IDE, open `firmware/VtaBoard/VtaBoard.ino` and install:

- **esp32 by Espressif Systems 3.3.12** in Boards Manager.
- **ArduinoJson by Benoit Blanchon 7.4.3** in Library Manager.

Select **ESP32S3 Dev Module**, **USB CDC On Boot: Enabled**, **Flash: 8 MB**,
**PSRAM: OPI PSRAM**, and **Partition: 8M with spiffs (3MB APP/1.5MB SPIFFS)**
(`default_8MB`). The Elecrow SSD1683 driver is included, with native 400×300
`ROTATE_0` landscape orientation; no extra display library is needed.

Click **Verify** before uploading. Connect the board with a USB data cable,
select its port, then click **Upload**. If no port appears, hold BOOT, press/release
RESET, release BOOT, and select the new port. Serial Monitor is **115200 baud**.
Test mock mode on the panel first; then configure live mode, verify, and upload
again. No script in this project flashes automatically.

## Automated checks

With Node 22 and Clang installed:

```sh
npm test
sh tools/test.sh
sh tools/build.sh mock
sh tools/build.sh live
```

There are no npm packages to install. Run the Node tests first: they generate a
real normalized handler response in `.build/proxy-response.json`, which the C++
parser test consumes for a cross-language contract check. C++ parser and vendor
framebuffer render tests use AddressSanitizer and UndefinedBehaviorSanitizer.
Five 400×300 PBM frames are rendered, covering mock, ARRIVING, empty, error, and
offline states. Build helpers override mock/live mode without editing config.
Set `ARDUINO_CLI` or `ARDUINOJSON_SRC` if their locations differ on your machine.

The tests cover successful and empty responses, malformed upstream payloads,
missing arrival fallback, past arrivals, minute boundaries, service/network/HTTP
failures and timeout, bounded bodies, caching, concurrent requests, secret
redaction/no logging, normalized ESP32 parsing, and preserving previous rows.

Verification for this update: **13 Node API tests passed**, the C++ normalized
parser (including the proxy-generated fixture) and five sanitized framebuffer
render cases passed, and **both mock and live firmware modes compiled** with the
versions above. The mock and retained-data error frames were visually inspected.
All firmware files, including the ignored local configuration, were checked for
absence of the old key identifier, direct upstream hostname, and upstream schema
field names. No Vercel deployment or hardware upload was performed.

Both firmware modes must compile before flashing. The checks here use mocked
upstream requests; deployment with your real Vercel environment and 511 key is
still needed to verify live Bay 12 payloads. On the CrowPanel, check the USB
upload, orientation, bullet/icon/text, Wi-Fi/NTP, real HTTPS connection, last-good
data after a network failure, refresh flash, and ghosting. The stock vendor BUSY
loop has no timeout, so the correct SSD1683 hardware revision/pins also matter.
