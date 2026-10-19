#include "search.cpp"

static Thread MAIN_THREAD;

u64 perft(Board& board, i32 depth) {
    if (depth <= 0) return 1;
    u64 nodes = 0;
    i32 moves[MAX_MOVE];
    i32 count = board.movegen(moves, TRUE);
    for (i32 i = 0; i < count; i++) {
        Board child = board;
        if (child.make(moves[i])) continue;
        nodes += perft(child, depth - 1);
    }
    return nodes;
}

i32 main(i32 argc, char *argv[]) {
    mt19937_64 rng;
    for (i32 i = 0; i < 832; i++)
        KEYS[i / 64][i % 64] = rng(),
        DIAG[0][i % 64 ^ 56] = BSWAP(DIAG[1][i % 64] = ray(1ull << i % 64, 0, se) | ray(1ull << i % 64, 0, nw));

    for (i32 d = 1; d < 64; d++)
        for (i32 l = 1; l < 64; l++)
            LMR_TABLE[d][l] = log(d) * log(l) * 0.43 + 0.83;

    TTABLE = (TTEntry*)calloc(1ull << TT_BITS, sizeof(TTEntry));

    if (argc > 2 && string(argv[1]) == "perft") {
        BOARD.startpos();
        u64 t1 = now();
        u64 n = perft(BOARD, stoi(argv[2]));
        u64 t2 = now();
        cout << n << " nodes " << (n * 1000 / max(t2 - t1, u64(1))) << " nps" << endl;
        free(TTABLE);
        return 0;
    }

    string token;
    if (!(cin >> token)) {
        free(TTABLE);
        return 0;
    }

    cout << "id name K8E\n";
    cout << "id author AI\n";
    cout << "option name Hash type spin default 8 min 1 max 64\n";
    cout << "option name Threads type spin default 1 min 1 max 1\n";
    cout << "uciok\n";

    for (; getline(cin, token);) {
        stringstream tokens(token);
        tokens >> token;

        if (token[0] == 'i')
            cout << "readyok\n";
        else if (token[0] == 'u') {
            memset(TTABLE, 0, sizeof(TTEntry) * (1ull << TT_BITS));
            memset(&MAIN_THREAD, 0, sizeof(Thread));
            memset(EVAL_CACHE, 0, sizeof(EVAL_CACHE));
        }
        else if (token[0] == 's') {
            tokens >> token >> token;
            if (token == "Hash") {
                u64 tt_size;
                tokens >> token >> tt_size;
                tt_size *= 1024 * 1024 / sizeof(TTEntry);
                TT_BITS = 63 - __builtin_clzll(tt_size);
                TT_SHIFT = 64 - TT_BITS;
                free(TTABLE);
                TTABLE = (TTEntry*)calloc(1ull << TT_BITS, sizeof(TTEntry));
            }
        }
        else if (token[0] == 'p') {
            VISITED_COUNT = 0;
            BOARD.startpos();
            tokens >> token;
            if (token[0] == 'f')
                BOARD.from_fen(tokens);
            tokens >> token;

            for (; tokens >> token;)
                BEST_MOVE = move_make(token[0] + token[1] * 8 - 489, token[2] + token[3] * 8 - 489, token.size() > 4 ? token[4] % 35 * 5 % 6 : 0),
                VISITED[VISITED_COUNT++] = BOARD.hash,
                VISITED_COUNT *= BOARD.board[move_to(BEST_MOVE)] > BLACK_KING && BOARD.board[move_from(BEST_MOVE)] > BLACK_PAWN,
                BOARD.make(BEST_MOVE);
        }
        else if (token[0] == 'g') {
            u64 time = 1ull << 32, inc = 0;
            bool is_movetime = false;
            while (tokens >> token) {
                if (token == "wtime" && BOARD.stm == WHITE) tokens >> time;
                if (token == "btime" && BOARD.stm == BLACK) tokens >> time;
                if (token == "winc" && BOARD.stm == WHITE) tokens >> inc;
                if (token == "binc" && BOARD.stm == BLACK) tokens >> inc;
                if (token == "movetime") { tokens >> time; inc = 0; is_movetime = true; }
            }

            STOP = FALSE;
            TIME_START = now();

            if (is_movetime) {
                TIME_SOFT = time > 10 ? time - 10 : time;
                TIME_LIMIT = TIME_START + TIME_SOFT;
            } else {
                u64 safe_time = time > 20 ? time - 20 : time;
                u64 move_time = safe_time / 22 + inc * 3 / 4;
                TIME_SOFT = min(move_time, safe_time / 3);
                TIME_LIMIT = TIME_START + min(safe_time * 3 / 5, max(TIME_SOFT * 3, safe_time / 4));
            }

            MAIN_THREAD.start(0);
        }
        else if (token[0] == 'q')
            break;
    }

    free(TTABLE);
}
