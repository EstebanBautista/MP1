# MP1 — Enemy Cars: Diseño 3 — Hilo por Tipo de Vehículo

Esta rama implementa el **Diseño 3: Hilo por Tipo de Vehículo** del
MINIPROYECTO  1 de Programación Paralela (300CIP013, 2026-II).

> Para la arquitectura general del proyecto (común a los 4 diseños), el
> análisis completo y la comparación entre diseños, ver el
> [README de `main`](../../tree/main) y el informe.

## Qué hace esta rama distinto

Se crean 5 hilos fijos, uno por cada tipo/color de vehículo. Cada hilo
recorre el vector completo de autos, pero solo mueve los que coinciden con su
tipo asignado. El número de hilos no crece con la cantidad de vehículos (a
diferencia del Diseño 1), pero el paralelismo real depende de cómo se
distribuyen los tipos entre los autos activos en cada momento.

## Cómo correr

```bash
git checkout design-3
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

- `backend/server.cpp` — lógica de threading de este diseño específico (5
  hilos, uno por tipo)
- `backend/src/world.cpp` / `world.h` — estado del juego (compartido con las
  otras ramas)
- `backend/src/protocol.cpp` / `protocol.h` — parseo de mensajes del cliente
  (compartido con las otras ramas)