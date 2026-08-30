## Objective
Implementar el micro-proyecto MP1 de programación paralela: el backend procesa el movimiento de los vehículos enemigos con hilos (4 diseños, uno por rama) y el frontend consume las posiciones vía WebSocket. Diseños implementados en orden 2→3→1→4.

## Important Details
- **Pila**: C++ websocketpp + asio standalone (1.22.1) + nlohmann/json, vendored en `backend/third_party/`. Docker build: `g++ -std=c++14 -O2 -DASIO_STANDALONE ... -pthread`.
- **Protocolo**: server→client `hello`, `state` (tick, enemySpeed, msToRelease, evaded, slowmo, cars[]); client→server `start`, `restart`, `game_over`, `removeCar`, `slowmo`.
- **Endpoint**: `ws://${location.hostname}:5000`.
- **Enunciado**: `MP1_2026.md` — Diseño 1 = hilos independientes (1 hilo por vehículo); Diseño 2 = hilo único de actualización; Diseño 3 = hilo por tipo de vehículo; Diseño 4 = grupo fijo de hilos + cola de tareas. Orden de implementación aprobado por el usuario: 2→3→1→4. Un commit por diseño, en su rama, sin merges.
- Después de Fase 0 en main se cortaron las 4 ramas de diseño (todas parten del commit de Fase 0).
- El smoke test (`%TEMP%\opencode\smoke.js`) usa el WebSocket nativo de Node: hello → start → ~30 estados → slowmo on/off → restart; valida `states>30` y `maxY>-29` (los autos avanzan).

## Work State
### Completed
- **Fase 0 (main, commit 476eb66)**: backend `server.cpp` + `src/{car.h,world.h,world.cpp,protocol.h,protocol.cpp}` (simulación integra en servidor), frontend migrado (ws.js, game.js/car.js/config.js/index.html), README `ws://`, deps vendored. Smoke PASS.
- **Rama design-2 (a7a8cc7) `Diseno 2: hilo unico de actualizacion`**: un `simThread` corre `World::update` a ~30 Hz; `simMutex` serializa simulación y manejadores; outbox entrega snapshots al hilo de asio. PASS.
- **Rama design-3 (8c8d030) `Diseno 3: hilo por tipo`**: TYPES (5) workers; cada uno mueve los autos de su tipo en la fase paralela; controlador mantiene `simMutex` todo el tick. World extendido con granos `beginTick/moveCar/endTick`. PASS.
- **Rama design-1 (1cfd08f) `Diseno 1: hilos independientes`**: por tick se crea un `std::thread` por carro (`world.moveCar(world.cars[i], mult)`), join al final; demuestra el costo de O(cars) hilos a 30 Hz. PASS.
- **Rama design-4 (22d9d68) `Diseno 4: pool + cola`**: pool fijo de K hilos (min(16, max(2, núcleos))), cola compartida `MoveTask{index,mult}`, `pendingTasks` atómico + `doneCv` para fin de tanda, `workersStop` para shutdown limpio. PASS con estrés de 2 clientes a 8 s.

### Active
- Ninguno. Stack docker corriendo con la imagen de **main** (Fase 0) actualizado en el último paso.

### Blocked
- Ninguno.

## Next Move
Nada pendiente de código. Siguientes pasos opcionales (solo si el usuario lo pide):
1. Push de `main` y las 4 ramas a `origin` (`git push -u origin --all`).
2. Crear el informe PDF solicitado en MP1_2026.md (descripción por diseño, sincronización, resultados de las pruebas).
3. Responder las preguntas del enunciado (núcleos, agregar vehículos, miles de vehículos, carreras, estructuras) para cada diseño.
4. Si se quiere probar un diseño frente a otro, `git checkout design-N` + `docker compose up --build -d backend`.

## Relevant Files
- `C:\Users\ESTEBAN\Desktop\MP1\backend\server.cpp` : punto de entrada (cambia por rama; main = Fase 0)
- `C:\Users\ESTEBAN\Desktop\MP1\backend\src\world.{h,cpp}` : en ramas 3/1/4 agrega granos `beginTick/moveCar/endTick` + `slowmoMultiplier`
- `C:\Users\ESTEBAN\Desktop\MP1\frontend\scripts\game.js`, `ws.js`, `car.js`, `config.js` : frontend servido por nginx en :8080
- `C:\Users\ESTEBAN\Desktop\MP1\MP1_2026.md` : enunciado con las definiciones de los 4 diseños y preguntas de análisis
- Smoke test: `C:\Users\ESTEBAN\AppData\Local\Temp\opencode\smoke.js`