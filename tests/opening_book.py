#!/usr/bin/env python3
"""Generate seeded, balanced openings using only the supplied reference engine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import chess
import chess.engine
from match import LINES

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('reference')
p.add_argument('--positions', type=int, default=700)
p.add_argument('--seed', type=int, default=2026102310)
p.add_argument('--out', default='tests/results/openings.fen')
args = p.parse_args()
if hasattr(os, 'sched_getaffinity'):
    os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
rng = random.Random(args.seed)
positions, seen, attempts = [], set(), 0
with chess.engine.SimpleEngine.popen_uci(str(Path(args.reference).resolve())) as engine:
    engine.configure({'Hash': 8, 'Threads': 1})
    while len(positions) < args.positions:
        attempts += 1
        board = chess.Board()
        for san in LINES[(attempts - 1) % len(LINES)].split():
            board.push_san(san)
        for _ in range(rng.randrange(2, 9)):
            if board.is_game_over():
                break
            board.push(rng.choice(list(board.legal_moves)))
        key = board.epd()
        if key in seen or board.is_game_over() or board.is_check():
            continue
        result = engine.play(board, chess.engine.Limit(nodes=5000),
                             game=object(), info=chess.engine.INFO_ALL)
        if abs(result.info['score'].white().score(mate_score=100000)) > 100:
            continue
        seen.add(key)
        positions.append(board.fen())
out = Path(args.out)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(positions) + '\n')
print(json.dumps(dict(positions=len(positions), attempts=attempts, seed=args.seed,
                      sha256=hashlib.sha256(out.read_bytes()).hexdigest())))
