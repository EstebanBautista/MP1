# MP1 — Enemy Cars: Diseño 1 — Hilos Independientes

Esta rama implementa el **Diseño 1: Hilos Independientes** del MINIPROYECTO 1 de Programación Paralela (300CIP013, 2026-II).

> Para la arquitectura general del proyecto (común a los 4 diseños), el
> análisis completo y la comparación entre diseños, ver el
> [README de `main`](../../tree/main) y el informe.

## Qué hace esta rama distinto

Cada vehículo enemigo tiene **su propio hilo de ejecución**, creado y
destruido en cada tick del servidor (~30 veces por segundo). El hilo
controlador crea un `std::thread` por cada auto activo, espera (`join`) a que
todos terminen, y recién ahí continúa con la limpieza y la dificultad. Es el
diseño con mayor overhead de creación de hilos de los 4 — pensado justamente
para evidenciar ese costo. Ver el comentario de cabecera de `server.cpp` para
el detalle de sincronización.

## Cómo correr

```bash
git checkout design-1
docker compose down   # si venías de otra rama
docker compose up --build
```
Abrí `http://localhost:8080` — hard refresh (`Ctrl+Shift+R`) o modo incógnito
si venías de probar otra rama.

## Archivos clave de este diseño

- `backend/server.cpp` — lógica de threading de este diseño específico (un
  hilo por vehículo)
- `backend/src/world.cpp` / `world.h` — estado del juego (compartido con las
  otras ramas)
- `backend/src/protocol.cpp` / `protocol.h` — parseo de mensajes del cliente
  (compartido con las otras ramas)