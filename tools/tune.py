#!/usr/bin/env python3
"""Offline Texel evaluation fitting and paired-game SPSA search tuning."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys

from tuning_model import Model, ROOT, SEARCH, write_json

sys.path.insert(0, str(ROOT / "tests"))
from match import openings, play_game
import chess
import chess.engine
import chess.pgn


def positive(value):
    result = int(value)
    if result < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def pin_cpu():
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})


def collect(args):
    rng = random.Random(args.seed)
    rows = []
    games = 0
    for path in args.pgn:
        with Path(path).open() as stream:
            while (game := chess.pgn.read_game(stream)) is not None:
                if game.errors:
                    raise ValueError(f"Invalid PGN in {path}: {game.errors}")
                result = {"1-0": 1.0, "1/2-1/2": .5, "0-1": 0.0}.get(game.headers["Result"])
                if result is None:
                    continue
                # Both colors and all repeats of an opening belong to one split.
                # Ordinary PGNs start at move one; use their first eight plies.
                opening_board = game.board()
                for move in game.mainline_moves():
                    if opening_board.ply() >= 8:
                        break
                    opening_board.push(move)
                identity = hashlib.sha256(opening_board.fen().encode()).hexdigest()[:20]
                board = game.board()
                for ply, move in enumerate(game.mainline_moves(), 1):
                    board.push(move)
                    if (ply >= 16 and ply % args.stride == 0 and not board.is_check()
                            and not board.is_game_over(claim_draw=True)):
                        rows.append((board.fen(), result, identity))
                games += 1
    # Deduplicate complete observations; group identity is retained on every row.
    rows = list(dict.fromkeys(rows))
    if args.max_positions and len(rows) > args.max_positions:
        rows = rng.sample(rows, args.max_positions)
    if not rows:
        raise ValueError("No usable labeled positions")
    path = Path(args.out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{fen}\t{result}\t{group}\n" for fen, result, group in rows))
    print(json.dumps(dict(games_read=games, positions=len(rows), out=str(path))))


def load_data(path):
    rows = []
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            fen, label, group = line.split("\t")
            label = float(label)
            board = chess.Board(fen)
            if label not in (0.0, .5, 1.0) or not group or not board.is_valid() or board.is_check():
                raise ValueError("invalid label, group or position")
        except ValueError as error:
            raise ValueError(f"{path}:{number}: {error}") from error
        rows.append((fen, label, group))
    if not rows:
        raise ValueError("Empty training data")
    return rows


def game_split(groups, seed):
    names = sorted(set(groups))
    if len(names) < 6:
        raise ValueError("At least six distinct opening groups are needed for a separated holdout")
    random.Random(seed).shuffle(names)
    held_out = set(names[:max(1, len(names) // 5)])
    return [name not in held_out for name in groups]


def scores(executable, rows, env=None, features=False):
    import numpy as np
    result = subprocess.run([str(executable), "--features" if features else "--scores"],
                            input="".join(row[0] + "\n" for row in rows), text=True,
                            capture_output=True, env=env, check=True)
    values = np.fromstring(result.stdout, sep=" ")
    columns = 2 + 2 * len(Model().weights) if features else 1
    if values.size != len(rows) * columns:
        raise ValueError(f"Evaluator returned {values.size} values for {len(rows)} positions")
    return values.reshape(len(rows), columns)


def texel(args):
    import numpy as np
    from scipy.optimize import minimize, minimize_scalar
    from scipy.special import expit
    pin_cpu()
    model = Model()
    executable = model.build()
    rows = load_data(args.data)
    values = scores(executable, rows, features=True)
    # Fit only quiet positions; the actual engine qsearch is the volatility oracle.
    keep = np.abs(values[:, 0] - values[:, 1]) <= args.max_quiet_gap
    rows = [row for row, include in zip(rows, keep) if include]
    values = values[keep]
    train = np.array(game_split([row[2] for row in rows], args.seed), dtype=bool)
    y = np.array([row[1] for row in rows])
    base, features = values[:, 0], values[:, 2:]
    weights = np.array(list(model.weights.values()), dtype=float).reshape(-1)
    selected = args.eval_params.split(",")
    if selected != ["all"] and any(not any(name.startswith(prefix) for name in model.weights)
                                   for prefix in selected):
        raise ValueError("Unknown evaluation parameter prefix")
    scales, bounds = [], []
    for name, pair in model.weights.items():
        for phase, value in enumerate(pair):
            radius = 8 if name.startswith("DATA_") else max(4, abs(value) * .15)
            lo, hi = value - radius, value + radius
            if name.startswith("DATA_"):
                lo, hi = max(0, lo), min(94, hi)
            if name == "TEMPO" and phase == 1:
                lo = hi = 0
            if selected != ["all"] and not any(name.startswith(prefix) for prefix in selected):
                lo = hi = value
            scales.append(radius)
            bounds.append((lo - value, hi - value))
    scales = np.array(scales)
    active = np.array([lo != hi for lo, hi in bounds])
    if not active.any():
        raise ValueError("No evaluation parameters selected")
    # Equal total weight per game avoids allowing long games to dominate the fit.
    groups = [row[2] for row in rows]
    counts = {name: groups.count(name) for name in set(groups)}
    importance = np.array([1 / counts[name] for name in groups])
    def mse(prediction, mask):
        return float(np.average((prediction[mask] - y[mask])**2, weights=importance[mask]))
    k = minimize_scalar(lambda value: mse(expit(value * base), train),
                        bounds=(math.log(10) / 1000, math.log(10) / 50), method="bounded").x
    x = features[train] * scales
    w = importance[train] / importance[train].sum()
    def objective(change):
        p = expit(k * (base[train] + x @ change))
        error = p - y[train]
        penalty = args.regularization * np.mean(change[active]**2)
        gradient = 2 * k * (x.T @ (w * error * p * (1 - p)))
        gradient[active] += 2 * args.regularization * change[active] / active.sum()
        return float(w @ error**2 + penalty), gradient
    normalized_bounds = [(lo / scale, hi / scale) for (lo, hi), scale in zip(bounds, scales)]
    result = minimize(objective, np.zeros_like(weights), jac=True, bounds=normalized_bounds,
                      method="L-BFGS-B", options=dict(maxiter=args.iterations, ftol=1e-12, gtol=1e-9))
    rounded = np.rint(weights + result.x * scales).astype(int).reshape(-1, 2)
    profile = model.profile("texel")
    profile["evaluation"] = {name: list(map(int, pair)) for name, pair in zip(model.weights, rounded)}
    env = model.environment(profile, Path(args.out).with_suffix(".weights"))
    exact = scores(executable, rows, env=env)[:, 0]
    before, after = expit(k * base), expit(k * exact)
    profile["fit"] = dict(data_sha256=hashlib.sha256(Path(args.data).read_bytes()).hexdigest(),
                          seed=args.seed, positions=len(rows), quiet_positions_rejected=int((~keep).sum()),
                          training_groups=len(set(g for g, t in zip(groups, train) if t)),
                          validation_groups=len(set(g for g, t in zip(groups, train) if not t)),
                          train_mse_before=mse(before, train), train_mse_after=mse(after, train),
                          validation_mse_before=mse(before, ~train), validation_mse_after=mse(after, ~train),
                          sigmoid_k=float(k), regularization=args.regularization,
                          max_quiet_gap=args.max_quiet_gap,
                          parameters=selected,
                          optimizer_iterations=int(result.nit), optimizer_success=bool(result.success),
                          changed_components=int((rounded.reshape(-1) != weights).sum()))
    profile["fit"]["holdout_improved"] = profile["fit"]["validation_mse_after"] < profile["fit"]["validation_mse_before"]
    write_json(args.out, profile)
    print(json.dumps(profile["fit"], indent=2))


def spsa(args):
    pin_cpu()
    model = Model()
    executable = model.build()
    rng = random.Random(args.seed)
    names = args.params.split(",") if args.params else list(SEARCH)
    if len(set(names)) != len(names) or any(name not in SEARCH for name in names):
        raise ValueError("Unknown or duplicate search parameter")
    theta = {name: float(model.search[name]) for name in names}
    average = dict.fromkeys(names, 0.0)
    averaged = 0
    opening = iter(openings(args.iterations * args.pairs, args.seed))
    limit = chess.engine.Limit(time=args.movetime) if args.movetime else chess.engine.Limit(nodes=args.nodes)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    history = out.with_suffix(".history.jsonl")
    start = 0
    if history.exists():
        if not args.resume:
            raise ValueError(f"Refusing to overwrite SPSA history without --resume: {history}")
        previous = json.loads(out.read_text())
        model.validate(previous)
        settings = dict(seed=args.seed, pairs_per_iteration=args.pairs,
                        requested_iterations=args.iterations,
                        nodes=None if args.movetime else args.nodes,
                        movetime=args.movetime, parameters=names)
        if any(previous["fit"].get(key) != value for key, value in settings.items()):
            raise ValueError("Resume settings differ from the checkpoint")
        records = [json.loads(line) for line in history.read_text().splitlines()]
        if len(records) != previous["fit"]["iterations"]:
            raise ValueError("Incomplete history/checkpoint transaction; inspect the last iteration")
        start = len(records)
        theta = records[-1]["theta"]
        for i, record in enumerate(records):
            for _ in names:
                rng.choice((-1, 1))
            for _ in range(args.pairs):
                next(opening)
            if i >= args.iterations // 2:
                averaged += 1
                for name in names:
                    average[name] += record["theta"][name]
    elif args.resume:
        raise ValueError("No SPSA history to resume")
    stop = min(args.iterations, start + args.steps) if args.steps else args.iterations
    for iteration in range(start, stop):
        delta = {name: rng.choice((-1, 1)) for name in names}
        c = (iteration + 1)**(-.101)
        a = ((iteration + 1 + args.iterations / 10) / (1 + args.iterations / 10))**(-.602)
        profiles = [model.profile("spsa-perturbation") for _ in range(2)]
        for name in names:
            lo, hi, radius, _ = SEARCH[name]
            # Integer coordinates keep a useful perturbation after rounding.
            perturbation = max(.5, radius * c) if model.types[name] == "int" else radius * c
            for sign, profile in zip((1, -1), profiles):
                value = min(hi, max(lo, theta[name] + sign * perturbation * delta[name]))
                profile["search"][name] = int(round(value)) if model.types[name] == "int" else round(value, 5)
        engines = []
        try:
            for i, profile in enumerate(profiles):
                env = model.environment(profile, out.with_suffix(f".{i}.weights"))
                engine = chess.engine.SimpleEngine.popen_uci(str(executable), env=env)
                engines.append(engine)
                engine.configure({"Hash": 8, "Threads": 1})
            pair_scores = []
            for _ in range(args.pairs):
                board = next(opening)
                pair_scores.append(sum(play_game(engines, board, color, limit)[0]
                                       for color in (chess.WHITE, chess.BLACK)))
        finally:
            for engine in engines:
                engine.quit()
        reward = sum(pair_scores) / args.pairs - 1  # plus engine's centered score, [-1, 1]
        for name in names:
            lo, hi, _, gain = SEARCH[name]
            theta[name] = min(hi, max(lo, theta[name] + gain * a * reward * delta[name] / c))
        if iteration >= args.iterations // 2:
            averaged += 1
            for name in names:
                average[name] += theta[name]
        with history.open("a") as stream:
            stream.write(json.dumps(dict(iteration=iteration + 1, pair_scores=pair_scores,
                                         theta=theta, plus=profiles[0]["search"],
                                         minus=profiles[1]["search"])) + "\n")
        # Checkpoint after every iteration; a long run can be inspected safely.
        profile = model.profile("spsa")
        for name in names:
            value = average[name] / averaged if averaged else theta[name]
            profile["search"][name] = int(round(value)) if model.types[name] == "int" else round(value, 5)
        profile["fit"] = dict(seed=args.seed, iterations=iteration + 1,
                              requested_iterations=args.iterations,
                              complete=iteration + 1 == args.iterations, pairs_per_iteration=args.pairs,
                              games=(iteration + 1) * args.pairs * 2, nodes=None if args.movetime else args.nodes,
                              movetime=args.movetime, parameters=names, iterate_average_count=averaged,
                              note="Optimization trace, not an independent strength estimate")
        write_json(out, profile)
        if (iteration + 1) % 10 == 0 or iteration + 1 == stop:
            print(json.dumps(profile["fit"]), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("build", help="build the offline evaluator/tunable UCI engine")
    p.set_defaults(run=lambda _: print(Model().build()))
    p = sub.add_parser("collect", help="extract labeled, game-grouped TSV from PGNs")
    p.add_argument("pgn", nargs="+")
    p.add_argument("--out", required=True)
    p.add_argument("--stride", type=positive, default=4)
    p.add_argument("--max-positions", type=positive)
    p.add_argument("--seed", type=int, default=20261022)
    p.set_defaults(run=collect)
    p = sub.add_parser("texel", help="fit and exactly validate quantized evaluation weights")
    p.add_argument("data")
    p.add_argument("--out", required=True)
    p.add_argument("--iterations", type=positive, default=150)
    p.add_argument("--max-quiet-gap", type=positive, default=80)
    p.add_argument("--regularization", type=float, default=.02)
    p.add_argument("--eval-params", default="MATERIAL_,BISHOP_PAIR,PAWN_,ROOK_,KING_,MINOR_,BOARD_,TEMPO",
                   help="comma-separated weight name prefixes, or all for a large data set")
    p.add_argument("--seed", type=int, default=20261022)
    p.set_defaults(run=texel)
    p = sub.add_parser("spsa", help="tune search with paired games and compile-free perturbations")
    p.add_argument("--out", required=True)
    p.add_argument("--iterations", type=positive, default=200)
    p.add_argument("--steps", type=positive, help="stop after this many additional iterations")
    p.add_argument("--resume", action="store_true")
    p.add_argument("--pairs", type=positive, default=1)
    limit = p.add_mutually_exclusive_group()
    limit.add_argument("--nodes", type=positive, default=3000)
    limit.add_argument("--movetime", type=float)
    p.add_argument("--params", help="comma-separated subset of params.h names")
    p.add_argument("--seed", type=int, default=20261022)
    p.set_defaults(run=spsa)
    p = sub.add_parser("apply", help="explicitly apply a source-matched profile after review")
    p.add_argument("profile")
    p.set_defaults(run=lambda args: Model().apply(json.loads(Path(args.profile).read_text())))
    args = parser.parse_args()
    if (not math.isfinite(getattr(args, "regularization", 0)) or getattr(args, "regularization", 0) < 0
            or (getattr(args, "movetime", None) is not None
                and (not math.isfinite(args.movetime) or args.movetime <= 0))):
        parser.error("regularization must be nonnegative and movetime must be positive")
    args.run(args)


if __name__ == "__main__":
    main()
