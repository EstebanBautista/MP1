# MP1 - Registro de progreso (opencode)

Rama por diseño de hilos. `main` = fase 0 (se deja intacto).

## Ronda 1 - Spawns solapados + +1 fantasma (aplicada a las 4 ramas)
- `SPAWN_SAFE_GAP = 100.0f` (world.h/cpp): `spawnCar()` solo reusa un carril si su
  último ocupante ya salió de la zona de spawn; si todos los carriles están
  bloqueados, se omite el spawn (el timer reintenta).
- `score += 1` en el bloque "car passed below" (game.js).
- Variables `restartPending`, `lastSeenTick`, `pendingDeletionIds`; guard del
  handler de estado; `restartGame` captura el estado antes de null y limpia
  `pendingDeletionIds`; escudo marca la id borrada pendiente.
- `laneDist` eliminado; distribución local en `spawnCar()`.
- Verificado: `node --check`, `smoke.js` PASS, `spawn-check.js` 0 solapamientos
  (~363 estados por rama). El usuario reorg. la historia en cada rama
  (ej. design-4: `22d9d68` base -> `bcba66b bugs` -> `1de677e bug`).

## Ronda 2 - Bug 5: score inicial tras morir con >50 pts (aplicada a las 4 ramas)
- Causa raíz: el backend emite snapshots congelados (mismo tick/autos) cada
  33 ms aunque `world.running == false`; el guard `state.tick <= lastSeenTick`
  dejaba pasar un frame congelado tras restart -> autos fantasma -> +1 fantasma.
- Fix A: `state.tick < lastSeenTick` (estricto).
- Fix B: el guard se re-arrmoliza solo con `if (!restartPending)` (evita que un
  doble-restart rápido desarme la guardia con `lastSeenTick = 0`).
- Supuesto: partida de 1 solo tick inalcanzable (auto nace en y = -30).
- `game.js` byte-idéntico en las 4 ramas (blob `dda04ea...`). Frontend rebuildeado.
- Pendiente manual: checklist en navegador (score 0 exacto tras muerte >50 pts,
  sin fantasmas) - cubierto de nuevo en ronda 3 sobre el transporte HTTP.

## Ronda 3 - Transporte HTTP + polling (SOLO design-4, sin websockets)
Decisión del usuario: HTTP con polling (~33 ms) como alternativa WS; SSE
descartado (mantiene conexión abierta). No tocar design-1/2/3 ni main.

### Cambios (working tree actual, design-4, HEAD `1de677e`)
- `backend/server.cpp`: servidor HTTP/1.1 manual sobre ASIO standalone en el hilo
  main (aceptación síncrona, `Connection: close`). Pool + cola + `controllerThread`
  + `simMutex` intactos. Endpoints:
  - `GET /state` -> `world.stateJson()` on-demand bajo `simMutex` (estado siempre
    completo; congelado cuando el juego está detenido).
  - `POST /action` -> reusa `parseClientMessage` (protocol.{h,cpp} sin cambios)
    con el mismo switch que el viejo handler WS.
  - `OPTIONS` -> preflight CORS (`ACAO *`, `Allow-Methods: GET, POST, OPTIONS`,
    `Allow-Headers: Content-Type`, `Max-Age 86400`).
  - 404 para lo demás. Comentario de cabecera documenta el transporte alternativo.
- `backend/Dockerfile`: `git rm -r third_party/websocketpp` + fuera el `-I` (solo
  asio standalone + nlohmann quedan en el build).
- `frontend/scripts/poll.js` (nuevo): global `GameWS` con la misma API
  (on/connect/send) -> `game.js` sin cambios. Polling con `setTimeout` encadenado
  cada 33 ms (sin solapamientos); `send()` = POST fire-and-forget.
- `frontend/scripts/ws.js`: eliminado. `frontend/index.html`: carga `poll.js`.

### Verificación ronda 3
- `g++ -std=c++14 -O2 -DASIO_STANDALONE` compila OK en la imagen (sin websocketpp).
- `http-smoke.js` (en %TEMP%\opencode) ALL PASS: GET /state + headers CORS,
  preflight OPTIONS, start/ticks, spawn espontáneo, slowmo on/off, game_over
  congela tick y autos, restart deja tick<y autos vacíos + sigue avanzando,
  removeCar elimina por id, 60 polls rápidos sin bodys rotos.
- Stack en design-4: `enemy-server` (5000) + `game-client` (8080) up; index sirve
  poll.js, ws.js da 404.

### Pendiente
- Checklist manual en navegador sobre la versión HTTP de design-4:
  score en 0 tras morir con >50 pts y reiniciar, sin autos fantasma, un solo
  PROTECTED!, spawns sin solapar (tecla B), slowmo visual; consola sin errores
  de red/CORS; jugar 30-60 s para sentir el polling.
- Commit de la ronda 3 (solo design-4) + push si el usuario lo pide.