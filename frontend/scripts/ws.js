/**
 * GameWS
 * Thin WebSocket client for the PP Racing backend.
 *
 * The backend is the authority over enemy positions, difficulty and spawns.
 * The frontend renders the positions received in each `state` message and
 * notifies the backend about gameplay events (start, restart, game over,
 * shield destruction, slow-motion).
 *
 * Protocol:
 *   server -> client  {"type":"hello",...}
 *                     {"type":"state", ...}
 *   client -> server  {"type":"start"} | {"type":"restart"} | {"type":"game_over"}
 *                     {"type":"removeCar","id":N}
 *                     {"type":"slowmo","active":true|false}
 */
const GameWS = (function () {
  const url = GameConfig.WS_URL;

  let socket = null;
  let retries = 0;
  const handlers = { hello: null, state: null };

  function scheduleReconnect() {
    const delay = Math.min(8000, 1500 * (retries + 1));
    setTimeout(connect, delay);
  }

  function connect() {
    try {
      socket = new WebSocket(url);
    } catch (err) {
      scheduleReconnect();
      return;
    }

    socket.onopen = () => {
      retries = 0;
    };

    socket.onmessage = (event) => {
      let msg;
      try {
        msg = JSON.parse(event.data);
      } catch (err) {
        return;
      }
      if (msg.type === 'hello' && handlers.hello) {
        handlers.hello(msg);
      } else if (msg.type === 'state' && handlers.state) {
        handlers.state(msg);
      }
    };

    socket.onclose = () => {
      socket = null;
      scheduleReconnect();
    };

    socket.onerror = () => {
      try {
        socket.close();
      } catch (err) {
        // ignore
      }
    };
  }

  return {
    connect: connect,
    on: function (type, callback) {
      handlers[type] = callback;
    },
    send: function (obj) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        try {
          socket.send(JSON.stringify(obj));
        } catch (err) {
          // ignore
        }
      }
    },
    isOpen: function () {
      return socket !== null && socket.readyState === WebSocket.OPEN;
    }
  };
})();