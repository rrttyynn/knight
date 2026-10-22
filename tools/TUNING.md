# Offline tuning

The normal engine still uses one search worker and constant parameters. Neither
Python nor a weight file is needed by the native engine. The packer inlines
src/params.h before compression; the launcher must remain below 8,192 bytes.
Mutable evaluation weights exist only in the offline binary under build/.

## Setup and baseline

~~~sh
python3 -m pip install -r tools/requirements.txt
make
mkdir -p build
cp k8e build/baseline
python3 tools/tune.py build
~~~

Use a separate checkout when comparing different source revisions. Keep compiler
flags, Hash, CPU affinity and search limits equal.

## Evaluation: PGN to Texel fit

Generate complete, color-paired games, or provide PGNs with final results:

~~~sh
python3 tests/match.py ./k8e ./build/baseline --pairs 100 --nodes 6000 --seed 123 --out tests/results/data
python3 tools/tune.py collect tests/results/data.pgn --out tests/results/data.tsv
python3 tools/tune.py texel tests/results/data.tsv --out tests/results/texel.json
~~~

The TSV format is FEN, white result and opening group, separated by tabs. Results
are 0, 0.5 or 1. Collection samples every fourth ply after the first 16 PGN plies, drops check and
terminal positions, and groups both colors and repeated opening positions
together. Ordinary PGNs are grouped by their position after eight plies.
The --max-positions option provides seeded sampling for large corpora.

The evaluator is generated from the current production source. It exposes the
five material pairs, handcrafted terms, compressed table entries, two additional
board terms and tempo. Features are central finite differences of the actual
evaluation, so changes to evaluation logic need no duplicate implementation.
The engine's own quiescence search rejects volatile training positions
(--max-quiet-gap 80).

The fit minimizes the game-result Brier error of sigmoid(k * white_eval), with
equal total weight per opening group. Sigmoid calibration uses training data only.
Twenty percent of groups form a held-out set, including their reverse-color
games. Optimization uses normalized coordinates, bounded changes and L2
regularization. The default parameter prefixes select material and handcrafted
terms. Use --eval-params all for a sufficiently large corpus, or
--eval-params BOARD_,TEMPO for a smaller experiment. Bounds keep compressed
table entries in printable ASCII.

After fitting, weights are rounded to the production representation and evaluated
again with the exact C++ evaluator. The JSON reports both training and held-out
errors for the baseline and quantized candidate, the data hash and the source hash. An
improved training error alone does not qualify a profile for release.

~~~sh
# Inspect validation_mse_before/after and holdout_improved first.
python3 tools/tune.py apply tests/results/texel.json
make test
~~~

Small self-play corpora are useful for checking the pipeline. They do not establish
convergence or playing strength. Keep an independent match set for final testing.

## Search: paired-game SPSA

~~~sh
python3 tools/tune.py spsa --iterations 200 --pairs 2 --nodes 4000 --seed 456 --out tests/results/search.json
~~~

This builds one offline engine and starts two copies per iteration, with opposite
Bernoulli perturbations. Each opening is played with both colors. Legal moves,
draw claims and PGN rules use the same match implementation as the regular arena.
Search remains single-threaded; the runner pins itself and its engines to one
available CPU. Hash is fixed to 8 MB.

SEARCH in tuning_model.py specifies bounds, perturbation sizes and update scales.
The schedules use exponents 0.101 and 0.602. Integer coordinates retain useful
perturbations after rounding, and the final profile averages the second half of
the parameter trajectory. The --params LMR_SCALE,RFP_MARGIN option selects a
subset. The --movetime 0.2 option can replace the fixed node budget.

The JSON checkpoint and history JSONL record every completed iteration.
Use --steps to stop at an iteration boundary and --resume to continue:

~~~sh
python3 tools/tune.py spsa --iterations 200 --steps 50 --pairs 2 --nodes 4000 --seed 456 --out tests/results/search.json
python3 tools/tune.py spsa --iterations 200 --steps 50 --pairs 2 --nodes 4000 --seed 456 --out tests/results/search.json --resume
~~~

Continue until fit.complete is true. Resume checks source, parameter selection,
seed and limits. A partial checkpoint/history transaction is reported as an
error rather than silently discarded. Optimization games are not an independent
strength estimate.

## Applying and validating

Profiles never change production source during optimization. Application verifies
the exact source fingerprint before updating src/params.h, src/eval.cpp and
the explicit evaluation terms in src/board.cpp.

~~~sh
python3 tools/tune.py apply tests/results/search.json
make test
python3 tests/bench.py ./build/baseline ./k8e
python3 tests/match.py ./k8e ./build/baseline --pairs 200 --movetime 0.2 --seed 789 --out tests/results/validation
~~~

Use a new opening seed for final matches and report the paired confidence interval.
For small gains, use a larger independent match or an established SPRT runner
before claiming an Elo improvement. Check fixed-time performance as well as
fixed-node behavior. At 40 ms requested per move, the existing 10 ms reserve
accounts for a quarter of the budget.

The test suite verifies the offline evaluator against independently compiled
native code, including changed parameters and source application. It also checks
TT replacement, generation wrap, mate-score conversion, perft and UCI behavior,
and deterministic native/fallback/packed search equivalence.

PGNs, data, profiles and history produced during tuning belong under the ignored
tests/results/ directory. Generated files are not committed to the repository.
