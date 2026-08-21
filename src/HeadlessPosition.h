#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace analysis {

enum class PieceColor : std::uint8_t {
    White = 0,
    Black = 1
};

enum class PieceType : std::uint8_t {
    None = 0,
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King
};

struct Move {
    int from = -1;          // 0..63
    int to = -1;            // 0..63
    PieceType moving = PieceType::None;
    PieceType promotion = PieceType::None; // valid for pawn promotions
    bool isCapture = false;
    bool isEnPassant = false;
    bool isCastleKingSide = false;
    bool isCastleQueenSide = false;

    std::string toUci() const;
};

class HeadlessPosition {
public:
    HeadlessPosition();

    void reset();

    PieceColor sideToMove() const { return side; }
    int fullmoveNumber() const { return fullmove; }
    int halfmoveClock() const { return halfmove; }

    bool decodeSan(const std::string& san, Move& move) const;
    bool applyMove(const Move& move);
    std::string getFEN() const;

    static PieceColor opposite(PieceColor color);

private:
    struct Piece {
        PieceType type = PieceType::None;
        PieceColor color = PieceColor::White;
    };

    std::array<Piece, 64> board;
    PieceColor side = PieceColor::White;
    bool whiteCastleK = true;
    bool whiteCastleQ = true;
    bool blackCastleK = true;
    bool blackCastleQ = true;
    int enPassant = -1; // square index 0..63, -1 if none
    int halfmove = 0;
    int fullmove = 1;

    Piece& at(int file, int rank);
    const Piece& at(int file, int rank) const;
    Piece& at(int index);
    const Piece& at(int index) const;

    static bool isEmptyPiece(const Piece& p);
    static bool inBounds(int file, int rank);
    static int index(int file, int rank);
    static int fileOf(int index);
    static int rankOf(int index);
    static char fileChar(int file);
    static char rankChar(int rank);

    static std::string normalizeSan(const std::string& san);

    bool resolveCastling(const std::string& san, Move& move) const;
    bool resolveStandardSan(const std::string& san, Move& move) const;

    bool isSquareAttacked(int file, int rank, PieceColor byColor) const;
    bool isLineAttacked(int file, int rank, int dFile, int dRank, PieceColor byColor) const;
    bool wouldLeaveKingInCheck(const Move& move) const;
    int findKingSquare(PieceColor color) const;

    bool canPieceReach(PieceType type, PieceColor color, int fromFile, int fromRank,
                       int toFile, int toRank, bool captureRequested, bool isPromotion,
                       PieceType promotion) const;
    bool pathClear(int fromFile, int fromRank, int toFile, int toRank) const;

    void updateCastlingRights(const Move& move);
    void updateCastlingRightsForCapture(int targetIndex);
};

} // namespace analysis
