# Architecture

## Request lifecycle

1. `main.c` parses configuration and starts the server.
2. `server.c` creates a TCP listening socket.
3. The accept loop receives client connections.
4. Each socket becomes a job in the thread-pool queue.
5. A worker removes the job.
6. The worker parses the HTTP request.
7. `GET` requests are mapped to a file below the document root.
8. The file is streamed to the client.
9. The worker closes the connection and waits for another job.

## Synchronization

The queue is protected by a pthread mutex. Workers wait on `not_empty` when there is no work. The submitting thread waits on `not_full` when the queue reaches its configured capacity.

Shutdown broadcasts both condition variables and joins every worker before destroying the pool.

## Scope

The server intentionally supports a small HTTP subset. It is intended to demonstrate:

- TCP socket programming
- HTTP request/response basics
- pthreads
- producer/consumer synchronization
- a fixed worker pool
- filesystem I/O
- graceful shutdown
