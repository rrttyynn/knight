#!/usr/bin/env python3
"""Perft differential oracle and UCI interruption/terminal-position checks."""
import random
import subprocess
import sys
from pathlib import Path
import chess
import chess.engine

exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else './k8e').resolve())

def perft(board, depth):
    if not depth:
        return 1
    total = 0
    for move in list(board.legal_moves):
        board.push(move)
        total += perft(board, depth - 1)
        board.pop()
    return total

positions = [
    (chess.STARTING_FEN, 5, 4865609),
    ('r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1', 4, 4085603),
    ('8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1', 4, 43238),
    ('r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1', 3, 9467),
    ('rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8', 3, 62379),
    ('4k3/8/8/r4pPK/8/8/8/8 w - f6 0 1', 3, None),
    ('4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1', 3, None),
]
rng = random.Random(20261020)
board = chess.Board()
for i in range(64):
    if board.is_game_over():
        board.reset()
    board.push(rng.choice(list(board.legal_moves)))
    positions.append((board.fen(en_passant='fen'), 2, None))
for fen, depth, expected in positions:
    if expected is None:
        expected = perft(chess.Board(fen), depth)
    actual = int(subprocess.check_output([exe, 'perft', str(depth), fen], text=True).split()[0])
    assert actual == expected, (fen, depth, actual, expected)
print(f'perft: {len(positions)} reference and random positions passed')

with chess.engine.SimpleEngine.popen_uci(exe) as engine:
    assert engine.options['Threads'].min == engine.options['Threads'].max == 1
    for fen in [chess.STARTING_FEN, positions[1][0], positions[4][0]]:
        b = chess.Board(fen)
        for nodes in [1, 256, 1000, 10000]:
            # Exercise both early fallback and interruptions inside deeper iterations.
            with engine.analysis(b, chess.engine.Limit(nodes=nodes), game=object(), info=chess.engine.INFO_ALL) as analysis:
                infos = list(analysis)
                result = analysis.wait()
            assert result.move in b.legal_moves
    for fen in ['7k/6Q1/6K1/8/8/8/8/8 b - - 0 1', '7k/5Q2/6K1/8/8/8/8/8 b - - 0 1']:
        assert not engine.play(chess.Board(fen), chess.engine.Limit(nodes=1000)).move
    b = chess.Board('7k/5K2/6Q1/8/8/8/8/8 w - - 0 1')
    result = engine.play(b, chess.engine.Limit(depth=5), info=chess.engine.INFO_ALL)
    b.push(result.move)
    assert b.is_checkmate(), result
    with engine.analysis(chess.Board()) as analysis:
        analysis.stop()
        assert analysis.wait().move in chess.Board().legal_moves
print('UCI: single worker, legal interrupted-search fallback, mate, stalemate and stop passed')

if len(sys.argv) > 2:
    other = str(Path(sys.argv[2]).resolve())
    with chess.engine.SimpleEngine.popen_uci(exe) as a, chess.engine.SimpleEngine.popen_uci(other) as b:
        for fen, _, _ in positions[:12]:
            results = [e.play(chess.Board(fen), chess.engine.Limit(nodes=20000), game=object(), info=chess.engine.INFO_ALL) for e in (a, b)]
            assert results[0].move == results[1].move, results
            assert results[0].info['score'] == results[1].info['score'], results
    print('engine/reference: deterministic fixed-node search results agree')
