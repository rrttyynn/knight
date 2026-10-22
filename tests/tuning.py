#!/usr/bin/env python3
"""Check the offline evaluator against independently compiled production code."""
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile

import chess
import chess.engine

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import tuning_model
from tune import game_split


def run_scores(executable, fens, env=None):
    output = subprocess.check_output([str(executable), "--scores"], input="\n".join(fens) + "\n",
                                     text=True, env=env)
    return list(map(int, output.split()))


def main():
    rng = random.Random(20261022)
    board = chess.Board()
    fens = [board.fen()]
    for _ in range(96):
        if board.is_game_over():
            board.reset()
        board.push(rng.choice(list(board.legal_moves)))
        fens.append(board.fen())
    with tempfile.TemporaryDirectory(prefix="tuning-test-", dir=ROOT / "build") as temporary:
        directory = Path(temporary)
        shutil.copytree(ROOT / "src", directory / "src")
        (directory / "tools").mkdir()
        shutil.copy2(ROOT / "tools/eval_driver.cpp", directory / "tools/eval_driver.cpp")
        tuning_model.ROOT = directory
        model = tuning_model.Model()
        driver = model.build()
        reference = directory / "reference.cpp"
        reference.write_text('#define main engine_main\n#include "src/main.cpp"\n#undef main\n'
                             'int main() { initialize(); string line; while (getline(cin,line)) {'
                             'stringstream input(line); Board b; b.from_fen(input);'
                             'cout << (b.stm ? -1 : 1) * b.eval() << "\\n"; } free(TTABLE); }\n')
        binary = directory / "reference"
        def compile_reference():
            subprocess.run(["g++", "-O2", "-std=c++20", "-march=native", "-pthread",
                            str(reference), "-o", str(binary)], check=True)
        compile_reference()
        expected = run_scores(binary, fens)
        assert run_scores(driver, fens) == expected, "instrumented baseline evaluation differs"
        profile = model.profile("test")
        original_tempo = model.weights["TEMPO"][0]
        profile["evaluation"]["TEMPO"][0] += 7
        assert model.weights["TEMPO"][0] == original_tempo, "profile mutation changed the baseline"
        profile["evaluation"]["MATERIAL_0"][1] += 2
        profile["evaluation"]["BISHOP_PAIR"][0] += 3
        profile["evaluation"]["DATA_8"][0] += 1
        profile["evaluation"]["BOARD_0"][1] += 1
        profile["search"]["LMR_SCALE"] += .01
        profile["search"]["NULL_DIVISOR"] = float(profile["search"]["NULL_DIVISOR"])
        environment = model.environment(profile, directory / "weights")
        changed = run_scores(driver, fens, environment)
        assert changed != expected, "tuning profile did not reach the evaluator"
        model.apply(profile)
        compile_reference()
        assert run_scores(binary, fens) == changed, "applied production weights differ from tuning weights"
        try:
            tuning_model.Model().validate(profile)
        except ValueError:
            pass
        else:
            raise AssertionError("a stale source profile was accepted")
        # The same generated binary also serves UCI with mutable search constants.
        with chess.engine.SimpleEngine.popen_uci(str(driver), env=environment) as engine:
            result = engine.play(chess.Board(), chess.engine.Limit(nodes=256))
            assert result.move in chess.Board().legal_moves
        native = directory / "native"
        subprocess.run(["g++", "-O3", "-std=c++20", "-march=native", "-pthread",
                        str(directory / "src/main.cpp"), "-o", str(native)], check=True)
        with chess.engine.SimpleEngine.popen_uci(str(driver), env=environment) as offline, \
                chess.engine.SimpleEngine.popen_uci(str(native)) as production:
            for fen in (fens[0], fens[40], fens[80]):
                results = [engine.play(chess.Board(fen), chess.engine.Limit(nodes=10000),
                                       game=object(), info=chess.engine.INFO_ALL)
                           for engine in (offline, production)]
                assert results[0].move == results[1].move
                assert results[0].info["score"] == results[1].info["score"], \
                    "offline search differs from the applied production profile"
    groups = [f"opening-{i // 2}" for i in range(20)]
    split = game_split(groups, 42)
    assert all(split[i] == split[i + 1] for i in range(0, 20, 2))
    assert any(split) and not all(split), "holdout split is empty"
    print("tuning: exact evaluation/search equivalence, profile application, UCI and grouped holdout passed")


if __name__ == "__main__":
    main()
