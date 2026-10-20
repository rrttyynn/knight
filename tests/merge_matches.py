#!/usr/bin/env python3
"""Merge disjoint, color-paired match shards with the same settings."""
import argparse
import json
import math
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('prefixes', nargs='+')
p.add_argument('--out', required=True)
args = p.parse_args()
rows = [(json.loads(Path(x + '.json').read_text()), Path(x + '.pgn')) for x in args.prefixes]
rows.sort(key=lambda x: x[0]['start_pair'])
expected = 0
pairs = []
for row, _ in rows:
    assert row['start_pair'] == expected, 'Shards overlap or leave a gap'
    assert row['games'] == 2 * len(row['pair_scores'])
    for key in ['seed', 'nodes', 'movetime', 'threads', 'hash_mb']:
        assert row[key] == rows[0][0][key], f'Different settings: {key}'
    expected += len(row['pair_scores'])
    pairs.extend(row['pair_scores'])
n = 2 * len(pairs)
mean = sum(pairs) / n
se = (sum((x / 2 - mean)**2 for x in pairs) / (len(pairs) - 1) / len(pairs))**0.5
stats = {key: rows[0][0][key] for key in ['seed', 'nodes', 'movetime', 'threads', 'hash_mb']}
stats.update(games=n, wins=sum(x['wins'] for x, _ in rows),
             draws=sum(x['draws'] for x, _ in rows), losses=sum(x['losses'] for x, _ in rows),
             score=mean, score_ci95=[max(0, mean - 1.96*se), min(1, mean + 1.96*se)],
             elo=400*math.log10(mean/(1-mean)) if 0 < mean < 1 else None,
             pair_scores=pairs,
             shard_seconds=[x['seconds'] for x, _ in rows])
Path(args.out + '.json').write_text(json.dumps(stats, indent=2) + '\n')
Path(args.out + '.pgn').write_text(''.join(path.read_text() for _, path in rows))
print(json.dumps({k: v for k, v in stats.items() if k != 'pair_scores'}, indent=2))
