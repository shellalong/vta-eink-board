const STOP = 'San Jose Diridon Bay 12';
const CACHE_MS = 30_000;
const TIMEOUT_MS = 12_000;
const MAX_UPSTREAM_BYTES = 256 * 1024;

function object(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function list(value) {
  if (Array.isArray(value)) return value;
  if (object(value)) return [value];
  throw new Error('Invalid upstream structure');
}
function failed(value) {
  return value.Status === false || value.Status === 'false' || value.ErrorCondition != null;
}
function text(value) {
  if (Array.isArray(value)) return text(value[0]);
  if (object(value)) return text(value.value);
  if (typeof value === 'string') return value.trim();
  if (Number.isSafeInteger(value)) return String(value);
  return '';
}
function timestamp(value) {
  // Date.parse alone accepts local/ambiguous times and normalizes impossible dates.
  if (typeof value !== 'string' ||
      !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(value)) return NaN;
  const [y, m, d, h, minute, second] = value.slice(0, 19).split(/[-T:]/).map(Number);
  const calendar = new Date(Date.UTC(y, m - 1, d));
  if (calendar.getUTCFullYear() !== y || calendar.getUTCMonth() !== m - 1 ||
      calendar.getUTCDate() !== d || h > 23 || minute > 59 || second > 59) return NaN;
  return Date.parse(value);
}

export function normalize(payload, now, updated = now) {
  const service = payload?.ServiceDelivery ?? payload?.Siri?.ServiceDelivery;
  if (!object(service) || failed(service)) throw new Error('Invalid upstream response');
  const arrivals = [];
  for (const delivery of list(service.StopMonitoringDelivery)) {
    if (!object(delivery) || failed(delivery)) throw new Error('Invalid upstream delivery');
    // A delivery without visits is the documented empty-result form.
    const visits = delivery.MonitoredStopVisit == null ? [] : list(delivery.MonitoredStopVisit);
    for (const visit of visits) {
      if (!object(visit)) throw new Error('Invalid upstream visit');
      const journey = visit.MonitoredVehicleJourney;
      if (!object(journey)) throw new Error('Invalid upstream journey');
      const call = journey.MonitoredCall;
      if (!object(call)) throw new Error('Invalid upstream call');
      if (journey.Monitored === false || journey.Monitored === 'false' ||
          journey.Cancellation === true || journey.Cancellation === 'true' ||
          ['cancelled', 'canceled'].includes(call.ArrivalStatus) ||
          ['cancelled', 'canceled'].includes(call.DepartureStatus)) continue;
      const route = text(journey.LineRef);
      const destination = text(journey.DestinationName);
      // Departure is used only when arrival is absent, not when arrival is past.
      const expected = text(call.ExpectedArrivalTime) || text(call.ExpectedDepartureTime);
      const due = timestamp(expected);
      if (!route || !destination || !Number.isFinite(due) || due < now) continue;
      arrivals.push({ route, destination, due });
    }
  }
  arrivals.sort((a, b) => a.due - b.due);
  return {
    ok: true,
    stop: STOP,
    updated: new Date(updated).toISOString(),
    arrivals: arrivals.slice(0, 3).map(({ route, destination, due }) => ({
      route, destination, minutes: Math.floor((due - now) / 60_000),
    })),
  };
}

function containsSecret(body, key) {
  return [body.stop, body.updated, ...body.arrivals.flatMap((a) => [a.route, a.destination])]
    .some((value) => value.includes(key));
}

async function readJSON(response) {
  if (!response.body || Number(response.headers.get('content-length')) > MAX_UPSTREAM_BYTES)
    throw new Error('Invalid upstream body');
  const reader = response.body.getReader();
  const chunks = [];
  let length = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > MAX_UPSTREAM_BYTES) {
        await reader.cancel();
        throw new Error('Upstream body too large');
      }
      chunks.push(value);
    }
  } finally {
    reader.releaseLock();
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8').replace(/^\uFEFF/, ''));
}

export function createArrivalsHandler({ fetchImpl = globalThis.fetch, now = Date.now,
                                        env = () => process.env, timeoutMs = TIMEOUT_MS } = {}) {
  // Best-effort warm-instance cache plus coalescing; not a global rate limiter.
  let cached = null;
  let pending = null;
  return async function handler(req, res) {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    if (req.method !== 'GET') {
      res.setHeader('Allow', 'GET');
      return res.status(405).json({ ok: false, error: 'method_not_allowed', message: 'Use GET' });
    }
    const settings = env();
    const key = settings.VTA_511_API_KEY?.trim();
    const agency = settings.VTA_AGENCY || 'SC';
    const stopCode = settings.VTA_STOP_CODE || '63072';
    if (!key || agency !== 'SC' || stopCode !== '63072') {
      return res.status(503).json({ ok: false, error: 'server_configuration_error',
        message: 'Arrivals service is not configured' });
    }
    try {
      let sample = cached;
      if (!sample || now() - sample.updated >= CACHE_MS) {
        if (!pending) {
          pending = (async () => {
            const url = new URL('http://api.511.org/transit/StopMonitoring');
            url.search = new URLSearchParams({ api_key: key, agency, stopCode, format: 'json' });
            const controller = new AbortController();
            const timer = setTimeout(() => controller.abort(), timeoutMs);
            try {
              const upstream = await fetchImpl(url, {
                headers: { Accept: 'application/json', 'Accept-Encoding': 'identity' },
                signal: controller.signal,
                redirect: 'error',
              });
              if (!upstream.ok) {
                await upstream.body?.cancel();
                throw new Error('Upstream request failed');
              }
              const payload = await readJSON(upstream);
              const updated = now();
              // Validate before caching, including a guard against reflected secrets.
              const normalized = normalize(payload, updated);
              if (containsSecret(normalized, key)) throw new Error('Unsafe upstream data');
              cached = { payload, updated };
              return cached;
            } finally {
              clearTimeout(timer);
            }
          })();
        }
        try { sample = await pending; } finally { pending = null; }
      }
      // Recompute minutes and filter departed buses on warm-cache hits.
      const body = normalize(sample.payload, now(), sample.updated);
      if (containsSecret(body, key)) throw new Error('Unsafe upstream data');
      const ttl = Math.max(1, Math.floor((CACHE_MS - (now() - sample.updated)) / 1000));
      res.setHeader('Cache-Control', 'public, max-age=0, must-revalidate');
      res.setHeader('Vercel-CDN-Cache-Control', `public, s-maxage=${ttl}`);
      return res.status(200).json(body);
    } catch {
      // Never log exceptions, upstream bodies, URLs, or environment values.
      return res.status(502).json({ ok: false, error: 'upstream_api_error',
        message: 'Unable to retrieve live arrivals' });
    }
  };
}
