# MiniMax Chess Bot (C + API + Web)

A complete chess project built around a C engine with a split backend:

- Chess engine in C (legal move generation + Minimax)
- API proxy in C (libmicrohttpd + libcurl)
- Bot adapter in C (libmicrohttpd + json-c)
- React web UI for playing against the engine
- CLI mode for terminal play

## Why this project is cool

- Full legal move generation, including castling and en passant
- Promotion support (Q, R, B, N)
- Check, checkmate, and stalemate detection
- Alpha-beta Minimax search
- Simple API for integration and UI clients
- Automated tests:
  - classical unit tests
  - perft validation tests against known chess node counts

## Project layout

### Engine and backend

- `include/chess.h`: shared public API
- `src/board.c`: board state, FEN parser, UCI parser
- `src/movegen.c`: legal move generation and check detection
- `src/eval.c`: static evaluation
- `src/minimax.c`: alpha-beta negamax search
- `src/main.c`: CLI entrypoint + protocol mode (`--protocol`) used by bot adapter
- `src/api.c`: HTTP API proxy (no engine logic)
- `src/bot_service.c`: bot HTTP adapter (stdin/stdout bridge to `chess-bot --protocol`)

### Tests

- `tests/test_minimax.c`: unit tests for move generation/rules
- `tests/test_perft.c`: perft suite on reference positions

### Frontend

- `web/src/ChessBoard.js`: board container and game flow
- `web/src/Square.js`: square component
- `web/src/*.css`: UI styling

### Containers

- `Dockerfile.api`: builds/runs API backend
- `Dockerfile.bot`: builds/runs bot adapter + bundled `chess-bot`
- `Dockerfile.cli`: builds/runs CLI binary
- `web/Dockerfile`: builds/runs React app
- `docker-compose.yml`: multi-service orchestration

## Quick start (recommended)

Run the full stack:

```bash
docker compose up --build
```

Depth is configured from `.env` at repo root.

```env
BOT_DEPTH=5
```

Optional API timeout config (compose):

```env
BOT_TIMEOUT_SECONDS=60
```

Then open:

- Web UI: http://localhost:3000
- API: http://localhost:5000/api/board

Behind the scenes:

- API proxy: port 5000
- Bot adapter: internal port 5001
- Engine process: `chess-bot --protocol` launched by bot adapter

Request flow for a move:

1. Web sends `POST /api/move`.
2. API proxy forwards to bot adapter (`/bot/move`).
3. Bot adapter writes `move <uci>` to `chess-bot --protocol`.
4. `main.c` computes and returns JSON.
5. Adapter returns JSON to API, API returns JSON to web.

## Ways to launch

### 1. Full project with Docker Compose

```bash
docker compose up --build
```

### 2. Only API + Web with Docker Compose

```bash
docker compose up --build bot api web
```

### 3. CLI bot in Docker

```bash
docker compose run --rm chess-bot
```

### 4. Local CLI build/run

```bash
make chess-bot
./chess-bot
```

Depth range is 1 to 8. You can pass an argument (`./chess-bot 6`) or set `BOT_DEPTH`.

### 5. Local API proxy build/run

Dependencies: `libmicrohttpd`, `json-c`, `libcurl`, `pkg-config`

```bash
make chess-api
./chess-api
```

API proxy listens on port `5000`.

### 5b. Local bot adapter build/run

Dependencies: `libmicrohttpd`, `json-c`, `pkg-config`

```bash
make chess-bot-service
BOT_DEPTH=5 ./chess-bot-service
```

Bot adapter listens on port `5001` and starts `./chess-bot --protocol`.

### 6. Local web frontend

Dependencies: Node.js 18+

```bash
cd web
npm install
npm start
```

The app runs on http://localhost:3000.

## Testing

The project contains two complementary test families.

### A) Unit tests (logic/regression)

Build + run:

```bash
make test
```

This executes `tests/test_minimax.c` and checks things like:

- legal moves from start position
- checkmate detection
- castling generation
- en passant legality/application

### B) Perft tests (move generation correctness)

Build + run:

```bash
make test-perft
```

Perft compares generated node counts against known reference counts on multiple chess positions. This is the strongest guardrail against subtle move generation bugs.

Helpful runtime flags for perft:

- `PERFT_MAX_DEPTH`: cap depth between 1 and 6
- `PERFT_STRICT=1`: fail with non-zero exit code on mismatch

Examples:

```bash
# Faster check (lower depth)
PERFT_MAX_DEPTH=4 make test-perft

# Strict CI-style check
PERFT_STRICT=1 make test-perft
```

PowerShell equivalents:

```powershell
$env:PERFT_MAX_DEPTH = "4"; make test-perft
$env:PERFT_STRICT = "1"; make test-perft
```

### Run tests in Docker

Unit tests through compose profile:

```bash
docker compose --profile test run --rm tests
```

Perft in Docker CLI image:

```bash
docker compose run --rm chess-bot make test-perft
```

## API reference

- `GET /api/board`: current board state and legal moves
- `POST /api/move`: apply a player move, then engine responds
- `POST /api/reset`: reset to start position

The API process is a proxy. The bot adapter does not run chess logic itself: it forwards commands to `chess-bot --protocol` (from `src/main.c`) and returns that process output.

Internal endpoints (not for frontend clients):

- `GET /bot/board`
- `POST /bot/move`
- `POST /bot/reset`

Example move request:

```json
{
  "uci": "e2e4"
}
```

## CLI usage

- Enter moves in UCI format: `e2e4`, `g1f3`, `e7e8q`
- Type `quit` to stop

## Notes

- Engine depth is controlled by `BOT_DEPTH` (default fallback is 4 when unset/invalid)
- API request timeout is controlled by `BOT_TIMEOUT_SECONDS` (default 60)
- Web UI applies your move immediately, then shows a "bot is thinking" indicator while waiting for response
- No opening book or endgame tablebase yet
- Promotion defaults to queen unless specified in UCI
