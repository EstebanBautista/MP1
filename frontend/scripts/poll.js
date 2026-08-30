/**
 * GameWS
 * Thin HTTP polling client for the PP Racing backend.
 *
 * Design-4 transport alternative: the game page does not keep any connection
 * open to the server. It polls the authoritative simulation state over HTTP
 * and sends gameplay events as POST requests:
 *
 *   server -> client  GET /state  -> {"type":"state", "tick":N, "cars":[...]}
 *   client -> server  POST /action -> {"type":"start"|"restart"|"game_over"}
 *                     (same body)   {"type":"removeCar","id":N}
 *                     (same body)   {"type":"slowmo","active":true|false}
 *
 * It exposes the exact same surface used by game.js (on/connect/send) as the
 * old ws.js, so the game logic is unaware that the transport changed.
 *
 * Polling uses a chained setTimeout: the next request only starts after the
 * previous one finished, so responses never pile up (a slow server simply
 * lowers the effective rate instead of buffering requests).
 */
const GameWS = (function () {
  const BASE_URL = 'http://' + location.hostname + ':5000';
  const POLL_MS = 33;  // mirrors World::TICK_MS (matches the WS cadence)

  let timer = null;
  let running = false;
  const handlers = { hello: null, state: null };

  async function poll() {
    if (!running) {
      return;
    }
    try {
      const res = await fetch(BASE_URL + '/state');
      if (!res.ok) {
        return;
      }
      const msg = await res.json();
      if (msg.type === 'state' && handlers.state) {
        handlers.state(msg);
      }
    } catch (err) {
      // Network/CORS error: ignore and try again on the next tick.
    } finally {
      if (running) {
        timer = setTimeout(poll, POLL_MS);
      }
    }
  }

  function connect() {
    if (running) {
      return;
    }
    running = true;
    poll();
  }

  return {
    connect: connect,
    on: function (type, callback) {
      handlers[type] = callback;
    },
    send: function (obj) {
      // Fire-and-forget: gameplay events are rare and the server owns the
      // state, so a lost request only means one delayed snapshot.
      fetch(BASE_URL + '/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(obj)
      }).catch(function () {});
    },
    isOpen: function () {
      return running;
    }
  };
})();