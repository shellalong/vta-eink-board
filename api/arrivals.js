import { createArrivalsHandler } from '../server/arrivals.js';

// One endpoint; no agency, stop, key, or upstream URL accepted from the client.
export default createArrivalsHandler();
