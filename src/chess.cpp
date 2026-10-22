#include <bits/stdc++.h>
#include "params.h"
#ifdef __BMI2__
#include <immintrin.h>
#endif

using namespace std;

using u8 = uint8_t;
using i16 = int16_t;
using i32 = int;
using u64 = uint64_t;
using null = void;
using HTable = i16[12][64];

#define TRUE 1
#define FALSE 0

#define WHITE 0
#define BLACK 1

#define PAWN 0
#define KNIGHT 1
#define BISHOP 2
#define ROOK 3
#define QUEEN 4
#define KING 5
#define TYPE_NONE 6

#define WHITE_PAWN 0
#define BLACK_PAWN 1
#define WHITE_KNIGHT 2
#define BLACK_KNIGHT 3
#define WHITE_BISHOP 4
#define BLACK_BISHOP 5
#define WHITE_ROOK 6
#define BLACK_ROOK 7
#define WHITE_QUEEN 8
#define BLACK_QUEEN 9
#define WHITE_KING 10
#define BLACK_KING 11
#define PIECE_NONE 12

#define A1 0
#define B1 1
#define C1 2
#define E1 4
#define G1 6
#define H1 7
#define A2 8
#define A8 56
#define H8 63
#define SQUARE_NONE 64

#define MAX_PLY 256
#define MAX_MOVE 256

#define WIN 30000
#define INF 32000
#define DRAW 0

#define CASTLED_WK 1
#define CASTLED_WQ 2
#define CASTLED_BK 4
#define CASTLED_BQ 8

#define BOUND_UPPER 0
#define BOUND_LOWER 1
#define BOUND_EXACT 2

#define MOVE_NONE 0
#define HIST_MAX 16384
#define CORRHIST_SIZE 16384
#define STACK_SIZE 264

i32 TT_BITS = 20;
i32 TT_SHIFT = 44;

string move_text(i32 move);

u64 now() {
    timespec t;
    clock_gettime(1, &t);
    return t.tv_sec * 1000ULL + t.tv_nsec / 1000000ULL;
}

i32 move_make(i32 from, i32 to, i32 promo = PAWN) {
    return from | to << 6 | promo << 12;
}

i32 move_from(i32 move) { return move & 63; }
i32 move_to(i32 move) { return move >> 6 & 63; }
i32 move_promo(i32 move) { return move >> 12; }

#define LSB countr_zero
#define POPCNT popcount
#define BSWAP __builtin_bswap64

u64 BEST_MOVE,
    TIME_START,
    TIME_SOFT,
    TIME_LIMIT,
    DIAG[2][64],
    KEYS[13][65];

vector<u64> VISITED;
u64 NODE_LIMIT = ~0ull;
i32 DEPTH_LIMIT = 255;

i32 LMR_TABLE[64][64];
// Tiny read-only attack tables keep the hot path independent of ray length.
u64 LEAPER[2][64];
u8 RANK_ATTACKS[8][256];
#ifdef __BMI2__
u64 SLIDER_MASKS[2][64], SLIDER_ATTACKS[64][4608];
#endif

u64 north(u64 b) { return b << 8; }
u64 south(u64 b) { return b >> 8; }
u64 west(u64 b) { return b >> 1 & ~0x8080808080808080ULL; }
u64 east(u64 b) { return b << 1 & ~0x0101010101010101ULL; }
u64 nw(u64 b) { return north(west(b)); }
u64 ne(u64 b) { return north(east(b)); }
u64 sw(u64 b) { return south(west(b)); }
u64 se(u64 b) { return south(east(b)); }

u64 ray(u64 mask, u64 occupied, auto func) {
    mask = func(mask);
    mask |= func(mask & ~occupied);
    mask |= func(mask & ~occupied);
    mask |= func(mask & ~occupied);
    mask |= func(mask & ~occupied);
    mask |= func(mask & ~occupied);
    mask |= func(mask & ~occupied);
    return mask;
}

u64 hyperbola(u64 mask, u64 occupied, u64 line) {
    return line & ((occupied & line) - mask ^ BSWAP(BSWAP(occupied & line) - BSWAP(mask)));
}

u64 sliding(u64 mask, u64 occupied, i32 type) {
    i32 square = LSB(mask);
    u64 moves = 0;
    if (type != ROOK)
        moves = hyperbola(mask, occupied, DIAG[0][square]) | hyperbola(mask, occupied, DIAG[1][square]);
    if (type != BISHOP)
        moves |= hyperbola(mask, occupied, mask ^ 0x0101010101010101ULL << square % 8) | (u64(RANK_ATTACKS[square % 8][occupied >> (square / 8 * 8) & 255]) << (square / 8 * 8));
    return moves;
}

u64 attack(u64 mask, u64 occupied, i32 type) {
    i32 square = LSB(mask);
    if (type < BISHOP || type > QUEEN) return LEAPER[type > QUEEN][square];
#ifdef __BMI2__
    u64 moves = 0;
    if (type != ROOK) moves = SLIDER_ATTACKS[square][_pext_u64(occupied, SLIDER_MASKS[0][square])];
    if (type != BISHOP) moves |= SLIDER_ATTACKS[square][512 + _pext_u64(occupied, SLIDER_MASKS[1][square])];
    return moves;
#else
    return sliding(mask, occupied, type);
#endif
}

struct TTEntry {
    i16 key, move, score;
    u8 depth, bound;
} *TTABLE;

u8 TT_GENERATION;

// The upper six bound bits hold the generation; entries stay eight bytes.
void tt_save(u64 hash, i32 move, i32 score, i32 depth, i32 bound) {
    TTEntry& entry = TTABLE[hash >> TT_SHIFT];
    i32 age = u8(TT_GENERATION - (entry.bound & 252)) / 4;
    if (!entry.key || depth + age * 4 + (bound == BOUND_EXACT) * 4 >= entry.depth) {
        if (!move && entry.key == i16(hash)) move = entry.move;
        entry = {i16(hash), i16(move), i16(score), u8(depth), u8(TT_GENERATION | bound)};
    } else if (entry.key == i16(hash) && move) entry.move = move;
}

struct EvalCacheEntry {
    u64 key;
    i32 eval;
} EVAL_CACHE[32768];

atomic<i32> STOP;

string move_text(i32 move) {
    if (!move) return "0000";
    string s = "a1a1";
    s[0] += move_from(move) % 8;
    s[1] += move_from(move) / 8;
    s[2] += move_to(move) % 8;
    s[3] += move_to(move) / 8;
    if (move_promo(move)) s += " pnbrq"[move_promo(move) + 1];
    return s;
}
