#include "eval.cpp"

#define HASH_NONPAWN_WHITE 0
#define HASH_NONPAWN_BLACK 1
#define HASH_PAWN 2

struct Board {
    u64 checkers, hash, hash_corrhist[3], colors[2], pieces[6];
    i32 trend;
    i32 halfmove;
    u8 stm, castled, enpassant = SQUARE_NONE, board[64];

    null edit(i32 square, i32 piece) {
        i32 p = board[square];
        if (p < PIECE_NONE)
            pieces[p / 2] ^= 1ull << square,
            colors[p % 2] ^= 1ull << square,
            hash ^= KEYS[p][square],
            (p < WHITE_KNIGHT ? hash_corrhist[HASH_PAWN] : hash_corrhist[p % 2]) ^= KEYS[p][square];

        p = piece;
        if (p < PIECE_NONE)
            pieces[p / 2] ^= 1ull << square,
            colors[p % 2] ^= 1ull << square,
            hash ^= KEYS[p][square],
            (p < WHITE_KNIGHT ? hash_corrhist[HASH_PAWN] : hash_corrhist[p % 2]) ^= KEYS[p][square];

        board[square] = piece;
    }

    u64 attackers(u64 mask) {
        return
            (nw(mask) | ne(mask)) & pieces[PAWN] & colors[BLACK] |
            (sw(mask) | se(mask)) & pieces[PAWN] & colors[WHITE] |
            attack(mask, 0, KNIGHT) & pieces[KNIGHT] |
            attack(mask, 0, KING) & pieces[KING] |
            attack(mask, colors[WHITE] | colors[BLACK], BISHOP) & (pieces[BISHOP] | pieces[QUEEN]) |
            attack(mask, colors[WHITE] | colors[BLACK], ROOK) & (pieces[ROOK] | pieces[QUEEN]);
    }

    i32 quiet(i32 move) {
        return board[move_to(move)] > BLACK_KING && !move_promo(move) && !(board[move_from(move)] < WHITE_KNIGHT && move_to(move) == enpassant);
    }

    i32 see(i32 move, i32 threshold) {
        i32 from = move_from(move), to = move_to(move), side = !stm;
        if (move_promo(move) || board[from] < WHITE_KNIGHT && to == enpassant) return TRUE;
        if ((threshold -= VALUE[board[to] / 2]) > 0) return FALSE;
        if ((threshold += VALUE[board[from] / 2]) <= 0) return TRUE;

        u64 whites = colors[WHITE], blacks = colors[BLACK];
        colors[stm] ^= 1ull << from;

        for (; u64 threats = attackers(1ull << to) & colors[side];) {
            i32 type = PAWN;
            for (; type < KING && !(pieces[type] & threats); type++);
            side ^= 1;
            if ((threshold = VALUE[type] - threshold) < 0) {
                side ^= type == KING && attackers(1ull << to) & colors[side];
                break;
            }
            colors[!side] ^= 1ull << LSB(pieces[type] & threats);
        }
        colors[WHITE] = whites;
        colors[BLACK] = blacks;
        return side != stm;
    }

    void normalize_ep() {
        if (enpassant == SQUARE_NONE) return;
        u64 target = 1ull << enpassant;
        u64 candidates = (stm ? nw(target) | ne(target) : sw(target) | se(target)) & pieces[PAWN] & colors[stm];
        while (candidates) {
            Board child = *this;
            if (!child.make(move_make(LSB(candidates), enpassant))) return;
            candidates &= candidates - 1;
        }
        hash ^= KEYS[PIECE_NONE][enpassant];
        enpassant = SQUARE_NONE;
    }

    u64 make(i32 move) {
        i32 from = move_from(move), to = move_to(move), piece = board[from];
        halfmove++;
        halfmove *= board[to] > BLACK_KING && piece > BLACK_PAWN;
        edit(to, move_promo(move) ? move_promo(move) * 2 + stm : piece);
        edit(from, PIECE_NONE);

        hash ^= KEYS[PIECE_NONE][enpassant];
        if (piece < WHITE_KNIGHT && to == enpassant)
            edit(to ^ 8, PIECE_NONE);

        enpassant = piece < WHITE_KNIGHT && abs(from - to) == 16 ? to ^ 8 : SQUARE_NONE;
        hash ^= KEYS[PIECE_NONE][enpassant];
        hash ^= KEYS[PIECE_NONE][castled];

        if (piece > BLACK_QUEEN && (castled |= 3 << stm * 2) && abs(from - to) == 2) {
            if (attackers(1ull << (from + to) / 2) & colors[!stm]) return TRUE;
            edit(to + (to > from ? 1 : -2), PIECE_NONE);
            edit((from + to) / 2, WHITE_ROOK + stm);
        }

        if (from == H1 || to == H1) castled |= CASTLED_WK;
        if (from == A1 || to == A1) castled |= CASTLED_WQ;
        if (from == H8 || to == H8) castled |= CASTLED_BK;
        if (from == A8 || to == A8) castled |= CASTLED_BQ;

        hash ^= KEYS[PIECE_NONE][castled];
        hash ^= KEYS[PIECE_NONE][0];

        __builtin_prefetch(&TTABLE[hash >> TT_SHIFT]);

        stm ^= 1;
        checkers = attackers(pieces[KING] & colors[stm]) & colors[!stm];
        normalize_ep();
        return attackers(pieces[KING] & colors[!stm]) & colors[stm];
    }

    null add_pawn_moves(i32*& list, u64 targets, i32 offset) {
        for (; targets;) {
            i32 to = LSB(targets);
            targets &= targets - 1;
            if (to < 8 || to > 55)
                for (i32 promo = KNIGHT; promo <= QUEEN; promo++) *list++ = move_make(to - offset, to, promo);
            else *list++ = move_make(to - offset, to);
        }
    }

    null add_moves(i32*& list, u64 targets, u64 occupied, u64 mask, i32 type) {
        for (; mask;) {
            i32 from = LSB(mask);
            mask &= mask - 1;
            u64 attacks = attack(1ull << from, occupied, type) & targets;
            for (; attacks;) {
                i32 to = LSB(attacks);
                attacks &= attacks - 1;
                *list++ = move_make(from, to);
            }
        }
    }

    i32 movegen(i32* list, i32 is_all) {
        i32* list_start = list;
        u64 occupied = colors[WHITE] | colors[BLACK],
            targets = is_all ? ~colors[stm] : colors[!stm],
            pawns = pieces[PAWN] & colors[stm],
            pawns_push = (stm ? south(pawns) : north(pawns)) & ~occupied & (is_all ? ~0ull : 0xff000000000000ffULL),
            pawns_targets = colors[!stm] | (enpassant < SQUARE_NONE ? 1ull << enpassant : 0);

        add_pawn_moves(list, pawns_push, stm ? -8 : 8);
        add_pawn_moves(list, (stm ? south(pawns_push & 0xff0000000000ULL) : north(pawns_push & 0xff0000ULL)) & ~occupied, stm ? -16 : 16);
        add_pawn_moves(list, (stm ? se(pawns) : nw(pawns)) & pawns_targets, stm ? -7 : 7);
        add_pawn_moves(list, (stm ? sw(pawns) : ne(pawns)) & pawns_targets, stm ? -9 : 9);

        add_moves(list, targets, occupied, pieces[KING] & colors[stm], KING);
        add_moves(list, targets, occupied, pieces[KNIGHT] & colors[stm], KNIGHT);
        add_moves(list, targets, occupied, (pieces[BISHOP] | pieces[QUEEN]) & colors[stm], BISHOP);
        add_moves(list, targets, occupied, (pieces[ROOK] | pieces[QUEEN]) & colors[stm], ROOK);

        if (is_all && !checkers && ~castled >> stm * 2 & 1 && !(occupied & 96ull << stm * 56)) *list++ = move_make(E1 + stm * 56, G1 + stm * 56);
        if (is_all && !checkers && ~castled >> stm * 2 & 2 && !(occupied & 14ull << stm * 56)) *list++ = move_make(E1 + stm * 56, C1 + stm * 56);

        return list - list_start;
    }

    i32 eval() {
        u64 ekey = hash ^ u64(trend * 0x9e3779b97f4a7c15ULL);
        EvalCacheEntry& ec = EVAL_CACHE[ekey & 32767];
        if (ec.key == ekey) return ec.eval;

        i32 eval = trend / 2 + (trend / 4 << 16), phases[2] {};

        for (i32 color = WHITE; color < 2; color++) {
            u64 pawns_us = pieces[PAWN] & colors[color],
                pawns_them = pieces[PAWN] & colors[!color],
                pawns_attacks = ne(pawns_us) | nw(pawns_us),
                pawns_threats = se(pawns_them) | sw(pawns_them),
                pawns_them_push = south(pawns_them) & ~(colors[WHITE] | colors[BLACK]),
                pawns_push_threats = se(pawns_them_push) | sw(pawns_them_push);

            i32 king_us = LSB(pieces[KING] & colors[color]),
                king_them = LSB(pieces[KING] & colors[!color]);

            eval += (POPCNT(pieces[BISHOP] & colors[color]) > 1) * BISHOP_PAIR -
                POPCNT(pawns_us & (north(pawns_us) | pawns_us << 16)) * PAWN_DOUBLED;

            // Specialize each piece type to fold its evaluation branches.
#pragma GCC unroll 6
            for (i32 type = PAWN; type < TYPE_NONE; type++) {
                u64 mask = pieces[type] & colors[color];
                for (; mask;) {
                    i32 square = LSB(mask);
                    mask &= mask - 1;
                    phases[color] += PHASE[type];
                    eval += PSQT[type][square];

                    if (!type) {
                        if (west(pawns_us) & pawns_us & 1ull << square)
                            eval += (get_data(square / 8 + INDEX_PHALANX) + OFFSET_PHALANX) * SCALE;

                        if (!(0x101010101010101ULL << square & (pawns_them | pawns_threats)))
                            eval += (
                                get_data(square / 8 + INDEX_PASSER) +
                                get_data(max(abs(square / 8 - king_us / 8 + 1), abs(square % 8 - king_us % 8)) + INDEX_KING_PASSER_US) +
                                get_data(max(abs(square / 8 - king_them / 8 + 1), abs(square % 8 - king_them % 8)) + INDEX_KING_PASSER_THEM) +
                                OFFSET_PASSER
                            ) * SCALE;

                        if (1ull << square & pawns_attacks)
                            eval += PAWN_PROTECTED;
                    } else {
                        u64 mobility = attack(1ull << square, colors[WHITE] | colors[BLACK], type);
                        eval += (get_data(type + INDEX_MOBILITY) + OFFSET_MOBILITY) * POPCNT(mobility & ~colors[color] & ~pawns_threats);

                        if (1ull << square & pawns_threats)
                            eval -= (get_data(type + INDEX_THREAT) + OFFSET_THREAT) * SCALE;

                        if (!(0x101010101010101ULL << square % 8 & pieces[PAWN]))
                            eval += (type > QUEEN) * KING_OPEN + (type == ROOK) * ROOK_OPEN;

                        if (!(0x101010101010101ULL << square % 8 & pawns_us))
                            eval += (type > QUEEN) * KING_SEMIOPEN + (type == ROOK) * ROOK_SEMIOPEN;

                        if (type > QUEEN)
                            eval += POPCNT(pawns_us & 460544ULL << 5 * (square % 8 > 2)) * PAWN_SHIELD * (square < A2);
                        else if (pieces[QUEEN])
                            eval += POPCNT(mobility & attack(1ull << king_them, 0, KING)) * (get_data(type + INDEX_KING_ATTACK) + OFFSET_KING_ATTACK);

                        if (1ull << square & pawns_push_threats)
                            eval -= get_data(type + INDEX_PUSH_THREAT) + OFFSET_PUSH_THREAT;

                        if (type < ROOK && 256ull << square & pieces[PAWN])
                            eval += MINOR_BEHIND_PAWN;

                        if (type == ROOK && square / 8 == 6 && (king_them / 8 == 7 || (pawns_them & 0xff000000000000ULL)))
                            eval += S(22, 32);

                        if (type == KNIGHT && square / 8 >= 3 && square / 8 <= 5 && (1ull << square & pawns_attacks))
                            eval += S(16, 10);
                    }
                }
            }

            for (i32 type = PAWN; type < TYPE_NONE; type++)
                pieces[type] = BSWAP(pieces[type]),
                colors[type % 2] = BSWAP(colors[type % 2]);

            eval = -eval;
        }

        i32 strong = eval < 0,
            phase = min(24, phases[WHITE] + phases[BLACK]),
            x = POPCNT(pieces[PAWN] & colors[strong]);

        i32 res = (
            i16(eval = stm ? -eval : eval) * phase +
            (eval >> 16) * (
                !x && phases[strong] - phases[!strong] < 2 ? 1 :
                8 + x
            ) / 16 * (24 - phase)
        ) / 24 + TEMPO;

        ec.key = ekey;
        ec.eval = res;
        return res;
    }

    void from_fen(istream& fen) {
        memset(this, 0, sizeof(Board));
        memset(board, PIECE_NONE, 64);
        string token;
        fen >> token;
        i32 square = 56;
        for (char c : token) {
            if (isdigit(c)) square += c - '0';
            else if (c == '/') square -= 16;
            else {
                i32 piece =
                    c == 'P' ? WHITE_PAWN : c == 'N' ? WHITE_KNIGHT : c == 'B' ? WHITE_BISHOP :
                    c == 'R' ? WHITE_ROOK : c == 'Q' ? WHITE_QUEEN : c == 'K' ? WHITE_KING :
                    c == 'p' ? BLACK_PAWN : c == 'n' ? BLACK_KNIGHT : c == 'b' ? BLACK_BISHOP :
                    c == 'r' ? BLACK_ROOK : c == 'q' ? BLACK_QUEEN : c == 'k' ? BLACK_KING : PIECE_NONE;
                edit(square, piece);
                square += 1;
            }
        }
        fen >> token;
        if (token == "b") {
            stm = BLACK;
            hash ^= KEYS[PIECE_NONE][0];
        }
        fen >> token;
        castled = CASTLED_WK | CASTLED_WQ | CASTLED_BK | CASTLED_BQ;
        for (char c : token) {
            if (c == 'K') castled ^= CASTLED_WK;
            if (c == 'Q') castled ^= CASTLED_WQ;
            if (c == 'k') castled ^= CASTLED_BK;
            if (c == 'q') castled ^= CASTLED_BQ;
        }
        hash ^= KEYS[PIECE_NONE][castled];
        fen >> token;
        enpassant = SQUARE_NONE;
        if (token != "-") {
            enpassant = (token[1] - '1') * 8 + token[0] - 'a' + A1;
            hash ^= KEYS[PIECE_NONE][enpassant];
        }
        fen >> token;
        halfmove = stoi(token);
        fen >> token;
        checkers = attackers(pieces[KING] & colors[stm]) & colors[!stm];
        normalize_ep();
    }

    null startpos() {
        *this = {};
        for (i32 i = 0; i < 64; i++) board[i] = PIECE_NONE;
        for (i32 i = 0; i < 8; i++)
            edit(i + A1, LAYOUT[i] * 2 + WHITE),
            edit(i + A8, LAYOUT[i] * 2 + BLACK),
            edit(i + A2, WHITE_PAWN),
            edit(i + 48, BLACK_PAWN);
        hash ^= KEYS[PIECE_NONE][castled];
    }
} BOARD;
