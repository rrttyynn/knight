#include "search.cpp"

static Thread MAIN_THREAD;

u64 perft(Board& board, i32 depth) {
    if (depth <= 0) return 1;
    u64 nodes = 0;
    i32 moves[MAX_MOVE];
    i32 count = board.movegen(moves, TRUE);
    for (i32 i = 0; i < count; i++) {
        Board child = board;
        if (!child.make(moves[i])) nodes += perft(child, depth - 1);
    }
    return nodes;
}

void initialize() {
    mt19937_64 rng;
    for (i32 i = 0; i < 832; i++)
        KEYS[i / 64][i % 64] = rng(),
        DIAG[0][i % 64 ^ 56] = BSWAP(DIAG[1][i % 64] = ray(1ull << i % 64, 0, se) | ray(1ull << i % 64, 0, nw));
    for (i32 d = 1; d < 64; d++)
        for (i32 l = 1; l < 64; l++)
            LMR_TABLE[d][l] = log(d) * log(l) * 0.43 + 0.83;
    TTABLE = (TTEntry*)calloc(1ull << TT_BITS, sizeof(TTEntry));
    BOARD.startpos();
}

i32 main(i32 argc, char *argv[]) {
    initialize();
    if (argc > 2 && string(argv[1]) == "perft") {
        if (argc > 3) { stringstream fen(argv[3]); BOARD.from_fen(fen); }
        u64 t1 = now(), n = perft(BOARD, stoi(argv[2]));
        osyncstream(cout) << n << " nodes " << n * 1000 / max(now() - t1, u64(1)) << " nps" << endl;
        free(TTABLE);
        return 0;
    }
    thread worker;
    auto stop = [&] { STOP = TRUE; if (worker.joinable()) worker.join(); };
    string line, cmd, token;
    while (getline(cin, line)) {
        stringstream tokens(line);
        if (!(tokens >> cmd)) continue;
        if (cmd == "uci")
            osyncstream(cout) << "id name K8E\nid author AI\noption name Hash type spin default 8 min 1 max 64\noption name Threads type spin default 1 min 1 max 1\nuciok" << endl;
        else if (cmd == "isready") osyncstream(cout) << "readyok" << endl;
        else if (cmd == "stop") stop();
        else if (cmd == "quit") break;
        else if (cmd == "ucinewgame") {
            stop();
            memset(TTABLE, 0, sizeof(TTEntry) * (1ull << TT_BITS));
            memset(&MAIN_THREAD, 0, sizeof(Thread));
            memset(EVAL_CACHE, 0, sizeof(EVAL_CACHE));
            VISITED.clear();
            BOARD.startpos();
        } else if (cmd == "setoption") {
            stop();
            tokens >> token >> token;
            i32 size;
            if (token == "Hash" && tokens >> token >> size) {
                TT_BITS = 17 + bit_width(uint32_t(clamp(size, 1, 64))) - 1;
                TT_SHIFT = 64 - TT_BITS;
                free(TTABLE);
                TTABLE = (TTEntry*)calloc(1ull << TT_BITS, sizeof(TTEntry));
            }
        } else if (cmd == "position") {
            stop();
            VISITED.clear();
            BOARD.startpos();
            tokens >> token;
            if (token == "fen") BOARD.from_fen(tokens);
            tokens >> token;
            while (tokens >> token) {
                i32 moves[MAX_MOVE], count = BOARD.movegen(moves, TRUE);
                for (i32 i = 0; i < count; i++) {
                    if (move_text(moves[i]) != token) continue;
                    Board child = BOARD;
                    if (child.make(moves[i])) break;
                    VISITED.push_back(BOARD.hash);
                    BOARD = child;
                    if (!BOARD.halfmove) VISITED.clear();
                    break;
                }
            }
        } else if (cmd == "go") {
            stop();
            u64 time = 1ull << 40, inc = 0, movetime = 0;
            bool fixed = false, infinite = false;
            NODE_LIMIT = ~0ull;
            DEPTH_LIMIT = 255;
            while (tokens >> token) {
                if (token == (BOARD.stm ? "btime" : "wtime")) tokens >> time;
                else if (token == (BOARD.stm ? "binc" : "winc")) tokens >> inc;
                else if (token == "movetime") { tokens >> movetime; fixed = true; }
                else if (token == "depth") { tokens >> DEPTH_LIMIT; DEPTH_LIMIT = clamp(DEPTH_LIMIT, 1, 255); }
                else if (token == "nodes") tokens >> NODE_LIMIT;
                else if (token == "infinite") infinite = true;
            }
            TIME_START = now();
            u64 safe = time > 20 ? time - 20 : time;
            TIME_SOFT = min(safe / 22 + inc * 3 / 4, safe / 3);
            u64 hard = min(safe * 3 / 5, max(TIME_SOFT * 3, safe / 4));
            if (fixed) hard = TIME_SOFT = movetime > 10 ? movetime - 10 : movetime;
            if (infinite) hard = TIME_SOFT = 1ull << 60;
            TIME_LIMIT = TIME_START + hard;
            STOP = FALSE;
            worker = thread([] { MAIN_THREAD.start(0); });
        }
    }
    stop();
    free(TTABLE);
    return 0;
}
