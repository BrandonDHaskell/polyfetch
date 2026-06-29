# polyfetch server

The payload server used as a shared test fixture by all four polyfetch clients.
It is not part of the experiment; it is background infrastructure.

## What it does

Serves deterministic synthetic byte payloads generated on the fly. Each payload
derives its bytes from a seeded PRNG (PCG) keyed on the target ID, so identical
requests always produce identical bytes across restarts and across clients. The
matching SHA-256 is therefore stable and acts as the cross-language correctness
anchor.

## Running in Docker (normal)

From the repo root:

    docker compose up -d

The container is pinned to cores 0-3 and listens on `127.0.0.1:8080`. The
`--wait` flag (used in `make up`) blocks until the healthcheck passes.

## Running standalone (debugging)

    cd server
    go build -o polyfetch-server .
    ./polyfetch-server

Flags:

    -addr string    listen address (default ":8080")
    -quiet          suppress per-request access log
    -healthcheck    probe the server and exit 0/1; used by Docker healthcheck

## API

### GET /payload/{id}

Returns a deterministic byte stream for the given `id`.

| Parameter | Type     | Default   | Description                            |
|-----------|----------|-----------|----------------------------------------|
| `size`    | int >= 0 | 1048576   | Payload size in bytes                  |
| `delay`   | int >= 0 | 0         | Milliseconds to wait before responding |
| `status`  | int      | 200       | HTTP status code to return (100-599)   |

Non-2xx responses return an empty body. Validation errors (bad parameter values)
return 400 with a plain-text message.

Examples:

    GET /payload/00001                          # 1 MiB, immediate, 200 OK
    GET /payload/00001?size=65536               # 64 KiB
    GET /payload/00001?size=1048576&delay=200   # 1 MiB, 200ms latency
    GET /payload/00001?status=500               # error response, empty body

### GET /health

Returns `ok` with status 200. Used by the Docker healthcheck and by
`-healthcheck` mode.
