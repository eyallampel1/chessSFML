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
    bool setFEN(const std::string& fen);

    // Every legal move for the side to move. Used for SAN disambiguation and
    // for telling check from checkmate.
    std::vector<Move> legalMoves() const;

    // "e2e4" / "e7e8q" -> the matching legal move.
    bool decodeUci(const std::string& uci, Move& move) const;

    // The move written the way a human reads it: "Nf3", "exd5", "O-O",
    // "Rae1", "e8=Q+", "Qxf7#".
    std::string toSan(const Move& move) const;

    bool isInCheck(PieceColor color) const;

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

// Renders an engine principal variation the way an analysis board does:
// "12.Nf3 d6 13.d4 cxd4" - real SAN, numbered from the position the search
// started in, and starting with "12..." when it is Black to move.
std::string formatPvSan(const std::string& startFen,
                        const std::vector<std::string>& uciMoves,
                        std::size_t maxPlies);

} // namespace analysis
