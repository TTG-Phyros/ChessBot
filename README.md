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
- `eval/basic.c`: basic material evaluation profile
- `eval/advanced.c`: advanced evaluation profile (material + positional + mobility)
- `eval/tactical.c`: tactical profile (pressure + attacking opportunities)
- `eval/phased.c`: phased profile (PST + king safety + pawn structure + mobility)
- `eval/select.c`: runtime evaluation profile selector
- `bots/minimax/src/minimax.c`: alpha-beta negamax search
- `bots/minimax/src/main.c`: CLI entrypoint + protocol mode (`--protocol`) used by bot adapter
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
4. The selected bot executable computes and returns JSON.
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

## Bot debug logs

When debug mode is enabled from the web UI (`debug=true`), bot logs are written outside Docker to:

- `./logs` on the host (repo root)

The `bot` service mounts this folder as `/app/logs` in the container.

Each run creates a dated file with algorithm/eval/depth in the name:

- `logs/YYYYMMDD-HHMMSS_<bot>_<eval>_d<depth>_<label>.log`

Each file starts with a banner containing:

- algorithm name
- evaluation profile
- runtime parameters (depth and mode)
- start timestamp

## Testing

The project contains two complementary test families.

### A) Unit tests (logic/regression)

Build + run:

```bash
make test
```

This executes core-rule test suites and checks things like:

- legal moves from start position
- checkmate detection
- castling generation
- en passant legality/application

This now runs shared core tests for both bot variants:

- `./test-engine-core-minimax`
- `./test-engine-core-iterdeep`

### B) Perft tests (move generation correctness)

Build + run:

```bash
make test-perft
```

Perft compares generated node counts against known reference counts on multiple chess positions. This is the strongest guardrail against subtle move generation bugs.

This now runs perft for both bot variants:

- `./test-perft-minimax`
- `./test-perft-iterdeep`

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

## Elo evaluation versus Stockfish

You can estimate your bot Elo by running automatic matches against Stockfish at multiple `UCI_Elo` strengths, then fitting a single Elo value from the observed scores.

Prerequisites:

- Build bot binaries: `make all`
- Install Python dependency: `pip install python-chess`
- Install a UCI opponent engine (Stockfish, Rodent, Komodo) and ensure it is available on PATH (or pass `--stockfish <path>`)

Quick run (default bot: minimax):

```bash
make elo-eval
```

Docker run:

```bash
docker compose --profile eval run --rm elo-eval
```

Ready-to-run low-strength Rodent run (movetime presets):

```bash
docker compose --profile eval run --rm eval-rodent
```

Before running `eval-rodent`, place a Linux Rodent UCI binary in `./engines` as `./engines/rodent` (not a Windows `.exe`).
If you only have `rodent.exe`, run the evaluator directly on Windows (outside Docker).

All Docker eval settings are read from the root `.env` file:

- `EVAL_BOT` (`minimax` or `iterdeep`)
- `EVAL_BOT_DEPTH`
- `EVAL_BOT_EVAL` (`basic`, `advanced`, `tactical`, or `phased`)
- `EVAL_PARALLEL_GAMES` (number of concurrent games per level)
- `EVAL_BOT_THREADS` (bot search threads per game, via `BOT_THREADS`)
- `EVAL_OPPONENT_ENGINE` (for example `stockfish`, `/app/engines/rodent`, `/app/engines/komodo`)
- `EVAL_OPPONENT_KIND` (`auto`, `stockfish`, `rodent`, `komodo`)
- `EVAL_LEVELS` (supports `elo:N`, `skill:N`, `depth:N`, `movetime:N`)
- `EVAL_GAMES_PER_LEVEL`
- `EVAL_SF_MOVETIME_MS`
- `EVAL_SF_HASH`
- `EVAL_SF_THREADS`
- `EVAL_MAX_PLIES`
- `EVAL_STOP_IF_SCORE_RATE_BELOW` (default `0.40`; set `< 0` to disable early stop)
- `EVAL_PGN_DIR` (directory for one PGN file per played game)
- `EVAL_OUTPUT_JSON`

Rodent preset settings are also available in `.env` under `EVAL_RODENT_*`.
Rodent profile also supports `EVAL_RODENT_PARALLEL_GAMES` and `EVAL_RODENT_BOT_THREADS`.
Rodent profile PGNs are controlled by `EVAL_RODENT_PGN_DIR`.

The evaluator can stop early: if score rate on a tested level is below the configured threshold,
higher levels are skipped (defaults to `0.40`).

Docker run with custom settings:

```bash
docker compose --profile eval run --rm elo-eval --bot=iterdeep --bot-depth=5 --bot-eval=phased --opponent-kind=rodent --stockfish=/app/engines/rodent --levels=elo:600,elo:800,elo:1000,elo:1200 --games-per-level=12 --output-json=logs/elo-iterdeep.json
```

Direct script usage:

```bash
python tools/elo_eval.py --bot=minimax --bot-depth=4 --bot-eval=basic --opponent-kind=komodo --stockfish=/path/to/komodo --levels=elo:600,elo:800,elo:1000,elo:1200 --games-per-level=8 --output-json logs/elo-report.json
```

PowerShell example:

```powershell
python .\tools\elo_eval.py --bot iterdeep --bot-depth 5 --bot-eval phased --opponent-kind rodent --stockfish .\engines\rodent.exe --levels elo:600,elo:800,elo:1000,elo:1200 --games-per-level 12 --output-json logs\elo-iterdeep.json
```

Notes:

- The script alternates colors each game and uses a fixed opponent move time (`--sf-movetime-ms`, default 120 ms) unless `depth:N` or `movetime:N` is used in levels.
- The evaluator supports multiprocessing with `--parallel-games` (or env `EVAL_PARALLEL_GAMES`).
- Bot search supports root-level OpenMP parallelism through `BOT_THREADS` (wired via compose env vars above).
- Every played game can be exported as an individual PGN file (enabled by default via `EVAL_PGN_DIR` / `EVAL_RODENT_PGN_DIR`).
- For low Elo below Stockfish limits, use Rodent/Komodo as opponent and configure `EVAL_LEVELS` with `elo:N` values.
- Some Rodent builds expose `UCI_Elo` with a minimum (often 800). Keep Rodent Elo levels at `elo:800` or above.
- `eval-rodent` now ships with defaults: `elo:800,elo:900,elo:1000,elo:1100,elo:1200,elo:1300,elo:1400` (adjust with `EVAL_RODENT_LEVELS`).
- Early-stop threshold for Rodent profile is controlled by `EVAL_RODENT_STOP_IF_SCORE_RATE_BELOW`.
- Per-level report includes W/D/L, score rate, and performance Elo.
- Final estimate is an overall maximum-likelihood Elo across all tested opponent levels.
- The bot protocol now supports `go` in `--protocol` mode so automation can play either color.

## Notes

- Engine depth is controlled by `BOT_DEPTH` (default fallback is 4 when unset/invalid)
- Evaluation profile is controlled by `BOT_EVAL` (`basic`, `advanced`, `tactical`, or `phased`, default `basic`)
- API request timeout is controlled by `BOT_TIMEOUT_SECONDS` (default 60)
- Web UI applies your move immediately, then shows a "bot is thinking" indicator while waiting for response
- No opening book or endgame tablebase yet
- Promotion defaults to queen unless specified in UCI
