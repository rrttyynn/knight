// Only this offline binary exposes mutable parameters and feature extraction.
#define main engine_main
#include "tune_engine.cpp"
#undef main

i32 static_score(Board& board) {
    EVAL_CACHE[board.hash & 32767].key = ~board.hash;
    return (board.stm ? -1 : 1) * board.eval();
}

int main(int argc, char** argv) {
    load_tune_profile();
    if (argc == 1) return engine_main(argc, argv);
    bool features = string(argv[1]) == "--features";
    if (!features && string(argv[1]) != "--scores") return 2;
    initialize();
    TIME_LIMIT = NODE_LIMIT = ~0ull;
    for (auto& ptr : MAIN_THREAD.stack_conthist)
        ptr = &MAIN_THREAD.conthist[WHITE_PAWN][B1];
    string fen;
    cout << setprecision(9);
    while (getline(cin, fen)) {
        if (fen.empty()) continue;
        stringstream input(fen);
        Board board;
        board.from_fen(input);
        i32 base = static_score(board);
        cout << base;
        if (features) {
            i32 quiet = (board.stm ? -1 : 1) *
                MAIN_THREAD.search(board, -INF, INF, 0, 0, TRUE);
            cout << ' ' << quiet;
            for (size_t i = 0; i < TUNE_MG.size(); i++)
                for (auto* values : {&TUNE_MG, &TUNE_EG}) {
                    i32 saved = (*values)[i];
                    (*values)[i] = saved + 16;
                    refresh_tune_material();
                    i32 plus = static_score(board);
                    (*values)[i] = saved - 16;
                    refresh_tune_material();
                    i32 minus = static_score(board);
                    (*values)[i] = saved;
                    refresh_tune_material();
                    cout << ' ' << (plus - minus) / 32.0;
                }
        }
        cout << '\n';
    }
    free(TTABLE);
}
