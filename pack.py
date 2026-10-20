from pathlib import Path
import hashlib
import lzma
import re

root = Path(__file__).resolve().parent
parts = [re.sub(r'^#include "[^"\n]+"\n', '', (root / 'src' / name).read_text(), flags=re.M) for name in ('chess.cpp', 'eval.cpp', 'board.cpp', 'search.cpp', 'main.cpp')]
source = '\n'.join(parts)
lexer = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[A-Za-z_][A-Za-z_0-9]*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[A-Za-z_0-9]*|>>=|<<=|->\*|\.\.\.|::|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\|=|\^=|[^\s]')
names = 'move_make move_from move_to move_promo move_text update_history normalize_ep add_pawn_moves add_moves hash_corrhist stack_conthist stack_eval cutoff_count prev_move nodes_table root_legals is_improving quiet_count noisy_count move_scores move_list quiet_list noisy_list depth_next nodes_start singular_beta raw_hist hist_term prev_best best_stability candidates enpassant castled checkers attackers LEAPER RANK_ATTACKS TIME_START TIME_LIMIT TIME_SOFT LMR_TABLE get_data PIECE_NONE SQUARE_NONE MAX_PLY MAX_MOVE'.split()
names = list(dict.fromkeys(names + re.findall(r'^#define\s+([A-Za-z_]\w*)', source, flags=re.M) + 'SLIDER_MASKS SLIDER_ATTACKS sliding edges subset square occupied colors pieces eval score depth alpha beta'.split()))
aliases = {name: 'z' + str(i) for i, name in enumerate(names)}
assert not set(aliases.values()) & set(lexer.findall(source))

def tokens(text):
    return [aliases.get(t, t) for t in lexer.findall(text) if not t.startswith(('//', '/*'))]

def compact(text):
    out = []
    for t in tokens(text):
        if out and len(lexer.findall(out[-1] + t)) == 1:
            out.append(' ')
        out.append(t)
    return ''.join(out)

chunks = []
body = []
for line in source.splitlines():
    if line.startswith('#'):
        if body:
            chunks.append(compact('\n'.join(body)) + '\n')
            body = []
        chunks.append(lexer.sub(lambda m: aliases.get(m[0], m[0]), line) + '\n')
    else:
        body.append(line)
if body:
    chunks.append(compact('\n'.join(body)) + '\n')
payload = ''.join(chunks).encode()
assert tokens(source) == lexer.findall(payload.decode())
build = root / 'build'
build.mkdir(exist_ok=True)
(build / 'amalgamation.cpp').write_bytes(payload)
compressed = min((lzma.compress(payload, filters=[dict(id=lzma.FILTER_LZMA2, dict_size=65536, lc=lc, pb=pb, mode=lzma.MODE_NORMAL, nice_len=273, mf=lzma.MF_BT4, depth=1000)]) for lc in (2, 3, 4) for pb in (0, 1, 2)), key=len)
key = hashlib.sha256(payload).hexdigest()[:20]
header = f'''#!/bin/sh
set -e
D=${{XDG_CACHE_HOME:-$HOME/.cache}}/k8e
B=$D/{key}
if [ ! -x "$B" ]; then
mkdir -p "$D"
T=$(mktemp "$D/.XXXXXX")
trap 'rm -f "$T"' EXIT
sed '1,/^exit$/d' "$0"|xz -d|g++ -O3 -std=c++20 -march=native -DNDEBUG -fno-rtti -pthread -s -x c++ - -o "$T"
chmod +x "$T"
mv "$T" "$B"
fi
exec "$B" "$@"
exit
'''.encode()
release = header + compressed
if len(release) >= 8192:
    raise SystemExit(f'Release too large: {len(release)} bytes (must be < 8192)')
output = root / 'k8e_release'
output.write_bytes(release)
output.chmod(0o755)
print(f'k8e_release: {len(release)} bytes (< 8192)')
