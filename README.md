# MP1 — Enemy Cars: Diseño 2 — Hilo Único de Actualización

Esta rama implementa el **Diseño 2: Hilo Único de Actualización** del
MINIPROYECTO  1 de Programación Paralela (300CIP013, 2026-II).

> Para la arquitectura general del proyecto (común a los 4 diseños), el
> análisis completo y la comparación entre diseños, ver el
> [README de `main`](../../tree/main) y el informe.

## Qué hace esta rama distinto

Un único hilo (`simThread`) actualiza **todos** los vehículos de forma
secuencial en cada tick, sin ningún paralelismo en el cómputo de movimiento.
Es el diseño más simple de los 4: sin overhead de creación de hilos ni
reparto de trabajo entre workers. Sirve como línea base de comparación de
rendimiento contra los otros tres diseños.

## Cómo correr

```bash
git checkout design-2
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

- `backend/server.cpp` — lógica de threading de este diseño específico (un
  solo hilo de simulación)
- `backend/src/world.cpp` / `world.h` — estado del juego (compartido con las
  otras ramas)
- `backend/src/protocol.cpp` / `protocol.h` — parseo de mensajes del cliente
  (compartido con las otras ramas)