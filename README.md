# KNIGHT (K8E)

**KNIGHT (K8E)** is a compact, high-performance UCI chess engine engineered to fit strictly within an **8,192-byte (8KB)** standalone executable.

 Achieving **~3355 CCRL Blitz Elo** (+45 Elo over baseline in 5s+0.05s matches), it combines a fast bitboard core with modern search heuristics and an automated token-compacting LZMA2 build pipeline.

## Features

- **< 8KB Self-Caching Binary**: Packed via custom lexical minification and LZMA2 compression (`pack.py`), caching the native binary under `~/.cache/k8e` for zero-latency startup.
- **Full Chess & UCI Compliance**: Supports all promotions, normalized en-passant Zobrist hashing, 3-fold repetition, 50-move rule, and asynchronous UCI commands (`wtime`/`btime`, `movetime`, `depth`, `nodes`, `infinite`).
- **Search Architecture**: PVS with Aspiration Windows, Singular Extensions, Precalculated LMR, Null-Move Pruning, RFP, Futility/SEE Pruning, and Mate Distance Pruning.
- **Move Ordering & Caches**: Staged ordering (`TT` -> `Good Captures` -> `Killers` -> `Countermoves` -> `Quiets`), 3-ply Continuation History, Correction History, and a 32K-entry `EvalCache`.
- **Dynamic Time Management**: Best-move stability scaling and instant forced-move resolution.

## Build & Run

Requirements: Linux, `g++` (C++20), `python3`, and `xz-utils`.

    # Build both native (k8e) and <8KB release (k8e_release)
    make

    # Run UCI engine
    ./k8e_release

    # Run built-in Perft benchmark
    ./k8e perft 6

## Credits

- **Base Architecture**: Derived from [c4ke](https://github.com/ciekals/c4ke) by Ciekals.
- **Search Heuristics**: Inspired by [Ethereal](https://github.com/AndyGrant/Ethereal) by Andrew Grant.
- **Engineering & Optimization**: Developed by rrttyynn.

## License

MIT License
