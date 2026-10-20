# KNIGHT (K8E 2)

KNIGHT is a compact UCI chess engine with **one search worker** and a compressed release launcher strictly smaller than **8,192 bytes**. This iteration focuses on single-core move-generation throughput and search behavior. Strength is measured against the repository baseline; no absolute CCRL rating is claimed.

## Build and run

Requirements: Linux, `g++` with C++20, `python3`, and `xz-utils`.

```sh
make
./k8e              # Native engine, ready to use in a UCI GUI
./k8e_release      # <8KB launcher, compiles and caches the native engine on first run
./k8e perft 6
```

The release launcher needs the compiler on its first run and caches the result under `${XDG_CACHE_HOME:-$HOME/.cache}/k8e`. Its 8KB limit applies to the compressed launcher, not the cached native executable. The native build uses `-march=native`, selecting the attack path for the build machine. The BMI2 path adds about 2.25 MiB of slider lookup storage at runtime. The UCI `Threads` option is fixed to 1; a separate input thread handles `stop` while the single search worker thinks. `Hash` supports 1–64 MB, default 8 MB.

## Engine

- Bitboards with precomputed knight/king attacks and BMI2/PEXT lookup tables for sliding pieces. Builds without BMI2 use hyperbola and a small rank-attack table.
- PVS, aspiration windows, singular extensions, LMR, null-move pruning, reverse futility, SEE pruning.
- TT/capture/killer/countermove ordering, continuation and correction history, and an evaluation cache.
- Tapered handcrafted evaluation with mobility, king safety, threats, pawn structure and passed pawns.
- Promotions, castling, normalized en-passant hashing, repetition and 50-move handling.
- UCI `wtime`/`btime`, increments, `movetime`, `depth`, `nodes`, `infinite` and asynchronous `stop`.

## Validation and reproducible matches

```sh
python3 -m pip install -r tests/requirements.txt
make test

# Keep a separately compiled checkout of the baseline as ./baseline.
python3 tests/bench.py ./baseline ./k8e
python3 tests/bench.py ./baseline ./k8e --movetime 0.15
python3 tests/match.py ./k8e ./baseline --pairs 100 --movetime 0.04 --out tests/results/timed
python3 tests/match.py ./k8e ./baseline --pairs 30 --nodes 20000 --out tests/results/nodes
```

The tests cover attack-table equivalence against independent rays, incremental hashes, evaluation/SEE immutability, standard and random perft positions, terminal positions, UCI interruption and packed/native search equivalence. The match runner pins itself and both engines to one available CPU, uses equal 8 MB hash, swaps colors for every opening, validates all moves with `python-chess`, and saves PGN plus a JSON summary. Its confidence interval groups reverse-color games into pairs. `--movetime 0.04` requests 40 ms per move; both versions reserve 10 ms internally.

Generated reports and PGN files stay in the ignored `tests/results/` directory.

## Credits and license

- Base architecture: [c4ke](https://github.com/ciekals/c4ke) by Ciekals.
- Search inspiration: [Ethereal](https://github.com/AndyGrant/Ethereal) by Andrew Grant.
- Engine engineering: rrttyynn.

GNU General Public License v3.0; see [LICENSE](LICENSE).
