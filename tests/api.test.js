import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { createArrivalsHandler, normalize } from '../server/arrivals.js';
import endpoint from '../api/arrivals.js';

const NOW = Date.parse('2026-11-05T10:18:00-08:00');
const SECRET = 'unit-test-secret-never-return';
const environment = { VTA_511_API_KEY: SECRET, VTA_AGENCY: 'SC', VTA_STOP_CODE: '63072' };
const stamp = (seconds) => new Date(NOW + seconds * 1000).toISOString();
function visit(seconds, route = '500', destination = 'Berryessa') {
  return { MonitoredVehicleJourney: { LineRef: route, DestinationName: destination,
    MonitoredCall: { ExpectedArrivalTime: stamp(seconds) } } };
}
function upstream(visits = []) {
  return { ServiceDelivery: { Status: true, StopMonitoringDelivery: { MonitoredStopVisit: visits } } };
}
function fakeResponse() {
  return {
    headers: {}, statusCode: 200, body: null,
    setHeader(name, value) { this.headers[name.toLowerCase()] = value; },
    status(code) { this.statusCode = code; return this; },
    json(body) { this.body = body; return this; },
  };
}
async function request(handler, method = 'GET') {
  const res = fakeResponse();
  // Caller-supplied stop and key must not change the upstream request.
  await handler({ method, query: { agency: 'XX', stopCode: 'other', api_key: 'client-key' } }, res);
  assert.ok(!JSON.stringify(res).includes(SECRET));
  return res;
}
function handlerFor(fetchImpl, extra = {}) {
  return createArrivalsHandler({ fetchImpl, env: () => environment, now: () => NOW, ...extra });
}

test('normalizes actual fields, orders by timestamp and limits to three; firmware fixture', async () => {
  let requested;
  const data = upstream([visit(21 * 60, '68', 'Gilroy'), visit(14 * 60, '568', 'Fremont'),
    visit(6 * 60, '22', 'Palo Alto'), visit(59)]);
  const res = await request(handlerFor(async (url, options) => {
    requested = new URL(url);
    assert.equal(options.headers['Accept-Encoding'], 'identity');
    assert.equal(options.redirect, 'error');
    return Response.json(data);
  }));
  assert.equal(requested.origin, 'http://api.511.org');
  assert.equal(requested.pathname, '/transit/StopMonitoring');
  assert.equal(requested.searchParams.get('api_key'), SECRET);
  assert.equal(requested.searchParams.get('agency'), 'SC');
  assert.equal(requested.searchParams.get('stopCode'), '63072');
  assert.equal(requested.searchParams.get('format'), 'json');
  assert.equal(res.statusCode, 200);
  assert.deepEqual(res.body, { ok: true, stop: 'San Jose Diridon Bay 12', updated: stamp(0),
    arrivals: [
      { route: '500', destination: 'Berryessa', minutes: 0 },
      { route: '22', destination: 'Palo Alto', minutes: 6 },
      { route: '568', destination: 'Fremont', minutes: 14 },
    ] });
  await mkdir('.build', { recursive: true });
  await writeFile('.build/proxy-response.json', JSON.stringify(res.body));
});

test('empty arrays and a delivery with no visits return a successful empty result', async () => {
  for (const data of [upstream(), { ServiceDelivery: { StopMonitoringDelivery: {} } }]) {
    const res = await request(handlerFor(async () => Response.json(data)));
    assert.equal(res.statusCode, 200);
    assert.deepEqual(res.body.arrivals, []);
  }
});

test('departure fallback only when arrival is missing; rejects missing, past, invalid and cancelled entries', () => {
  const fallback = visit(100);
  delete fallback.MonitoredVehicleJourney.MonitoredCall.ExpectedArrivalTime;
  fallback.MonitoredVehicleJourney.MonitoredCall.ExpectedDepartureTime = stamp(120);
  const pastWithFutureDeparture = visit(-30);
  pastWithFutureDeparture.MonitoredVehicleJourney.MonitoredCall.ExpectedDepartureTime = stamp(120);
  const missing = visit(100);
  missing.MonitoredVehicleJourney.MonitoredCall = {};
  const cancelled = visit(100);
  cancelled.MonitoredVehicleJourney.MonitoredCall.DepartureStatus = 'cancelled';
  const unmonitored = visit(100);
  unmonitored.MonitoredVehicleJourney.Monitored = false;
  const invalid = visit(100);
  invalid.MonitoredVehicleJourney.MonitoredCall.ExpectedArrivalTime = '2026-02-30T10:18:00Z';
  const result = normalize(upstream([visit(-1), pastWithFutureDeparture, missing,
    cancelled, unmonitored, invalid, fallback]), NOW);
  assert.deepEqual(result.arrivals, [{ route: '500', destination: 'Berryessa', minutes: 2 }]);
});

test('minute boundaries and timezone offsets', () => {
  assert.deepEqual(normalize(upstream([visit(0), visit(59), visit(60)]), NOW)
    .arrivals.map((a) => a.minutes), [0, 0, 1]);
  const item = visit(0, 568, [{ value: 'Gilroy' }]);
  item.MonitoredVehicleJourney.MonitoredCall.ExpectedArrivalTime = '2026-11-05T10:24:00-08:00';
  assert.deepEqual(normalize(upstream([item]), NOW).arrivals,
    [{ route: '568', destination: 'Gilroy', minutes: 6 }]);
});

test('object visits, array deliveries, SIRI envelope, and BOM', async () => {
  const data = { Siri: { ServiceDelivery: { StopMonitoringDelivery: [{ MonitoredStopVisit: visit(60) }] } } };
  const res = await request(handlerFor(async () => new Response('\uFEFF' + JSON.stringify(data))));
  assert.equal(res.body.arrivals[0].minutes, 1);
});

test('malformed 511 payloads and explicit upstream service errors fail without leaking details', async () => {
  const payloads = ['not JSON', '{}', 'null',
    JSON.stringify({ ServiceDelivery: { Status: false, ErrorCondition: SECRET } }),
    JSON.stringify({ ServiceDelivery: { StopMonitoringDelivery: 'invalid' } }),
    JSON.stringify({ ServiceDelivery: { StopMonitoringDelivery: { ErrorCondition: SECRET } } }),
    JSON.stringify(upstream([null])), JSON.stringify(upstream([{}])),
    JSON.stringify(upstream('invalid'))];
  for (const payload of payloads) {
    const res = await request(handlerFor(async () => new Response(payload)));
    assert.equal(res.statusCode, 502);
    assert.equal(res.body.error, 'upstream_api_error');
    assert.equal(res.headers['cache-control'], 'no-store');
  }
});

test('HTTP failures, network exceptions containing secrets, timeout and reflected secrets are sanitized', async (t) => {
  const log = t.mock.method(console, 'log', () => {});
  const error = t.mock.method(console, 'error', () => {});
  const warn = t.mock.method(console, 'warn', () => {});
  for (const fetchImpl of [
    async () => new Response(SECRET, { status: 429 }),
    async () => { throw new Error('http://api.511.org/?api_key=' + SECRET); },
    async () => Response.json(upstream([visit(100, '500', SECRET)])),
    async (url, { signal }) => new Promise((resolve, reject) => {
      signal.addEventListener('abort', () => reject(new Error(SECRET)), { once: true });
    }),
  ]) {
    const res = await request(handlerFor(fetchImpl, { timeoutMs: 5 }));
    assert.equal(res.statusCode, 502);
    assert.deepEqual(res.body, { ok: false, error: 'upstream_api_error',
      message: 'Unable to retrieve live arrivals' });
  }
  assert.equal(log.mock.callCount() + error.mock.callCount() + warn.mock.callCount(), 0);
});

test('oversized upstream response is bounded and rejected', async () => {
  const res = await request(handlerFor(async () => new Response('x'.repeat(256 * 1024 + 1))));
  assert.equal(res.statusCode, 502);
});

test('reflected credentials are blocked before JSON escaping', async () => {
  const key = 'test-"quoted"-secret';
  const res = await request(handlerFor(async () => Response.json(upstream([visit(100, '500', key)])),
    { env: () => ({ ...environment, VTA_511_API_KEY: key }) }));
  assert.equal(res.statusCode, 502);
  assert.ok(!JSON.stringify(res).includes('quoted'));
});

test('warm cache recomputes minutes, drops past arrivals and expires without hiding errors', async () => {
  let now = NOW;
  let calls = 0;
  const handler = handlerFor(async () => {
    calls++;
    if (calls > 1) throw new Error(SECRET);
    return Response.json(upstream([visit(10), visit(65)]));
  }, { now: () => now });
  let res = await request(handler);
  assert.equal(res.headers['vercel-cdn-cache-control'], 'public, s-maxage=30');
  now += 20_000;
  res = await request(handler);
  assert.equal(calls, 1);
  assert.equal(res.body.arrivals.length, 1);
  assert.equal(res.body.arrivals[0].minutes, 0);
  assert.equal(res.body.updated, stamp(0));
  assert.equal(res.headers['vercel-cdn-cache-control'], 'public, s-maxage=10');
  now += 10_000;
  res = await request(handler);
  assert.equal(calls, 2);
  assert.equal(res.statusCode, 502);
  assert.equal(res.headers['cache-control'], 'no-store');
  assert.ok(!res.headers['vercel-cdn-cache-control']);
});

test('coalesces concurrent upstream requests on a warm instance', async () => {
  let calls = 0;
  const handler = handlerFor(async () => { calls++; return Response.json(upstream()); });
  const responses = await Promise.all([request(handler), request(handler), request(handler)]);
  assert.equal(calls, 1);
  assert.ok(responses.every((res) => res.statusCode === 200));
});

test('missing or wrong server configuration and non-GET requests never call upstream', async () => {
  for (const env of [{}, { ...environment, VTA_AGENCY: 'XX' }, { ...environment, VTA_STOP_CODE: 'other' }]) {
    const res = await request(handlerFor(() => { assert.fail('must not call upstream'); }, { env: () => env }));
    assert.equal(res.statusCode, 503);
    assert.equal(res.body.error, 'server_configuration_error');
  }
  const res = await request(handlerFor(() => { assert.fail('must not call upstream'); }), 'POST');
  assert.equal(res.statusCode, 405);
  assert.equal(res.headers.allow, 'GET');
});

test('Vercel entry point exports a handler and reports missing configuration', async () => {
  const previous = process.env.VTA_511_API_KEY;
  process.env.VTA_511_API_KEY = '';
  try {
    assert.equal(typeof endpoint, 'function');
    const res = await request(endpoint);
    assert.equal(res.statusCode, 503);
  } finally {
    if (previous === undefined) delete process.env.VTA_511_API_KEY;
    else process.env.VTA_511_API_KEY = previous;
  }
});
