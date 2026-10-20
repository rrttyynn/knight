#!/usr/bin/env python3
"""Reproducible paired single-core UCI match. Requires python-chess."""
import argparse
import json
import math
import os
import random
from pathlib import Path
import time
import chess
import chess.engine
import chess.pgn

LINES = [
    'e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6',
    'e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6',
    'd4 d5 c4 e6 Nc3 Nf6 Bg5 Be7',
    'd4 Nf6 c4 g6 Nc3 Bg7 e4 d6',
    'e4 e6 d4 d5 Nc3 Nf6 e5 Nfd7',
    'e4 c6 d4 d5 Nc3 dxe4 Nxe4 Bf5',
    'd4 d5 c4 c6 Nf3 Nf6 Nc3 e6',
    'c4 e5 Nc3 Nf6 g3 d5 cxd5 Nxd5',
    'Nf3 d5 g3 Nf6 Bg2 g6 O-O Bg7',
    'e4 d5 exd5 Nf6 d4 Nxd5 Nf3 g6',
    'd4 Nf6 c4 e6 Nf3 b6 g3 Bb7',
    'e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6',
    'd4 Nf6 c4 e6 Nc3 Bb4 e3 O-O',
    'e4 e5 Nf3 Nf6 Nxe5 d6 Nf3 Nxe4',
    'd4 f5 g3 Nf6 Bg2 g6 Nf3 Bg7',
    'e4 g6 d4 Bg7 Nc3 d6 f4 Nf6',
    'd4 d5 c4 dxc4 Nf3 Nf6 e3 e6',
    'e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 g6',
    'e4 e5 Nc3 Nf6 f4 d5 fxe5 Nxe4',
    'c4 c5 Nc3 Nc6 g3 g6 Bg2 Bg7',
]

def openings(pairs, seed):
    rng = random.Random(seed)
    for i in range(pairs):
        b = chess.Board()
        for san in LINES[i % len(LINES)].split():
            b.push_san(san)
        # Seeded variations prevent repeats after the first pass through the book.
        for _ in range(2 * (i // len(LINES))):
            if b.is_game_over():
                break
            b.push(rng.choice(list(b.legal_moves)))
        yield b


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('candidate')
    p.add_argument('baseline')
    p.add_argument('--pairs', type=int, default=50)
    p.add_argument('--nodes', type=int)
    p.add_argument('--movetime', type=float, default=0.05)
    p.add_argument('--seed', type=int, default=20261020)
    p.add_argument('--start-pair', type=int, default=0)
    p.add_argument('--out', default='tests/results/match')
    args = p.parse_args()
    if hasattr(os, 'sched_getaffinity'):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    engines = [chess.engine.SimpleEngine.popen_uci(str(Path(x).resolve()))
               for x in (args.candidate, args.baseline)]
    for engine in engines:
        engine.configure({'Hash': 8, 'Threads': 1})
    limit = chess.engine.Limit(nodes=args.nodes) if args.nodes else chess.engine.Limit(time=args.movetime)
    scores = []
    pairs = []
    started = time.monotonic()
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    pgn = open(args.out + '.pgn', 'w')
    try:
        for pair, opening in enumerate(openings(args.start_pair + args.pairs, args.seed)):
            if pair < args.start_pair:
                continue
            pair_score = 0
            for color in (chess.WHITE, chess.BLACK):
                board = opening.copy()
                game = chess.pgn.Game()
                game.setup(board)
                game.headers.update(Event='K8E single-core regression', Round=str(pair + 1),
                                    White='candidate' if color else 'baseline',
                                    Black='baseline' if color else 'candidate')
                node = game
                token = object()
                while not board.is_game_over(claim_draw=True) and board.ply() < 600:
                    index = 0 if board.turn == color else 1
                    result = engines[index].play(board, limit, game=token)
                    if result.move not in board.legal_moves:
                        raise RuntimeError(f'Illegal move: engine={index}, {result.move}, {board.fen()}')
                    board.push(result.move)
                    node = node.add_variation(result.move)
                outcome = board.outcome(claim_draw=True)
                result = board.result(claim_draw=True) if outcome else '1/2-1/2'
                game.headers['Result'] = result
                game.headers['Termination'] = outcome.termination.name if outcome else '600-ply limit'
                score = 0.5 if not outcome or outcome.winner is None else float(outcome.winner == color)
                scores.append(score)
                pair_score += score
                print(game, file=pgn, end='\n\n', flush=True)
            pairs.append(pair_score)
            n = len(scores)
            w, d, l = scores.count(1.0), scores.count(0.5), scores.count(0.0)
            mean = sum(scores) / n
            # Color-paired cluster interval accounts for correlated reverse-color games.
            rates = [x / 2 for x in pairs]
            se = (sum((x - mean)**2 for x in rates) / (len(rates) - 1) / len(rates))**0.5 if len(rates) > 1 else 0.5
            stats = dict(games=n, wins=w, draws=d, losses=l, score=mean,
                         score_ci95=[max(0, mean - 1.96*se), min(1, mean + 1.96*se)],
                         elo=400*math.log10(mean/(1-mean)) if 0 < mean < 1 else None,
                         pair_scores=pairs, seed=args.seed, start_pair=args.start_pair, nodes=args.nodes,
                         movetime=None if args.nodes else args.movetime,
                         seconds=round(time.monotonic()-started, 1), threads=1, hash_mb=8)
            Path(args.out + '.json').write_text(json.dumps(stats, indent=2) + '\n')
            print(json.dumps({k: v for k, v in stats.items() if k != 'pair_scores'}), flush=True)
    finally:
        pgn.close()
        for engine in engines:
            engine.quit()

if __name__ == '__main__':
    main()
