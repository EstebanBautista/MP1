# MP1 — Enemy Cars: Multi-threading and Shared-Memory Ideas

MINIPROYECTO 1 · Programación Paralela (300CIP013) · 2026-II

Juego cliente-servidor donde el **backend en C++** genera y mueve vehículos
enemigos usando 4 estrategias distintas de concurrencia (una por rama de este
repositorio), y el **frontend en PixiJS** los renderiza en el navegador. El
backend es la única fuente de verdad: el frontend solo dibuja lo que el
servidor le manda.

---

## Cómo está organizado este repositorio

Este repositorio NO tiene toda la implementación en `main`. Cada uno de los 4 diseños
de concurrencia vive en su **propia rama**, completa y ejecutable de forma
independiente:

| Rama&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | Diseño | Resumen |
|---|---|---|
| [`main`](../../tree/main) | — | — |
| [`design-1`](../../tree/design-1) | Hilos Independientes | Un `std::thread` por cada vehículo, creado y destruido en cada tick. |
| [`design-2`](../../tree/design-2) | Hilo Único de Actualización | Un solo hilo mueve todos los vehículos, secuencialmente. |
| [`design-3`](../../tree/design-3) | Hilo por Tipo de Vehículo | Un hilo fijo por cada uno de los 5 tipos/colores de auto. |
| [`design-4`](../../tree/design-4) | Pool de Hilos + Cola de Tareas | Grupo fijo de workers que toman tareas de una cola compartida (productor-consumidor). **Única rama sin WebSocket** — usa HTTP polling. |

---

## Cómo correr cualquiera de los diseños

1. Cloná el repositorio y pará en la rama que quieras probar:
```bash
   git clone https://github.com/EstebanBautista/MP1.git
```
```bash
   cd MP1
```
```bash
   git checkout design-2   # o design-1 / design-3 / design-4
```

2. Levantá los contenedores:
```bash
   docker compose up --build
```
   El `--build` es obligatorio la primera vez y cada vez que cambies de rama —
   si no, Docker puede seguir usando una imagen vieja compilada de otro diseño.

3. Abrí el juego en el navegador:
```
   http://localhost:8080
```

4. **Importante**: si viene de probar otra rama, haga un **hard refresh**
   (`Ctrl+Shift+R`) o abra en **modo incógnito** — el navegador cachea los
   archivos `.js` y puede mostrarle comportamiento de la rama anterior si no
   lo fuerza a recargar todo de cero.

### Si vas a cambiar de rama para probar otro diseño

```bash
docker compose down
git checkout design-3
docker compose up --build
```
Y de nuevo, hard refresh en el navegador después.

---

## Arquitectura común a los 4 diseños

- **Backend** (`backend/`): C++, WebSocket (`websocketpp` + Asio standalone,
  vendorizados en `backend/third_party/` para builds reproducibles sin
  depender de internet), JSON (`nlohmann/json`).
  - `server.cpp` — arranca el servidor y contiene la lógica de threading
    **específica de cada diseño** (es el único archivo que cambia
    sustancialmente entre ramas).
  - `src/world.cpp` / `world.h` — estado del juego: vehículos, spawn,
    dificultad, serialización a JSON. Compartido por los 4 diseños.
  - `src/protocol.cpp` / `protocol.h` — traduce los mensajes JSON del cliente
    (`start`, `restart`, `removeCar`, `slowmo`) a tipos de C++.
  - `src/car.h` — estructura de datos de un vehículo.

- **Frontend** (`frontend/`): PixiJS. Solo renderiza el estado que recibe del
  backend y maneja la entrada del jugador (controles, colisiones visuales,
  power-ups, puntaje). No calcula posiciones de enemigos por su cuenta.

- **Transporte**: WebSocket en `design-1`, `design-2` y `design-3`. La rama
  `design-4` usa HTTP polling en su lugar — ver el comentario de cabecera de
  su `server.cpp` para el detalle de por qué.

---

