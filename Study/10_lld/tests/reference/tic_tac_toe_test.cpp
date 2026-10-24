// Exercise 1 from 01_machine_coding_problems/01_tic_tac_toe.md, turned into
// real assertions. Run: ctest -R TicTacToe --output-on-failure
#include "reference/tic_tac_toe.hpp"

#include <gtest/gtest.h>

using namespace lld::tic_tac_toe;

namespace {

Game makeGame() {
    std::vector<Player> players{{"X", 'X'}, {"O", 'O'}};
    return Game(std::move(players), std::make_unique<RowColDiagonalStrategy>());
}

}  // namespace

TEST(TicTacToe, WinsViaRow) {
    Game game = makeGame();
    EXPECT_EQ(game.makeMove(0, 0, 0), "");
    EXPECT_EQ(game.makeMove(1, 1, 0), "");
    EXPECT_EQ(game.makeMove(0, 0, 1), "");
    EXPECT_EQ(game.makeMove(1, 1, 1), "");
    EXPECT_EQ(game.makeMove(0, 0, 2), "X");   // X completes row 0
}

TEST(TicTacToe, WinsViaDiagonal) {
    Game game = makeGame();
    EXPECT_EQ(game.makeMove(0, 0, 0), "");
    EXPECT_EQ(game.makeMove(1, 0, 1), "");
    EXPECT_EQ(game.makeMove(0, 1, 1), "");
    EXPECT_EQ(game.makeMove(1, 0, 2), "");
    EXPECT_EQ(game.makeMove(0, 2, 2), "X");   // X completes the main diagonal
}

TEST(TicTacToe, DetectsDraw) {
    Game game = makeGame();
    // X O X
    // X O O
    // O X X
    struct Move { int player, row, col; };
    std::vector<Move> moves = {
        {0, 0, 0}, {1, 0, 1}, {0, 0, 2},
        {1, 1, 1}, {0, 1, 0}, {1, 1, 2},
        {0, 2, 1}, {1, 2, 0}, {0, 2, 2},
    };
    std::string result;
    for (auto& m : moves) result = game.makeMove(m.player, m.row, m.col);
    EXPECT_EQ(result, "DRAW");
}

TEST(TicTacToe, RejectsOccupiedCell) {
    Game game = makeGame();
    EXPECT_EQ(game.makeMove(0, 0, 0), "");
    EXPECT_THROW(game.makeMove(1, 0, 0), std::invalid_argument);
}
