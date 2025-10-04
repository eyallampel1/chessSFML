// Board move history and PGN functions
#include "Board.h"
#include <sstream>
#include <fstream>

// undoLastMove is implemented in Board.cpp with full en passant/promotion handling

std::string Board::getPGN() const {
    std::ostringstream pgn;

    pgn << "[Event \"Chess Game\"]" << std::endl;
    pgn << "[Site \"Local\"]" << std::endl;
    pgn << "[Round \"1\"]" << std::endl;
    pgn << "[White \"Player 1\"]" << std::endl;
    pgn << "[Black \"Player 2\"]" << std::endl;
    pgn << "[Result \"*\"]" << std::endl;
    pgn << std::endl;

    int moveNumber = 1;
    for (size_t i = 0; i < moveHistory.size(); i++) {
        const MoveRecord& move = moveHistory[i];

        if (move.movedColor == PieceColor::WHITE) {
            pgn << moveNumber << ". ";
        }

        // Convert to algebraic notation (simplified)
        std::string pieceSymbol;
        switch (move.movedPiece) {
            case PieceType::KNIGHT: pieceSymbol = "N"; break;
            case PieceType::BISHOP: pieceSymbol = "B"; break;
            case PieceType::ROOK: pieceSymbol = "R"; break;
            case PieceType::QUEEN: pieceSymbol = "Q"; break;
            case PieceType::KING: pieceSymbol = "K"; break;
            default: pieceSymbol = ""; break; // Pawn
        }

        pgn << pieceSymbol << move.to;

        if (move.movedColor == PieceColor::BLACK) {
            pgn << " ";
            moveNumber++;
        } else {
            pgn << " ";
        }
    }

    pgn << "*" << std::endl;
    return pgn.str();
}

void Board::loadPGN(const std::string& pgn) {
    // Reset board to initial position
    pieces.clear();
    moveHistory.clear();
    initializePieces();
    currentTurn = PieceColor::WHITE;
    isCheck = false;

    std::cout << "PGN loading not fully implemented yet. Board reset to initial position." << std::endl;
    // Full PGN parsing would require more complex implementation
}
