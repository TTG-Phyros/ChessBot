#!/usr/bin/env python3
"""Run bot-vs-Stockfish matches and estimate bot Elo from win rate."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    import chess
    import chess.engine
    import chess.pgn
except ImportError as exc:  # pragma: no cover - import guard
    raise SystemExit(
        "Missing dependency 'python-chess'. Install it with: pip install python-chess"
    ) from exc


@dataclass
class GameResult:
    level_label: str
    level_order: int
    opponent_elo: int
    game_index: int
    bot_color: str
    result: str
    score: float
    plies: int
    pgn_text: str
    pgn_file: str


class BotProtocolClient:
    def __init__(self, bot_binary: str, bot_depth: int, bot_eval: str) -> None:
        bot_env = os.environ.copy()
        bot_env["BOT_EVAL"] = bot_eval

        self._proc = subprocess.Popen(
            [bot_binary, "--protocol", str(bot_depth)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            env=bot_env,
        )

        if self._proc.stdin is None or self._proc.stdout is None:
            raise RuntimeError("Failed to open bot protocol pipes")

    def _send(self, command: str) -> dict[str, Any]:
        if self._proc.poll() is not None:
            err = self._proc.stderr.read() if self._proc.stderr else ""
            raise RuntimeError(f"Bot process terminated unexpectedly. stderr={err!r}")

        assert self._proc.stdin is not None
        assert self._proc.stdout is not None

        self._proc.stdin.write(command + "\n")
        self._proc.stdin.flush()

        line = self._proc.stdout.readline()
        if not line:
            err = self._proc.stderr.read() if self._proc.stderr else ""
            raise RuntimeError(f"No response from bot for command {command!r}. stderr={err!r}")

        try:
            payload = json.loads(line)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"Invalid bot JSON response: {line!r}") from exc

        if "error" in payload:
            raise RuntimeError(f"Bot returned error for command {command!r}: {payload['error']}")

        return payload

    def reset(self) -> dict[str, Any]:
        return self._send("reset")

    def move(self, uci_move: str) -> dict[str, Any]:
        return self._send(f"move {uci_move}")

    def go(self) -> dict[str, Any]:
        return self._send("go")

    def close(self) -> None:
        if self._proc.poll() is not None:
            return
        try:
            self._send("quit")
        except Exception:
            pass
        try:
            self._proc.wait(timeout=1.5)
        except subprocess.TimeoutExpired:
            self._proc.kill()


@dataclass
class StockfishLevel:
    label: str
    opponent_elo: int
    mode: str
    value: int





def parse_levels(raw_levels: str) -> list[StockfishLevel]:
    levels: list[StockfishLevel] = []
    for token in raw_levels.split(","):
        token = token.strip()
        if not token:
            continue

        if token.lower().startswith("elo:"):
            token = token.split(":", 1)[1].strip()

        try:
            elo = int(token)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(
                f"Invalid level token '{token}'. Use values like 1200 or elo:1200."
            ) from exc
        if elo < 100 or elo > 4000:
            raise argparse.ArgumentTypeError(
                f"UCI_Elo value {elo} is outside expected range [100, 4000]."
            )
        levels.append(
            StockfishLevel(
                label=f"UCI_Elo {elo}",
                opponent_elo=elo,
                mode="elo",
                value=elo,
            )
        )

    if not levels:
        raise argparse.ArgumentTypeError("At least one level is required.")

    return levels


def expected_score(bot_elo: float, opp_elo: float) -> float:
    return 1.0 / (1.0 + 10.0 ** ((opp_elo - bot_elo) / 400.0))


def solve_mle_elo(stats: list[dict[str, Any]]) -> float | None:
    total_games = sum(s["games"] for s in stats)
    if total_games == 0:
        return None

    rating = sum(s["opponent_elo"] * s["games"] for s in stats) / total_games
    k = math.log(10.0) / 400.0

    for _ in range(48):
        g = 0.0
        h = 0.0
        for row in stats:
            n = row["games"]
            if n == 0:
                continue
            s = row["score_rate"]
            e = expected_score(rating, row["opponent_elo"])
            g += n * (s - e)
            h -= n * k * e * (1.0 - e)

        if abs(g) < 1e-9 or abs(h) < 1e-12:
            break

        delta = g / h
        rating -= delta
        if abs(delta) < 1e-6:
            break

    return rating


def performance_from_score(score_rate: float, opp_elo: float) -> float:
    eps = 1e-9
    p = min(max(score_rate, eps), 1.0 - eps)
    return opp_elo + 400.0 * math.log10(p / (1.0 - p))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Estimate bot Elo by running matches against Stockfish with configured UCI_Elo levels."
    )
    parser.add_argument(
        "--bot",
        choices=["minimax", "iterdeep"],
        default="minimax",
        help="Bot variant to evaluate.",
    )
    parser.add_argument(
        "--bot-binary",
        default="",
        help="Optional explicit bot binary path. Defaults to ./chess-bot or ./chess-bot-iterdeep.",
    )
    parser.add_argument("--bot-depth", type=int, default=4, help="Bot search depth (1-8).")
    parser.add_argument(
        "--bot-eval",
        choices=["basic", "advanced", "tactical", "phased"],
        default="basic",
        help="Evaluation profile used by the bot.",
    )
    parser.add_argument(
        "--stockfish",
        default="stockfish",
        help="Opponent UCI executable path (Stockfish, Rodent, Komodo, etc.).",
    )
    parser.add_argument(
        "--opponent-kind",
        choices=["auto", "stockfish", "rodent", "komodo"],
        default="auto",
        help="Opponent type hint used for diagnostics.",
    )
    parser.add_argument(
        "--levels",
        type=parse_levels,
        default=parse_levels("1320,1500,1700,1900"),
        help="Comma-separated UCI Elo levels. Example: 800,900,1000,1100",
    )
    parser.add_argument(
        "--games-per-level",
        type=int,
        default=8,
        help="Number of games per level. Colors alternate automatically.",
    )
    parser.add_argument(
        "--parallel-games",
        type=int,
        default=1,
        help="Number of games to run concurrently within each level.",
    )
    parser.add_argument(
        "--sf-movetime-ms",
        type=int,
        default=120,
        help="Stockfish think time per move in milliseconds.",
    )
    parser.add_argument("--sf-hash", type=int, default=64, help="Stockfish hash MB.")
    parser.add_argument("--sf-threads", type=int, default=1, help="Stockfish thread count.")
    parser.add_argument(
        "--max-plies",
        type=int,
        default=300,
        help="Adjudicate game as draw if this many plies are reached.",
    )
    parser.add_argument(
        "--output-json",
        default="",
        help="Optional output file for JSON report. If omitted, no report file is written.",
    )
    parser.add_argument(
        "--pgn-dir",
        default="logs/elo-pgn",
        help="Directory where one PGN file per played game is written. Use empty value to disable.",
    )
    parser.add_argument(
        "--stop-if-score-rate-below",
        type=float,
        default=0.40,
        help=(
            "Stop running higher levels when score rate on the current level is below this threshold. "
            "Use a negative value to disable."
        ),
        dest="stop_if_score_rate_below",
    )
    parser.add_argument(
        "--stop-if-winrate-below",
        type=float,
        help=argparse.SUPPRESS,
        dest="stop_if_score_rate_below",
    )

    args = parser.parse_args()

    if args.bot_depth < 1 or args.bot_depth > 8:
        parser.error("--bot-depth must be in [1, 8]")
    if args.games_per_level < 1:
        parser.error("--games-per-level must be >= 1")
    if args.parallel_games < 1:
        parser.error("--parallel-games must be >= 1")
    if args.sf_movetime_ms < 1:
        parser.error("--sf-movetime-ms must be >= 1")
    if args.max_plies < 20:
        parser.error("--max-plies must be >= 20")
    if args.stop_if_score_rate_below > 1.0:
        parser.error("--stop-if-score-rate-below must be <= 1.0")

    return args


def resolve_bot_binary(bot: str, explicit_binary: str) -> str:
    if explicit_binary:
        explicit_path = Path(explicit_binary)
        if not explicit_path.exists():
            raise FileNotFoundError(f"Bot binary not found: {explicit_binary}")
        return str(explicit_path.resolve())

    default_binary = "chess-bot" if bot == "minimax" else "chess-bot-iterdeep"
    if os.name == "nt":
        default_exe = Path(default_binary + ".exe")
        if default_exe.exists():
            return str(default_exe.resolve())

    default_path = Path(default_binary)
    if default_path.exists():
        return str(default_path.resolve())

    raise FileNotFoundError(
        f"Bot binary not found ({default_binary}). Build it first with 'make all' or pass --bot-binary."
    )


def resolve_stockfish_binary(path_hint: str) -> str:
    direct = Path(path_hint)
    if direct.exists():
        return str(direct)

    # Helpful on Windows-mounted engine files inside Linux containers.
    if not direct.suffix:
        with_exe = direct.with_suffix(".exe")
        if with_exe.exists():
            return str(with_exe)

    if direct.suffix.lower() == ".exe":
        without_exe = direct.with_suffix("")
        if without_exe.exists():
            return str(without_exe)

    resolved = shutil.which(path_hint)
    if not resolved:
        raise FileNotFoundError(
            f"Opponent executable not found: {path_hint}. Install/copy the engine binary and pass --stockfish <path>. "
            "For Docker mounts, place it under ./engines (for example ./engines/rodent.exe)."
        )
    return resolved


def _find_option_name(engine: chess.engine.SimpleEngine, candidates: list[str]) -> str | None:
    by_lower = {name.lower(): name for name in engine.options.keys()}
    for candidate in candidates:
        if candidate in engine.options:
            return candidate
        mapped = by_lower.get(candidate.lower())
        if mapped:
            return mapped
    return None


def configure_opponent_for_level(
    engine: chess.engine.SimpleEngine,
    level: StockfishLevel,
    opponent_kind: str,
) -> None:
    if level.mode == "elo":
        elo_opt = _find_option_name(engine, ["UCI_Elo", "Elo"])
        limit_opt = _find_option_name(engine, ["UCI_LimitStrength", "LimitStrength"])
        if not elo_opt:
            raise RuntimeError(
                f"Opponent engine ({opponent_kind}) does not expose an Elo option (UCI_Elo/Elo)."
            )

        cfg: dict[str, Any] = {elo_opt: level.value}
        if limit_opt:
            cfg[limit_opt] = True
        try:
            engine.configure(cfg)
        except chess.engine.EngineError as exc:
            # Some engines expose UCI_Elo but enforce a minimum (for example 800).
            # For lower requested Elo, try a skill-based fallback when available.
            elo_opt_obj = engine.options.get(elo_opt)
            elo_min = getattr(elo_opt_obj, "min", None) if elo_opt_obj else None
            if isinstance(elo_min, int) and level.value < elo_min:
                skill_opt = _find_option_name(engine, ["Skill Level", "Skill", "UCI_SkillLevel"])
                if skill_opt:
                    skill_opt_obj = engine.options.get(skill_opt)
                    skill_min = getattr(skill_opt_obj, "min", 0) if skill_opt_obj else 0
                    skill_max = getattr(skill_opt_obj, "max", 20) if skill_opt_obj else 20
                    # Map requested Elo proportionally into skill range.
                    ratio = max(0.0, min(1.0, level.value / float(max(elo_min, 1))))
                    mapped_skill = int(round(skill_min + ratio * (skill_max - skill_min)))

                    fallback_cfg: dict[str, Any] = {skill_opt: mapped_skill}
                    if limit_opt:
                        fallback_cfg[limit_opt] = False
                    try:
                        engine.configure(fallback_cfg)
                        print(
                            f"[warn] Opponent Elo {level.value} is below minimum {elo_min}; "
                            f"falling back to {skill_opt}={mapped_skill}."
                        )
                        return
                    except chess.engine.EngineError:
                        pass

                raise RuntimeError(
                    f"Opponent Elo {level.value} is below engine minimum {elo_min}. "
                    "Use a stronger engine or higher Elo levels."
                ) from exc

            raise RuntimeError(
                f"Failed to configure opponent Elo={level.value}. "
                "If this is Stockfish, values below about 1320 are unsupported. "
                "Use Rodent, Komodo, or another engine that supports lower Elo values."
            ) from exc
        return


def apply_bot_move(board: chess.Board, bot_response: dict[str, Any]) -> bool:
    move_uci = bot_response.get("engine_move", "")
    if not move_uci:
        return False

    move = chess.Move.from_uci(move_uci)
    if move not in board.legal_moves:
        raise RuntimeError(f"Bot produced illegal move for board state: {move_uci}")

    board.push(move)
    return True


def run_one_game(
    bot_binary: str,
    bot_depth: int,
    bot_eval: str,
    stockfish: chess.engine.SimpleEngine,
    level: StockfishLevel,
    level_order: int,
    game_index: int,
    sf_movetime_ms: int,
    max_plies: int,
    bot_name: str,
    opponent_name: str,
) -> GameResult:
    board = chess.Board()
    bot_color = chess.WHITE if (game_index % 2 == 0) else chess.BLACK
    bot_client = BotProtocolClient(bot_binary, bot_depth, bot_eval)
    plies = 0

    try:
        bot_client.reset()

        if bot_color == chess.WHITE:
            bot_reply = bot_client.go()
            if apply_bot_move(board, bot_reply):
                plies += 1

        while not board.is_game_over(claim_draw=True) and plies < max_plies:
            if board.turn == bot_color:
                bot_reply = bot_client.go()
                moved = apply_bot_move(board, bot_reply)
                if moved:
                    plies += 1
                else:
                    break
                continue

            limit = chess.engine.Limit(time=sf_movetime_ms / 1000.0)
            if level.mode == "depth":
                limit = chess.engine.Limit(depth=level.value)
            elif level.mode == "movetime":
                limit = chess.engine.Limit(time=level.value / 1000.0)
            sf_result = stockfish.play(board, limit)
            if sf_result.move is None:
                break

            sf_move = sf_result.move
            board.push(sf_move)
            plies += 1

            bot_reply = bot_client.move(sf_move.uci())
            if apply_bot_move(board, bot_reply):
                plies += 1

        if board.is_game_over(claim_draw=True):
            outcome = board.outcome(claim_draw=True)
            if outcome is None or outcome.winner is None:
                result = "1/2-1/2"
                score = 0.5
            elif outcome.winner == bot_color:
                result = "1-0" if bot_color == chess.WHITE else "0-1"
                score = 1.0
            else:
                result = "0-1" if bot_color == chess.WHITE else "1-0"
                score = 0.0
        else:
            result = "1/2-1/2"
            score = 0.5

        pgn_game = chess.pgn.Game()
        pgn_game.headers["Event"] = "Elo Evaluation"
        pgn_game.headers["Site"] = "Local"
        pgn_game.headers["Date"] = datetime.now(timezone.utc).strftime("%Y.%m.%d")
        pgn_game.headers["Round"] = str(game_index + 1)
        if bot_color == chess.WHITE:
            pgn_game.headers["White"] = bot_name
            pgn_game.headers["Black"] = opponent_name
        else:
            pgn_game.headers["White"] = opponent_name
            pgn_game.headers["Black"] = bot_name
        pgn_game.headers["Result"] = result
        pgn_game.headers["Level"] = level.label

        node = pgn_game
        for move in board.move_stack:
            node = node.add_variation(move)

        exporter = chess.pgn.StringExporter(headers=True, variations=False, comments=False)
        pgn_text = pgn_game.accept(exporter) + "\n"

        return GameResult(
            level_label=level.label,
            level_order=level_order,
            opponent_elo=level.opponent_elo,
            game_index=game_index + 1,
            bot_color="white" if bot_color == chess.WHITE else "black",
            result=result,
            score=score,
            plies=plies,
            pgn_text=pgn_text,
            pgn_file="",
        )
    finally:
        bot_client.close()


def run_one_game_worker(task: dict[str, Any]) -> GameResult:
    level = StockfishLevel(**task["level"])

    with chess.engine.SimpleEngine.popen_uci(task["stockfish_binary"]) as sf:
        sf.configure({
            "Threads": task["sf_threads"],
            "Hash": task["sf_hash"],
        })
        configure_opponent_for_level(sf, level, task["opponent_kind"])
        return run_one_game(
            bot_binary=task["bot_binary"],
            bot_depth=task["bot_depth"],
            bot_eval=task["bot_eval"],
            stockfish=sf,
            level=level,
            level_order=task["level_order"],
            game_index=task["game_index"],
            sf_movetime_ms=task["sf_movetime_ms"],
            max_plies=task["max_plies"],
            bot_name=task["bot_name"],
            opponent_name=task["opponent_name"],
        )


def sanitize_filename(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value)
    return cleaned.strip("_") or "level"


def write_game_pgn(pgn_dir: Path, game: GameResult) -> str:
    result_token = game.result.replace("/", "-")
    level_token = sanitize_filename(game.level_label)
    
    # Create subdirectory for this level
    level_dir = pgn_dir / level_token
    level_dir.mkdir(parents=True, exist_ok=True)
    
    # Simplified filename within level directory
    filename = f"g{game.game_index:03d}_{game.bot_color}_{result_token}.pgn"
    path = level_dir / filename
    path.write_text(game.pgn_text, encoding="utf-8")
    return str(path)


def print_report(stats: list[dict[str, Any]], overall_elo: float | None, total_games: int) -> None:
    print("\n=== Elo Evaluation Report ===")
    print("level        games   W   D   L   score   perf_elo")
    for row in stats:
        perf = row["performance_elo"]
        perf_text = "n/a" if perf is None else f"{perf:.1f}"
        print(
            f"{row['label']:<12} {row['games']:>5} {row['wins']:>3} {row['draws']:>3} {row['losses']:>3}"
            f" {row['score_rate']:.3f}   {perf_text}"
        )

    if overall_elo is None:
        print("\nOverall estimated Elo: n/a")
    else:
        print(f"\nOverall estimated Elo (MLE): {overall_elo:.1f}")

    print(f"Total games: {total_games}")


def main() -> int:
    args = parse_args()

    bot_binary = resolve_bot_binary(args.bot, args.bot_binary)
    stockfish_binary = resolve_stockfish_binary(args.stockfish)

    print(f"Using bot binary: {bot_binary}")
    print(f"Using bot eval profile: {args.bot_eval}")
    print(f"Using opponent engine: {stockfish_binary}")
    print(f"Opponent kind hint: {args.opponent_kind}")
    print(f"Parallel games per level: {args.parallel_games}")

    pgn_dir_path: Path | None = None
    if args.pgn_dir:
        pgn_dir_path = Path(args.pgn_dir)
        pgn_dir_path.mkdir(parents=True, exist_ok=True)
        print(f"PGN output directory: {pgn_dir_path}")

    report_games: list[GameResult] = []

    with chess.engine.SimpleEngine.popen_uci(stockfish_binary) as sf:
        sf.configure({
            "Threads": args.sf_threads,
            "Hash": args.sf_hash,
        })

        for level_index, level in enumerate(args.levels):
            print(f"\n[Level {level.label}] Running {args.games_per_level} games...")
            level_games: list[GameResult] = []
            bot_name = f"{args.bot}-{args.bot_eval}-d{args.bot_depth}"
            opponent_name = Path(stockfish_binary).name

            if args.parallel_games <= 1:
                configure_opponent_for_level(sf, level, args.opponent_kind)
                for gi in range(args.games_per_level):
                    game_result = run_one_game(
                        bot_binary=bot_binary,
                        bot_depth=args.bot_depth,
                        bot_eval=args.bot_eval,
                        stockfish=sf,
                        level=level,
                        level_order=level_index + 1,
                        game_index=gi,
                        sf_movetime_ms=args.sf_movetime_ms,
                        max_plies=args.max_plies,
                        bot_name=bot_name,
                        opponent_name=opponent_name,
                    )
                    report_games.append(game_result)
                    level_games.append(game_result)
                    print(
                        f"  game {gi + 1}/{args.games_per_level}: color={game_result.bot_color}"
                        f" result={game_result.result} score={game_result.score:.1f} plies={game_result.plies}"
                    )
            else:
                tasks: list[dict[str, Any]] = []
                for gi in range(args.games_per_level):
                    tasks.append({
                        "bot_binary": bot_binary,
                        "bot_depth": args.bot_depth,
                        "bot_eval": args.bot_eval,
                        "stockfish_binary": stockfish_binary,
                        "opponent_kind": args.opponent_kind,
                        "sf_threads": args.sf_threads,
                        "sf_hash": args.sf_hash,
                        "level": {
                            "label": level.label,
                            "opponent_elo": level.opponent_elo,
                            "mode": level.mode,
                            "value": level.value,
                        },
                        "level_order": level_index + 1,
                        "game_index": gi,
                        "sf_movetime_ms": args.sf_movetime_ms,
                        "max_plies": args.max_plies,
                        "bot_name": bot_name,
                        "opponent_name": opponent_name,
                    })

                with concurrent.futures.ProcessPoolExecutor(max_workers=args.parallel_games) as pool:
                    futures = [pool.submit(run_one_game_worker, task) for task in tasks]
                    for future in concurrent.futures.as_completed(futures):
                        game_result = future.result()
                        report_games.append(game_result)
                        level_games.append(game_result)

                level_games.sort(key=lambda g: g.game_index)
                for game_result in level_games:
                    if pgn_dir_path is not None:
                        game_result.pgn_file = write_game_pgn(pgn_dir_path, game_result)
                    print(
                        f"  game {game_result.game_index}/{args.games_per_level}: color={game_result.bot_color}"
                        f" result={game_result.result} score={game_result.score:.1f} plies={game_result.plies}"
                    )
            if args.parallel_games <= 1 and pgn_dir_path is not None:
                for game_result in level_games:
                    game_result.pgn_file = write_game_pgn(pgn_dir_path, game_result)

            if level_games:
                wins = sum(1 for g in level_games if g.score >= 0.999)
                win_rate = wins / len(level_games)
                score_rate = sum(g.score for g in level_games) / len(level_games)
                print(f"  level win rate: {win_rate:.3f} ({wins}/{len(level_games)})")
                print(f"  level score rate: {score_rate:.3f}")

                if (
                    args.stop_if_score_rate_below >= 0.0
                    and score_rate < args.stop_if_score_rate_below
                    and level_index < (len(args.levels) - 1)
                ):
                    print(
                        "  early stop: score rate below threshold "
                        f"({score_rate:.3f} < {args.stop_if_score_rate_below:.3f}); "
                        "skipping higher levels."
                    )
                    break

    grouped: dict[int, dict[str, Any]] = {}
    for game in report_games:
        row = grouped.setdefault(
            game.level_label,
            {
                "label": game.level_label,
                "opponent_elo": game.opponent_elo,
                "games": 0,
                "wins": 0,
                "draws": 0,
                "losses": 0,
                "score_sum": 0.0,
            },
        )
        row["games"] += 1
        row["score_sum"] += game.score
        if game.score >= 0.999:
            row["wins"] += 1
        elif game.score <= 0.001:
            row["losses"] += 1
        else:
            row["draws"] += 1

    stats: list[dict[str, Any]] = []
    for level_key in sorted(grouped):
        row = grouped[level_key]
        games = row["games"]
        score_rate = row["score_sum"] / games if games else 0.0
        perf = performance_from_score(score_rate, row["opponent_elo"]) if games else None
        row["score_rate"] = score_rate
        row["performance_elo"] = perf
        stats.append(row)

    overall_elo = solve_mle_elo(stats)
    total_games = len(report_games)

    print_report(stats, overall_elo, total_games)

    if args.output_json:
        payload = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "bot": args.bot,
            "bot_binary": bot_binary,
            "bot_depth": args.bot_depth,
            "bot_eval": args.bot_eval,
            "stockfish": stockfish_binary,
            "games_per_level": args.games_per_level,
            "parallel_games": args.parallel_games,
            "stop_if_score_rate_below": args.stop_if_score_rate_below,
            "pgn_dir": args.pgn_dir,
            "stockfish_levels": [lv.label for lv in args.levels],
            "stats": stats,
            "overall_elo_mle": overall_elo,
            "games": [
                {
                    "level": g.level_label,
                    "opponent_elo": g.opponent_elo,
                    "game_index": g.game_index,
                    "bot_color": g.bot_color,
                    "result": g.result,
                    "score": g.score,
                    "plies": g.plies,
                    "pgn_file": g.pgn_file,
                }
                for g in report_games
            ],
        }
        output_path = Path(args.output_json)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"\nSaved JSON report to: {output_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
