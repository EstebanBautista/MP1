# MP1 — Enemy Cars: Diseño 4 — Pool de Hilos + Cola de Tareas

Esta rama implementa el **Diseño 4: Hilo Asíncrono para Vehículos** del
MINIPROYECTO  1 de Programación Paralela (300CIP013, 2026-II).

> Para la arquitectura general del proyecto (común a los 4 diseños), el
> análisis completo y la comparación entre diseños, ver el
> [README de `main`](../../tree/main) y el informe.

## Qué hace esta rama distinto

Un grupo fijo de hilos trabajadores (acotado al hardware disponible) consume
tareas de movimiento desde una cola compartida, patrón productor-consumidor.
Los vehículos no tienen un hilo permanente asignado — cualquier worker libre
puede tomar la siguiente tarea de la cola.

**Esta es la única rama sin WebSocket**: usa HTTP polling como transporte
entre backend y frontend, en vez de una conexión persistente. Ver el
comentario de cabecera de `server.cpp` para el detalle de por qué se tomó
esta decisión y cómo funciona el polling.

## Cómo correr

```bash
git checkout design-4
```
```bash
docker compose down   # si viene de otra rama
```
```bash
docker compose up --build
```
Abrí `http://localhost:8080` — hard refresh (`Ctrl+Shift+R`) o modo incógnito
si venías de probar otra rama.

## Archivos clave de este diseño

- `backend/server.cpp` — lógica de threading de este diseño específico (pool
  + cola de tareas) y el servidor HTTP de polling
- `backend/src/world.cpp` / `world.h` — estado del juego (compartido con las
  otras ramas)
- `backend/src/protocol.cpp` / `protocol.h` — parseo de mensajes del cliente
  (compartido con las otras ramas)
- `frontend/scripts/poll.js` — reemplaza a `ws.js`; hace polling HTTP en vez
  de mantener una conexión WebSocket