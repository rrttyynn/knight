#define main engine_main
#include "../src/main.cpp"
#undef main

void require(bool condition, const char* what) {
    if (!condition) { cerr << what << endl; exit(1); }
}

int main() {
    initialize();
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
    cout << "core: attack rays, evaluation/SEE immutability and incremental hashes passed" << endl;
}
