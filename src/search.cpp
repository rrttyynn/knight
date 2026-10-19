#include "board.cpp"

null update_history(i16& entry, i32 bonus) {
    entry += bonus - entry * abs(bonus) / HIST_MAX;
}

struct Thread {
    HTable nhist[6],
        conthist[12][64],
        *stack_conthist[STACK_SIZE];
    u64 nodes,
        nodes_table[4096],
        visited[STACK_SIZE];
    i16 id,
        qhist[2][4096],
        corrhist[2][CORRHIST_SIZE],
        stack_eval[STACK_SIZE],
        cutoff_count[STACK_SIZE];
    i32 killers[STACK_SIZE][2],
        cmtable[14][64],
        prev_move[STACK_SIZE],
        root_legals;

    i32 search(Board& board, i32 alpha, i32 beta, i32 ply, i32 depth, i32 is_pv = FALSE, i32 excluded = MOVE_NONE) {
        i32 eval,
            score,
            best = -INF,
            is_improving = FALSE,
            quiet_count = 0,
            noisy_count = 0,
            legals = 0,
            bound = BOUND_UPPER,
            move_scores[MAX_MOVE],
            move_list[MAX_MOVE],
            quiet_list[MAX_MOVE],
            noisy_list[MAX_MOVE];

        if (depth < 0) depth = 0;
        if (!id && !(++nodes & 4095) && now() > TIME_LIMIT) STOP++;
        if (STOP || ply >= MAX_PLY) return DRAW;

        if (ply) {
            for (i32 i = 4; i <= ply; i += 2)
                if (board.hash == visited[ply - i]) return DRAW;
            for (i32 i = 0; i < VISITED_COUNT; i++)
                if (board.hash == VISITED[i]) return DRAW;
            if (board.halfmove > 99) return DRAW;

            i32 r_alpha = max(alpha, ply - INF),
                r_beta = min(beta, INF - ply - 1);
            if (r_alpha >= r_beta) return r_alpha;
        }

        visited[ply] = board.hash;

        TTEntry tt = TTABLE[board.hash >> TT_SHIFT];
        if (tt.key != i16(board.hash)) tt = {};
        else if (!is_pv && !excluded && depth <= tt.depth && tt.bound != tt.score < beta)
            return tt.score;

        stack_eval[ply] = INF;
        cutoff_count[ply + 1] = 0;
        killers[ply + 1][0] = killers[ply + 1][1] = 0;

        if (!board.checkers) {
            eval = stack_eval[ply] = board.eval() +
                corrhist[board.stm][board.hash_corrhist[HASH_PAWN] % CORRHIST_SIZE] / 105 +
                corrhist[board.stm][board.hash_corrhist[HASH_NONPAWN_WHITE] % CORRHIST_SIZE] / 131 +
                corrhist[board.stm][board.hash_corrhist[HASH_NONPAWN_BLACK] % CORRHIST_SIZE] / 131 +
                stack_conthist[ply + 3][0][0][0] / 118 +
                stack_conthist[ply + 2][0][1][0] / 210;

            if (tt.key && !excluded && tt.bound != tt.score < eval)
                eval = tt.score;

            is_improving = ply > 1 && stack_eval[ply] > stack_eval[ply - 2];

            if (!is_pv && !excluded && depth < 6 && stack_eval[ply] + 159 * depth < alpha)
                depth = 0;

            if (!depth && (alpha = max(alpha, best = eval)) >= beta)
                return eval;

            if (!is_pv && !excluded && depth && depth < 9 && eval < WIN && eval > beta + 70 * depth - 70 * is_improving)
                return (eval + beta) / 2;

            if (!is_pv && !excluded && depth > 2 && eval > beta + 28 && board.colors[board.stm] & ~board.pieces[PAWN] & ~board.pieces[KING]) {
                Board child = board;
                child.stm ^= 1;
                child.hash ^= KEYS[PIECE_NONE][0];
                child.hash ^= KEYS[PIECE_NONE][child.enpassant];
                child.enpassant = SQUARE_NONE;
                stack_conthist[ply + 4] = conthist[WHITE_PAWN];
                prev_move[ply] = 0;

                score = -search(child, -beta, -alpha, ply + 1, depth - 4 - depth / 3 - min(2, (eval - beta) / 200));
                if (score >= beta) return score < WIN ? score : beta;
            }
        }

        if (depth >= 4 && !tt.move && !excluded) depth--;

        i32 move_count = board.movegen(move_list, depth || board.checkers);
        i32 pmove = ply ? prev_move[ply - 1] : 0;
        i32 counter = (pmove && board.board[move_to(pmove)] < PIECE_NONE) ? cmtable[board.board[move_to(pmove)]][move_to(pmove)] : 0;

        for (i32 i = 0; i < move_count; i++) {
            i32 move = move_list[i];

            if (move == tt.move) {
                move_scores[i] = 100000000;
            } else if (!board.quiet(move)) {
                move_scores[i] = VALUE[board.board[move_to(move)] / 2 % TYPE_NONE] * 16 +
                    nhist[board.board[move_to(move)] / 2 % TYPE_NONE][board.board[move_from(move)]][move_to(move)] +
                    board.see(move, 0) * 20000000 - 10000000;
            } else if (move == killers[ply][0]) {
                move_scores[i] = 1000000;
            } else if (move == killers[ply][1]) {
                move_scores[i] = 900000;
            } else if (move == counter) {
                move_scores[i] = 800000;
            } else {
                move_scores[i] = qhist[board.stm][move & 4095] +
                    stack_conthist[ply][0][board.board[move_from(move)]][move_to(move)] +
                    i32(2.1 * stack_conthist[ply + 2][0][board.board[move_from(move)]][move_to(move)]) +
                    i32(2.2 * stack_conthist[ply + 3][0][board.board[move_from(move)]][move_to(move)]);
            }
        }

        for (i32 i = 0; i < move_count; i++) {
            i32 next = i;
            for (i32 k = i; k < move_count; k++)
                if (move_scores[k] > move_scores[next])
                    next = k;

            swap(move_list[i], move_list[next]);
            swap(move_scores[i], move_scores[next]);

            i32 move = move_list[i],
                is_quiet = board.quiet(move),
                depth_next = depth - 1;

            u64 nodes_start = nodes;

            if (!depth && best > -WIN && move_scores[i] < 1000000) break;
            if (ply && best > -WIN && quiet_count > 1 + depth * depth >> !is_improving && is_quiet && move != killers[ply][0] && move != killers[ply][1] && move != counter) continue;
            if (ply && best > -WIN && depth < 10 && !board.checkers && move != killers[ply][0] && move != killers[ply][1] && move != counter && stack_eval[ply] + 76 * depth + (is_quiet ? move_scores[i] % 100000 : 0) / 27 + 89 < alpha && is_quiet) continue;
            if (ply && best > -WIN && move_scores[i] < 1000000 && !board.see(move, -74 * depth)) continue;

            Board child = board;
            if (move == excluded || child.make(move)) continue;

            if (ply && depth > 3 && !excluded && move == tt.move && tt.depth > depth - 4 && tt.bound && abs(tt.score) < WIN) {
                i32 singular_beta = tt.score - depth / 2;
                score = search(board, singular_beta - 1, singular_beta, ply, depth_next / 2, FALSE, move);

                if (score < singular_beta)
                    depth_next += 1 + (!is_pv && score < singular_beta - 9) + (!is_pv && score < singular_beta - 33 && is_quiet);
                else if (score >= beta)
                    return score;
            }

            stack_conthist[ply + 4] = &conthist[board.board[move_from(move)]][move_to(move)];
            prev_move[ply] = move;
            score = beta;

            if (depth > 2 && legals) {
                i32 raw_hist = is_quiet ? (move_scores[i] % 100000) : nhist[board.board[move_to(move)] / 2 % TYPE_NONE][board.board[move_from(move)]][move_to(move)];
                i32 hist_term = is_quiet ? clamp(raw_hist / 8432, -2, 2) : clamp(raw_hist / 3990, -2, 2);

                i32 reduction =
                    LMR_TABLE[min(depth, 63)][min(legals + 1, 63)] -
                    is_pv -
                    hist_term -
                    ((move == killers[ply][0] || move == killers[ply][1] || move == counter) ? 1 : 0) -
                    !!child.checkers +
                    (!is_improving && is_quiet) +
                    (tt.move && !board.quiet(tt.move)) +
                    (cutoff_count[ply + 1] > 3);

                if (reduction > 0)
                    score = -search(child, -alpha - 1, -alpha, ply + 1, depth_next - reduction),
                    depth_next += (score > best + 50) - (score < best + 7 && reduction > 1);
            }

            if (score > alpha && depth && legals)
                score = -search(child, -alpha - 1, -alpha, ply + 1, depth_next);

            if (!depth || !legals || (is_pv && score > alpha))
                score = -search(child, -beta, -alpha, ply + 1, depth_next, is_pv);

            legals++;
            if (STOP) return DRAW;
            if (!ply) nodes_table[move & 4095] += nodes - nodes_start;

            if (score > best) best = score;
            if (score > alpha) {
                alpha = score;
                tt.move = move;
                bound = BOUND_EXACT;
                if (!id && !ply) BEST_MOVE = move;
            }

            if (score >= beta) {
                bound = BOUND_LOWER;
                if (depth) {
                    cutoff_count[ply]++;
                    i32 bonus = min(200 * depth - 60, 1715) + (stack_eval[ply] <= best) * 160;

                    if (is_quiet) {
                        if (move != killers[ply][0]) {
                            killers[ply][1] = killers[ply][0];
                            killers[ply][0] = move;
                        }
                        if (pmove && board.board[move_to(pmove)] < PIECE_NONE)
                            cmtable[board.board[move_to(pmove)]][move_to(pmove)] = move;

                        update_history(qhist[board.stm][move & 4095], bonus);
                        update_history(stack_conthist[ply][0][board.board[move_from(move)]][move_to(move)], bonus);
                        update_history(stack_conthist[ply + 2][0][board.board[move_from(move)]][move_to(move)], bonus);
                        update_history(stack_conthist[ply + 3][0][board.board[move_from(move)]][move_to(move)], bonus);

                        bonus = -bonus;
                        for (i32 k = 0; k < quiet_count; k++) {
                            i32 qm = quiet_list[k];
                            update_history(qhist[board.stm][qm & 4095], bonus);
                            update_history(stack_conthist[ply][0][board.board[move_from(qm)]][move_to(qm)], bonus);
                            update_history(stack_conthist[ply + 2][0][board.board[move_from(qm)]][move_to(qm)], bonus);
                            update_history(stack_conthist[ply + 3][0][board.board[move_from(qm)]][move_to(qm)], bonus);
                        }
                    } else {
                        update_history(nhist[board.board[move_to(move)] / 2 % TYPE_NONE][board.board[move_from(move)]][move_to(move)], bonus);
                        bonus = -bonus;
                        for (i32 k = 0; k < noisy_count; k++) {
                            i32 nm = noisy_list[k];
                            update_history(nhist[board.board[move_to(nm)] / 2 % TYPE_NONE][board.board[move_from(nm)]][move_to(nm)], bonus);
                        }
                    }
                }
                break;
            }
            (is_quiet ? quiet_list[quiet_count++] : noisy_list[noisy_count++]) = move;
        }

        if (!ply) root_legals = legals;

        if (!legals && board.checkers) return ply - INF;
        if (!legals && depth) return DRAW;

        if (!board.checkers && (!bound || board.quiet(tt.move)) && bound != (best < stack_eval[ply])) {
            i32 bonus = clamp((best - stack_eval[ply]) * depth, -596, 596) * 7;
            update_history(corrhist[board.stm][board.hash_corrhist[HASH_PAWN] % CORRHIST_SIZE], bonus);
            update_history(corrhist[board.stm][board.hash_corrhist[HASH_NONPAWN_WHITE] % CORRHIST_SIZE], bonus);
            update_history(corrhist[board.stm][board.hash_corrhist[HASH_NONPAWN_BLACK] % CORRHIST_SIZE], bonus);
            update_history(stack_conthist[ply + 3][0][0][0], bonus);
            update_history(stack_conthist[ply + 2][0][1][0], bonus);
        }

        if (!excluded)
            TTABLE[board.hash >> TT_SHIFT] = { i16(board.hash), i16(tt.move), i16(best), u8(depth), u8(bound) };

        return best;
    }

    null start(i32 ID) {
        id = ID;
        nodes = 0;
        root_legals = 0;
        memset(nodes_table, 0, sizeof(nodes_table));
        i32 score = 0, prev_best = 0, best_stability = 0;
        Board board = BOARD;

        for (i32 depth = 1; depth < 256; depth++) {
            stack_conthist[0] = stack_conthist[1] = stack_conthist[2] = stack_conthist[3] = &conthist[WHITE_PAWN][B1];

            i32 delta = 9,
                alpha = score - delta,
                beta = score + delta,
                reduction = 0;

            for (;;) {
                score = search(board, alpha, beta, 0, max(depth - reduction, 1), TRUE);

                if (score <= alpha)
                    beta = (alpha + beta) / 2,
                    alpha = score - delta,
                    reduction = 0;
                else if (score >= beta)
                    beta = score + delta,
                    reduction++;
                else
                    break;

                board.trend = clamp(board.stm ? -score : score, -81, 81);
            }

            if (BEST_MOVE == prev_best) best_stability++;
            else best_stability = 0;
            prev_best = BEST_MOVE;

            if (!id) {
                cout << "info depth " << depth << " score ";
                if (score >= WIN) cout << "mate " << i32((INF - score) / 2) << " ";
                else if (score <= -WIN) cout << "mate " << i32((-INF - score) / 2) << " ";
                else cout << "cp " << score << " ";
                cout << "nodes " << nodes << " nps " << u64(nodes * 1000 / max(now() - TIME_START, u64(1))) << " pv ";
                cout.put(97 + move_from(BEST_MOVE) % 8).put(49 + move_from(BEST_MOVE) / 8).put(97 + move_to(BEST_MOVE) % 8).put(49 + move_to(BEST_MOVE) / 8).put(move_promo(BEST_MOVE) ? 'q' : 32) << endl;
            }

            if (!id && depth >= 1 && root_legals == 1) break;

            double factor = 2.0 - 1.5 * (double)nodes_table[BEST_MOVE & 4095] / max(nodes, u64(1));
            if (best_stability >= 4) factor *= 0.75;
            else if (!best_stability && depth > 5) factor *= 1.25;

            if (!id && now() > TIME_START + u64(TIME_SOFT * factor))
                STOP++;

            if (STOP) break;
        }

        if (!id)
            STOP++,
            cout << "bestmove ",
            cout.put(97 + move_from(BEST_MOVE) % 8).put(49 + move_from(BEST_MOVE) / 8).put(97 + move_to(BEST_MOVE) % 8).put(49 + move_to(BEST_MOVE) / 8).put(move_promo(BEST_MOVE) ? 'q' : 32) << endl;
    }
};
