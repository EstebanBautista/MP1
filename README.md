# Enemy Cars Threading Project

## Overview

This is a micro project designed to teach parallel programming students how to use threads (std::thread or pthread) in a client-server architecture.

The objective is to implement a system where enemy cars are generated and their positions are updated asynchronously using threads. The backend server is responsible for generating movement data, while the frontend displays the enemy cars and applies the received positions.

The project focuses on the practical application of parallel programming concepts such as thread creation, task decomposition, synchronization, and communication between concurrent components.

By completing this project, students will learn how to:

- Create and manage parallel execution using: C++ std::thread or POSIX pthread
- Decompose a problem into independent tasks.
- Design thread-based solutions for real-time simulations.
- Manage shared data between threads.

## Technologies

Backend
- C++
- POSIX Threads (pthread) or C++ Threads (std::thread)
- Socket Programming
- Concurrent Programming Concepts

Frontend
- JavaScript
- HTML5 Canvas / PixiJS

Infrastructure
- Docker
- Docker Compose

## Start the application

Build and execute both services:

```bash
docker compose up --build
```

Services:

Frontend:
http://localhost:8080

Backend:
wss://localhost:5000

## General Assignment Tasks

Students must implement a parallel solution where enemy car movement is handled by threads.

The solution must:

- Create worker threads.
- Assign movement tasks to threads.
- Update enemy positions concurrently.
- Send updated states to the frontend.

More details in course assignment

## Submision

- Students must create a personal fork of the project repository.
- Send PDF report with
    - A short description of your implementation.
    - The threading approach used.
    - Any synchronization mechanisms implemented.
    - Testing results.

This project connects theoretical concepts from parallel programming with a practical simulation environment.