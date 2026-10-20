# K8E 2 single-core validation

Date: 2026-10-20. Baseline: `98359a3e3fd949cb0cb013dffbfb76c87bff0fd8` (the repository's main branch when this iteration began).

## Implementation

Precompute knight/king attacks and rank attacks. On builds with BMI2, generate occupancy-indexed bishop and rook tables at startup and use PEXT to select attacks; queens combine the two lookups. The BMI2 tables occupy about 2.25 MiB at runtime. Builds without BMI2 use conditional hyperbola/rank-table attacks. No NNUE weights, external books at runtime, or additional search workers are required.

The final search and evaluation code is unchanged from the baseline. Additional search changes were screened separately; the final candidate isolates the attack-generation improvement. Fixed-node searches against the baseline agree on moves and scores in the regression suite.

The compressed launcher is **7,878 bytes**, compared with the baseline's 8,098 bytes. Expanded lexical aliasing preserves the token-equivalence check in `pack.py`; packed/native fixed-node searches also agree. This size is for the compressed self-compiling launcher, not the cached ELF executable.

## Environment and throughput

Linux x86-64; Intel Xeon Platinum 8370C; GCC 13.3.0. Both engines use the same native build flags: `-O3 -std=c++20 -march=native -DNDEBUG -fno-rtti -pthread`. Hash: 8 MB. One search worker, with each runner and its engines pinned to one CPU. The BMI2 path is enabled on this machine.

The 12-position benchmark uses 300,000 nodes per position, resets game state before each position, and repeats three times. Reported throughput divides the total requested node budget by wall time, including protocol/reset overhead and the interrupted final iteration.

| Metric | Baseline | K8E 2 |
|---|---:|---:|
| Median node-budget throughput | 993,521 nodes/s | 1,461,740 nodes/s |
| Median mean reported depth, 150 ms requested/move | 13.33 | 13.75 |
| Mean reported depth at equal 300k-node budgets | 14.83 | 14.83 |

The throughput increase is **47.1%** on this host. Iterative depth includes the engine's existing aspiration/reduction behavior. Wall-time measurements vary with scheduling and hardware. Raw measurements: [bench.json](bench.json), [depth.json](depth.json).

## Head-to-head match

K8E 2: **90 wins, 52 draws, 58 losses**; score **58.0%**. Approximate paired 95% score interval: **53.5%–62.5%**. Estimated head-to-head difference: **+56.1 Elo** (score-interval transform: +24.3 to +88.8 Elo).

Settings: 100 opening pairs / 200 games, reverse colors for every pair; seed `20261023`; `go movetime 40`, with the same existing internal 10 ms reserve in both versions; 8 MB Hash; Threads=1. Twenty standard opening lines are diversified by seeded legal continuations. Draws use `python-chess` rule claims; games reaching 600 plies are drawn. No score-based win adjudication is used. Every returned move is checked against independent legal moves.

The 100 disjoint pairs were split into three concurrent runners on separate cores. Each individual engine remains pinned to one core and has only one search worker. Confidence intervals group the two reverse-color games into one observation and use a normal approximation. Results establish performance under these short controls on this CPU; longer controls and other CPUs have not been validated. The Elo difference is a head-to-head estimate, not an absolute or CCRL rating.

Raw results: [match.json](match.json), [match.pgn](match.pgn).

## Correctness checks

- 50,000 sliding/leaper comparisons against independent directional rays, on BMI2 and generic paths.
- 3,000 random-position checks for evaluation/SEE immutability and incremental position/correction hashes.
- 71 standard and random perft positions, including castling, promotions and pinned en-passant.
- UCI early interruption, legal fallback, stop, checkmate and stalemate.
- Generic/native and packed/native fixed-node move/score equivalence.
- AddressSanitizer and UndefinedBehaviorSanitizer on the core suite; leak checking disabled for runner compatibility.
- `git diff --check`.

## Reproduction

Compile the baseline in a separate checkout of the commit above, retaining its original `Makefile`. Then, from this checkout:

```sh
python3 -m pip install -r tests/requirements.txt
make test
python3 tests/bench.py /path/to/baseline/k8e ./k8e
python3 tests/bench.py /path/to/baseline/k8e ./k8e --movetime 0.15
python3 tests/match.py ./k8e /path/to/baseline/k8e --pairs 100 --seed 20261023 --movetime 0.04 --out match
```

For sharding, use disjoint `--start-pair`/`--pairs` ranges `0/34`, `34/33`, `67/33`, with the same seed and limits. Pin each runner to a separate CPU with `taskset`. Merge using `python3 tests/merge_matches.py part-a part-b part-c --out match`.

Native candidate SHA-256: `fee2438ec1f499ab8edf5529e6a6d2f3f0899e2863bb48f8158d6b00e3fab649`.

Native baseline SHA-256: `dd3cc78a26449974b67be9547c8fbbd4d87b32a85b9c5d973e658bcab2acb93b`.

Packed launcher SHA-256: `4908040b52626737b6eafd913109278fc1a9bf5f59ae5bc0e7a3a7044c9251da`.
