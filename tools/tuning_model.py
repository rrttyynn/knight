"""Read, instrument and update the engine's actual evaluation and constants."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FILES = ("chess.cpp", "eval.cpp", "board.cpp", "search.cpp", "main.cpp")
PAIR = re.compile(r"S\((-?\d+),\s*(-?\d+)\)")
CONSTANT = re.compile(r"constexpr (int|double) (\w+) = ([0-9.]+);")
# name: minimum, maximum, SPSA perturbation, SPSA update scale
SEARCH = {
    "LMR_SCALE": (.25, .65, .05, .008),
    "LMR_OFFSET": (.3, 1.3, .15, .025),
    "RAZOR_MARGIN": (80, 250, 20, 3),
    "RFP_MARGIN": (30, 130, 12, 2),
    "NULL_MARGIN": (0, 100, 12, 2),
    "NULL_BASE": (3, 5, 1, .15),
    "NULL_DIVISOR": (2, 5, 1, .15),
    "NULL_EVAL_DIVISOR": (100, 350, 30, 5),
    "FUTILITY_MARGIN": (30, 140, 12, 2),
    "FUTILITY_OFFSET": (30, 170, 18, 3),
    "SEE_MARGIN": (30, 130, 12, 2),
    "ASPIRATION": (5, 32, 4, .6),
}


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


class Model:
    def __init__(self):
        self.source = {name: (ROOT / "src" / name).read_text() for name in FILES}
        self.header = (ROOT / "src/params.h").read_text()
        self.sha = hashlib.sha256((self.header + "".join(self.source.values())).encode()).hexdigest()
        self.types = {name: kind for kind, name, _ in CONSTANT.findall(self.header)}
        self.search = {name: float(value) if kind == "double" else int(value)
                       for kind, name, value in CONSTANT.findall(self.header)}
        if self.search.keys() != SEARCH.keys():
            raise ValueError("params.h and SEARCH specifications disagree")
        self.weights = {}
        material = re.search(r"MATERIAL\[\] \{([^}]+)\}", self.source["eval.cpp"])[1]
        for i, pair in enumerate(PAIR.findall(material)):
            self.weights[f"MATERIAL_{i}"] = list(map(int, pair))
        self.material_count = len(self.weights)
        for name, mg, eg in re.findall(r"^#define (\w+) S\((-?\d+),\s*(-?\d+)\)$",
                                      self.source["eval.cpp"], re.M):
            self.weights[name] = [int(mg), int(eg)]
        for i, pair in enumerate(PAIR.findall(self.source["board.cpp"])):
            self.weights[f"BOARD_{i}"] = list(map(int, pair))
        self.data_string = json.loads(re.search(r'^#define DATA_STR (".*")$',
                                               self.source["eval.cpp"], re.M)[1])
        self.eg_index = int(re.search(r"#define INDEX_EG (\d+)", self.source["eval.cpp"])[1])
        if len(self.data_string) != 2 * self.eg_index:
            raise ValueError("Unexpected compressed evaluation table layout")
        for i in range(self.eg_index):
            self.weights[f"DATA_{i}"] = [ord(self.data_string[i]) - 32,
                                        ord(self.data_string[i + self.eg_index]) - 32]
        self.weights["TEMPO"] = [int(re.search(r"#define TEMPO (\d+)",
                                             self.source["eval.cpp"])[1]), 0]
        self.index = {name: i for i, name in enumerate(self.weights)}

    def profile(self, kind):
        return dict(schema=1, kind=kind, source_sha256=self.sha,
                    search=dict(self.search),
                    evaluation={name: list(pair) for name, pair in self.weights.items()})

    def validate(self, profile, check_source=True):
        if profile.get("schema") != 1:
            raise ValueError("Unsupported tuning profile schema")
        if check_source and profile.get("source_sha256") != self.sha:
            raise ValueError("Profile belongs to different source; rebuild/re-tune before applying")
        if profile["search"].keys() != self.search.keys() or profile["evaluation"].keys() != self.weights.keys():
            raise ValueError("Profile parameters disagree with this engine")
        for name, value in profile["search"].items():
            lo, hi, _, _ = SEARCH[name]
            if not lo <= value <= hi or (self.types[name] == "int" and int(value) != value):
                raise ValueError(f"Invalid search value: {name}={value}")
        for name, pair in profile["evaluation"].items():
            if len(pair) != 2 or any(type(v) is not int or abs(v) > 8191 for v in pair):
                raise ValueError(f"Invalid evaluation pair: {name}")
            if name.startswith("DATA_") and any(not 0 <= v <= 94 for v in pair):
                raise ValueError("Compressed table values must fit printable ASCII")
        if profile["evaluation"]["TEMPO"][1] != 0:
            raise ValueError("TEMPO has no endgame component")

    def environment(self, profile, path):
        self.validate(profile)
        path = Path(path).resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        values = [int(profile["search"][name]) if self.types[name] == "int"
                  else float(profile["search"][name]) for name in self.search]
        values += [v for name in self.weights for v in profile["evaluation"][name]]
        path.write_text(" ".join(map(str, values)) + "\n")
        return dict(os.environ, K8E_TUNE_PROFILE=str(path))

    def build(self, directory=None):
        directory = Path(directory or ROOT / "build/tuning").resolve()
        directory.mkdir(parents=True, exist_ok=True)
        driver = ROOT / "tools/eval_driver.cpp"
        command = shlex.split(os.environ.get("CXX", "g++"))
        command += shlex.split(os.environ.get("CXXFLAGS", "-O3 -std=c++20 -march=native -DNDEBUG -fno-rtti -pthread"))
        signature = hashlib.sha256((self.sha + driver.read_text() + json.dumps(command)).encode()).hexdigest()
        stamp = directory / "signature"
        executable = directory / "tune-engine"
        if executable.exists() and stamp.exists() and stamp.read_text() == signature:
            return executable
        parts = dict(self.source)
        ev = parts["eval.cpp"]
        counter = iter(range(self.material_count))
        ev = re.sub(r"(MATERIAL\[\] \{)([^}]+)(\})",
                    lambda m: m[1] + PAIR.sub(lambda _: f"tw({next(counter)})", m[2]) + m[3], ev)
        for name in self.weights:
            if name.startswith(("MATERIAL_", "BOARD_", "DATA_")):
                continue
            pattern = rf"^#define {name} .+$"
            value = f"TUNE_MG[{self.index[name]}]" if name == "TEMPO" else f"tw({self.index[name]})"
            ev, count = re.subn(pattern, f"#define {name} {value}", ev, flags=re.M)
            if count != 1:
                raise ValueError(f"Evaluation macro not found: {name}")
        ev, count = re.subn(r"i32 get_data\(i32 index\) \{[^}]+\}",
                           f"i32 get_data(i32 index) {{ return tw({self.index['DATA_0']} + index); }}", ev)
        if count != 1:
            raise ValueError("get_data instrumentation failed")
        parts["eval.cpp"] = ev
        counter = iter(self.index[name] for name in self.weights if name.startswith("BOARD_"))
        parts["board.cpp"] = PAIR.sub(lambda _: f"tw({next(counter)})", parts["board.cpp"])
        vectors = "\n".join(
            f"vector<i32> TUNE_{side} {{{','.join(str(v[i]) for v in self.weights.values())}}};"
            for i, side in enumerate(("MG", "EG")))
        vectors += "\ni32 tw(i32 i) { return i32(uint32_t(TUNE_MG[i]) + (uint32_t(TUNE_EG[i]) << 16)); }\n"
        parts["chess.cpp"] += "\n" + vectors
        source = self.header.replace("constexpr ", "") + "\n" + "\n".join(
            re.sub(r'^#include "[^"\n]+"\n', "", parts[name], flags=re.M) for name in FILES)
        source += "\nvoid refresh_tune_material() {\n"
        source += "\n".join(f"MATERIAL[{i}] = tw({i});" for i in range(self.material_count)) + "\n}\n"
        source += "void load_tune_profile() {\nif (auto p = getenv(\"K8E_TUNE_PROFILE\")) {\nifstream in(p);\n"
        for name in self.search:
            source += f"in >> {name};\n"
        source += "for (size_t i = 0; i < TUNE_MG.size(); i++) in >> TUNE_MG[i] >> TUNE_EG[i];\n"
        source += 'if (!in) throw runtime_error("Invalid tuning profile file");\n}\nrefresh_tune_material();\n}\n'
        (directory / "tune_engine.cpp").write_text(source)
        subprocess.run(command + [str(driver), "-I", str(directory), "-o", str(executable)], check=True)
        stamp.write_text(signature)
        return executable

    def apply(self, profile):
        self.validate(profile)
        weights = profile["evaluation"]
        ev = self.source["eval.cpp"]
        counter = iter(name for name in self.weights if name.startswith("MATERIAL_"))
        ev = re.sub(r"(MATERIAL\[\] \{)([^}]+)(\})",
                    lambda m: m[1] + PAIR.sub(
                        lambda _: "S(%d, %d)" % tuple(weights[next(counter)]), m[2]) + m[3], ev)
        for name, pair in weights.items():
            if name.startswith(("MATERIAL_", "BOARD_", "DATA_")):
                continue
            value = str(pair[0]) if name == "TEMPO" else "S(%d, %d)" % tuple(pair)
            ev = re.sub(rf"^#define {name} .+$", f"#define {name} {value}", ev, flags=re.M)
        table = "".join(chr(weights[f"DATA_{i}"][phase] + 32)
                        for phase in (0, 1) for i in range(self.eg_index))
        ev = re.sub(r'^#define DATA_STR ".*"$',
                    lambda _: "#define DATA_STR " + json.dumps(table), ev, flags=re.M)
        board_counter = iter(name for name in self.weights if name.startswith("BOARD_"))
        board = PAIR.sub(lambda _: "S(%d, %d)" % tuple(weights[next(board_counter)]),
                         self.source["board.cpp"])
        header = CONSTANT.sub(
            lambda m: f"constexpr {m[1]} {m[2]} = "
            f"{int(profile['search'][m[2]]) if m[1] == 'int' else float(profile['search'][m[2]])};",
            self.header)
        # Prepare every replacement before writing any source.
        for name, text in (("eval.cpp", ev), ("board.cpp", board), ("params.h", header)):
            (ROOT / "src" / name).write_text(text)
