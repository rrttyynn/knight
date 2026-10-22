#define main engine_main
#include "../src/main.cpp"
#undef main

void require(bool condition, const char* what) {
    if (!condition) { cerr << what << endl; exit(1); }
}

int main() {
    initialize();
    require(sizeof(TTEntry) == 8, "TT entries must retain the advertised Hash capacity");
    u64 key = (5ull << TT_SHIFT) | 1, collision = key + 1;
    TT_GENERATION = 252;
    tt_save(key, 123, 321, 24, BOUND_LOWER);
    tt_save(collision, 456, -200, 0, BOUND_EXACT);
    require(TTABLE[5].key == i16(key) && TTABLE[5].score == 321,
            "shallow collision displaced a fresh deep TT result");
    tt_save(key, 456, 12, 0, BOUND_UPPER);
    require(TTABLE[5].score == 321 && TTABLE[5].move == 456,
            "same-position move refresh lost the deep TT result");
    tt_save(key, 0, 400, 25, BOUND_EXACT);
    require(TTABLE[5].move == 456 && TTABLE[5].score == 400,
            "TT score refresh discarded the existing move");
    TT_GENERATION = 0;
    tt_save(collision, 789, 55, 0, BOUND_UPPER);
    require(TTABLE[5].key == i16(key), "TT generation wrap treated a recent entry as ancient");
    TT_GENERATION = 24;
    tt_save(collision, 789, 55, 0, BOUND_UPPER);
    require(TTABLE[5].key == i16(collision), "old deep TT entries cannot be replaced");
    TIME_LIMIT = NODE_LIMIT = ~0ull;
    STOP = FALSE;
    tt_save(BOARD.hash, 0, 321, 30, BOUND_LOWER);
    require(MAIN_THREAD.search(BOARD, 0, 100, 0, 1) == 321,
            "TT generation bits corrupted the lower-bound cutoff");
    tt_save(BOARD.hash, 0, INF - 2, 31, BOUND_EXACT);
    require(MAIN_THREAD.search(BOARD, -INF, INF, 6, 1) == INF - 8,
            "positive mate distance changed across TT plies");
    tt_save(BOARD.hash, 0, 2 - INF, 32, BOUND_EXACT);
    require(MAIN_THREAD.search(BOARD, -INF, INF, 6, 1) == 8 - INF,
            "negative mate distance changed across TT plies");
    Board mate;
    stringstream mate_fen("7k/5K2/6Q1/8/8/8/8/8 w - - 0 1");
    mate.from_fen(mate_fen);
    tt_save(mate.hash, 0, INF - 1, 32, BOUND_LOWER);
    u64 before_nodes = MAIN_THREAD.nodes;
    for (auto& ptr : MAIN_THREAD.stack_conthist)
        ptr = &MAIN_THREAD.conthist[WHITE_PAWN][B1];
    require(MAIN_THREAD.search(mate, -INF, INF, 20, 1) == INF - 21
            && MAIN_THREAD.nodes == before_nodes + 1,
            "mate-distance window did not enable the proven TT cutoff");
    memset(TTABLE, 0, sizeof(TTEntry) * (1ull << TT_BITS));
    TT_GENERATION = 0;
    mt19937_64 rng(20261020);
    // Independent directional rays cover blockers at edges and on the source.
    for (int n = 0; n < 10000; n++) {
        u64 occupied = rng();
        int square = rng() % 64;
        for (int type = KNIGHT; type <= KING; type++) {
            u64 expected = 0;
            for (int dr = -2; dr <= 2; dr++)
                for (int df = -2; df <= 2; df++) {
                    if (type == KNIGHT ? abs(dr * df) != 2 :
                        type == KING ? max(abs(dr), abs(df)) != 1 :
                        max(abs(dr), abs(df)) != 1 ||
                        (type == BISHOP && (!dr || !df)) || (type == ROOK && dr && df)) continue;
                    for (int r = square / 8 + dr, f = square % 8 + df;
                         r >= 0 && r < 8 && f >= 0 && f < 8; r += dr, f += df) {
                        u64 bit = 1ull << (r * 8 + f);
                        expected |= bit;
                        if (type == KNIGHT || type == KING || (occupied & bit)) break;
                    }
                }
            require(attack(1ull << square, occupied, type) == expected, "attack table mismatch");
        }
    }
    Board board = BOARD;
    for (int n = 0; n < 3000; n++) {
        Board before = board;
        int eval = board.eval();
        require(!memcmp(&before, &board, sizeof(Board)), "evaluation changed board");
        require(eval == board.eval(), "evaluation cache mismatch");
        int moves[MAX_MOVE], legal[MAX_MOVE], count = board.movegen(moves, TRUE), legals = 0;
        for (int i = 0; i < count; i++) {
            board.see(moves[i], -75);
            require(!memcmp(&before, &board, sizeof(Board)), "SEE changed board");
            Board child = board;
            if (!child.make(moves[i])) legal[legals++] = moves[i];
        }
        if (!legals || n % 100 == 99) { board.startpos(); continue; }
        board.make(legal[rng() % legals]);
        Board rebuilt = board;
        rebuilt.hash = 0;
        rebuilt.hash_corrhist[0] = rebuilt.hash_corrhist[1] = rebuilt.hash_corrhist[2] = 0;
        memset(rebuilt.colors, 0, sizeof(rebuilt.colors));
        memset(rebuilt.pieces, 0, sizeof(rebuilt.pieces));
        memset(rebuilt.board, PIECE_NONE, sizeof(rebuilt.board));
        for (int s = 0; s < 64; s++) rebuilt.edit(s, board.board[s]);
        rebuilt.hash ^= KEYS[PIECE_NONE][board.castled];
        if (board.enpassant < SQUARE_NONE) rebuilt.hash ^= KEYS[PIECE_NONE][board.enpassant];
        if (board.stm) rebuilt.hash ^= KEYS[PIECE_NONE][0];
        require(board.hash == rebuilt.hash, "incremental hash mismatch");
        require(!memcmp(board.hash_corrhist, rebuilt.hash_corrhist, sizeof(board.hash_corrhist)), "correction hash mismatch");
    }
    free(TTABLE);
    cout << "core: TT replacement/mate distance, attack rays, evaluation/SEE immutability and incremental hashes passed" << endl;
}
