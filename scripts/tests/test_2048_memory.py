"""Compile the real game logic on the host; compare against simulated moves."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class GameOverTest(unittest.TestCase):
    def test_game_over_without_allocations_or_board_changes(self):
        root = Path(__file__).resolve().parents[2]
        app = root / "applications/external/2048"
        source = (app / "game_2048.c").read_text()
        types = source[source.index("typedef enum {"):source.index("#define MENU_ITEMS_COUNT")]
        moves = source[source.index("void calculate_move_to_left("):source.index("void add_new_digit(")]
        check = source[source.index("bool is_game_over("):source.index("int32_t game_2048_app(")]
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "array_utils.h"
#define CELLS_COUNT 4
typedef void FuriMutex;
'''
        harness += types + moves
        # Fail compilation if game-over detection starts allocating again.
        harness += "\n#define malloc(...) forbidden_allocation\n" + check
        harness += r'''
static bool reference(GameState* state) {
    for(unsigned i = 0; i < 16; ++i)
        if(state->table[i / 4][i % 4] == 0) return false;
    void (*moves[])(uint8_t [4][4], MoveResult*) = {
        move_left, move_right, move_up, move_down};
    for(unsigned i = 0; i < 4; ++i) {
        uint8_t board[4][4];
        memcpy(board, state->table, sizeof(board));
        MoveResult result = {0};
        moves[i](board, &result);
        if(result.is_table_updated) return false;
    }
    return true;
}
static void verify(GameState* state) {
    GameState before;
    memcpy(&before, state, sizeof(before));
    assert(is_game_over(state) == reference(state));
    assert(memcmp(&before, state, sizeof(before)) == 0);
}
int main(void) {
    GameState state = {0};
    verify(&state);
    // Every binary full board, including both blocked checkerboards.
    for(uint32_t bits = 0; bits < 65536; ++bits) {
        for(unsigned i = 0; i < 16; ++i)
            state.table[i / 4][i % 4] = 1 + ((bits >> i) & 1);
        verify(&state);
    }
    // Mixed values and holes, deterministic for reproducibility.
    uint32_t seed = 42;
    for(unsigned trial = 0; trial < 10000; ++trial) {
        for(unsigned i = 0; i < 16; ++i) {
            seed = seed * 1664525u + 1013904223u;
            state.table[i / 4][i % 4] = (seed >> 24) % 12;
        }
        verify(&state);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "game.c"
            binary = Path(directory) / "game.exe"
            test.write_text(harness)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(app),
                            str(test), str(app / "array_utils.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
