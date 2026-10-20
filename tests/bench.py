#!/usr/bin/env python3
"""Single-core fixed-node search benchmark across reproducible middlegames."""
import argparse
import json
import os
from pathlib import Path
import statistics
import time
import chess
import chess.engine
from match import openings

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('engines', nargs='+')
p.add_argument('--nodes', type=int, default=300000)
p.add_argument('--runs', type=int, default=3)
p.add_argument('--movetime', type=float)
args = p.parse_args()
if hasattr(os, 'sched_getaffinity'):
    os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
positions = list(openings(12, 20261020))
reports = []
for path in args.engines:
    results = []
    with chess.engine.SimpleEngine.popen_uci(str(Path(path).resolve())) as engine:
        engine.configure({'Hash': 8, 'Threads': 1})
        for run in range(args.runs):
            elapsed = nodes = 0
            depths = []
            for board in positions:
                started = time.monotonic()
                limit = chess.engine.Limit(time=args.movetime) if args.movetime else chess.engine.Limit(nodes=args.nodes)
                result = engine.play(board, limit, game=object(), info=chess.engine.INFO_ALL)
                elapsed += time.monotonic() - started
                assert result.move in board.legal_moves
                nodes += result.info['nodes']
                depths.append(result.info['depth'])
            results.append(dict(seconds=elapsed, completed_iteration_nodes=nodes,
                                budget_nps=None if args.movetime else len(positions)*args.nodes/elapsed,
                                mean_depth=statistics.mean(depths)))
    reports.append(dict(engine=path, positions=len(positions), node_budget=None if args.movetime else args.nodes,
                          movetime=args.movetime,
                          median_budget_nps=None if args.movetime else statistics.median(x['budget_nps'] for x in results),
                          runs=results))
print(json.dumps(reports, indent=2), flush=True)
