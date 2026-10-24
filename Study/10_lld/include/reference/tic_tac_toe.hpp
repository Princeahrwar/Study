// Reference implementation for 01_machine_coding_problems/01_tic_tac_toe.md
// Faithful to the tutorial's code exactly -- read the .md for the design
// rationale (why WinningStrategy is an interface, why only the affected
// row/col/diagonal are checked). This file is here so the design is
// runnable and testable, not as a substitute for reading the tutorial.
#pragma once

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lld::tic_tac_toe {

class Board {
public:
    static constexpr int SIZE = 3;

    bool placeMark(int row, int col, char symbol) {
        if (row < 0 || row >= SIZE || col < 0 || col >= SIZE) return false;
        if (grid_[row][col] != '\0') return false;   // occupied
        grid_[row][col] = symbol;
        ++filled_;
        return true;
    }
    bool isFull() const { return filled_ == SIZE * SIZE; }
    char at(int row, int col) const { return grid_[row][col]; }

private:
    std::array<std::array<char, SIZE>, SIZE> grid_{};
    int filled_ = 0;
};

struct Player {
    std::string name;
    char symbol;
};

class WinningStrategy {
public:
    virtual bool checkWinner(const Board& b, int lastRow, int lastCol, char symbol) const = 0;
    virtual ~WinningStrategy() = default;
};

class RowColDiagonalStrategy : public WinningStrategy {
public:
    bool checkWinner(const Board& b, int lastRow, int lastCol, char symbol) const override {
        bool rowWin = true, colWin = true;
        for (int i = 0; i < Board::SIZE; ++i) {
            if (b.at(lastRow, i) != symbol) rowWin = false;
            if (b.at(i, lastCol) != symbol) colWin = false;
        }
        if (rowWin || colWin) return true;

        if (lastRow == lastCol) {
            bool diagWin = true;
            for (int i = 0; i < Board::SIZE; ++i)
                if (b.at(i, i) != symbol) diagWin = false;
            if (diagWin) return true;
        }
        if (lastRow + lastCol == Board::SIZE - 1) {
            bool antiDiagWin = true;
            for (int i = 0; i < Board::SIZE; ++i)
                if (b.at(i, Board::SIZE - 1 - i) != symbol) antiDiagWin = false;
            if (antiDiagWin) return true;
        }
        return false;
    }
};

class Game {
public:
    Game(std::vector<Player> players, std::unique_ptr<WinningStrategy> strategy)
        : players_(std::move(players)), strategy_(std::move(strategy)) {}

    // Returns: player name of winner, "DRAW", or "" if game continues.
    std::string makeMove(int playerIdx, int row, int col) {
        const Player& p = players_[playerIdx];
        if (!board_.placeMark(row, col, p.symbol))
            throw std::invalid_argument("illegal move");

        if (strategy_->checkWinner(board_, row, col, p.symbol)) return p.name;
        if (board_.isFull()) return "DRAW";
        return "";
    }

    const Board& board() const { return board_; }

private:
    Board board_;
    std::vector<Player> players_;
    std::unique_ptr<WinningStrategy> strategy_;
};

}  // namespace lld::tic_tac_toe
