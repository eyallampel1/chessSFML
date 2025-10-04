#include "Board.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Mouse.hpp>
#include <iostream>

Board::Board(sf::RenderWindow* window)
    : window(window), selectedPiece(nullptr), currentState(INITIAL) {
    loadTextures();
    initializePieces();
}

Board::~Board() {
}

void Board::loadTextures() {
    // Load chess board
    if (!chessBoardTexture.loadFromFile("src/assets/chess_board.png")) {
        chessBoardTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/chess_board.png");
    }
    chessBoardSprite.setTexture(chessBoardTexture);

    // Load chess pieces sprite sheet
    if (!chessPiecesTexture.loadFromFile("src/assets/chess_sprite.png")) {
        chessPiecesTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/chess_sprite.png");
    }

    // Sprite sheet: 6 columns (pawn, knight, bishop, rook, queen, king), 1 row (white only)
    imageCount.x = 6;
    imageCount.y = 1;

    rectSprite.width = chessPiecesTexture.getSize().x / float(imageCount.x);
    rectSprite.height = chessPiecesTexture.getSize().y / float(imageCount.y);
}

void Board::initializePieces() {
    pieces.clear();

    // White pieces (row 1 and 2)
    // Row 1: Rook, Knight, Bishop, Queen, King, Bishop, Knight, Rook
    pieces.push_back(ChessPiece(PieceType::ROOK, PieceColor::WHITE, "A1"));
    pieces.push_back(ChessPiece(PieceType::KNIGHT, PieceColor::WHITE, "B1"));
    pieces.push_back(ChessPiece(PieceType::BISHOP, PieceColor::WHITE, "C1"));
    pieces.push_back(ChessPiece(PieceType::QUEEN, PieceColor::WHITE, "D1"));
    pieces.push_back(ChessPiece(PieceType::KING, PieceColor::WHITE, "E1"));
    pieces.push_back(ChessPiece(PieceType::BISHOP, PieceColor::WHITE, "F1"));
    pieces.push_back(ChessPiece(PieceType::KNIGHT, PieceColor::WHITE, "G1"));
    pieces.push_back(ChessPiece(PieceType::ROOK, PieceColor::WHITE, "H1"));

    // Row 2: All pawns
    for (char file = 'A'; file <= 'H'; file++) {
        pieces.push_back(ChessPiece(PieceType::PAWN, PieceColor::WHITE, std::string(1, file) + "2"));
    }

    // Black pieces (row 7 and 8)
    // Row 7: All pawns
    for (char file = 'A'; file <= 'H'; file++) {
        pieces.push_back(ChessPiece(PieceType::PAWN, PieceColor::BLACK, std::string(1, file) + "7"));
    }

    // Row 8: Rook, Knight, Bishop, Queen, King, Bishop, Knight, Rook
    pieces.push_back(ChessPiece(PieceType::ROOK, PieceColor::BLACK, "A8"));
    pieces.push_back(ChessPiece(PieceType::KNIGHT, PieceColor::BLACK, "B8"));
    pieces.push_back(ChessPiece(PieceType::BISHOP, PieceColor::BLACK, "C8"));
    pieces.push_back(ChessPiece(PieceType::QUEEN, PieceColor::BLACK, "D8"));
    pieces.push_back(ChessPiece(PieceType::KING, PieceColor::BLACK, "E8"));
    pieces.push_back(ChessPiece(PieceType::BISHOP, PieceColor::BLACK, "F8"));
    pieces.push_back(ChessPiece(PieceType::KNIGHT, PieceColor::BLACK, "G8"));
    pieces.push_back(ChessPiece(PieceType::ROOK, PieceColor::BLACK, "H8"));
}

void Board::render() {
    renderBoard();
    renderPieces();
}

void Board::renderBoard() {
    window->draw(chessBoardSprite);
}

void Board::renderPieces() {
    for (const auto& piece : pieces) {
        if (!piece.isActive) continue;

        int x = spriteCoordinate.getX(piece.position);
        int y = spriteCoordinate.getY(piece.position);

        // If this piece is being dragged, attach it to mouse
        if (currentState == PIECE_CLICKED && selectedPiece == &piece) {
            sf::Vector2i mousePos = sf::Mouse::getPosition(*window);
            x = mousePos.x - 22;
            y = mousePos.y - 22;
        }

        renderPiece(piece, x, y);
    }
}

void Board::renderPiece(const ChessPiece& piece, int x, int y) {
    // Set the texture rectangle based on piece type
    // The sprite sheet only has white pieces in one row
    int col = static_cast<int>(piece.type);
    int row = 0;  // Only one row in the sprite sheet

    rectSprite.left = col * rectSprite.width;
    rectSprite.top = row * rectSprite.height;

    chessPiecesSprite.setTextureRect(rectSprite);
    chessPiecesSprite.setTexture(chessPiecesTexture);
    chessPiecesSprite.setPosition(x, y);

    // Color black pieces darker
    if (piece.color == PieceColor::BLACK) {
        chessPiecesSprite.setColor(sf::Color(80, 80, 80));  // Dark gray for black pieces
    } else {
        chessPiecesSprite.setColor(sf::Color::White);  // Normal color for white pieces
    }

    window->draw(chessPiecesSprite);
}

ChessPiece* Board::getPieceAt(const std::string& position) {
    for (auto& piece : pieces) {
        if (piece.isActive && piece.position == position) {
            return &piece;
        }
    }
    return nullptr;
}

bool Board::isMoveLegal(const ChessPiece& piece, const std::string& from, const std::string& to) {
    // Basic validation
    if (from == to) return false;
    if (from.empty() || to.empty()) return false;

    int startRank = (int)from.at(1) - '0';
    int endRank = (int)to.at(1) - '0';
    char startFile = from.at(0);
    char endFile = to.at(0);

    // Check if moving to a square with same color piece
    ChessPiece* targetPiece = getPieceAt(to);
    if (targetPiece && targetPiece->color == piece.color) {
        return false;
    }

    // Piece-specific move validation
    switch (piece.type) {
        case PieceType::PAWN: {
            if (piece.color == PieceColor::WHITE) {
                // White pawns move up (increasing rank)
                int direction = endRank - startRank;
                int fileDiff = abs(endFile - startFile);

                // Forward move
                if (fileDiff == 0 && !targetPiece) {
                    if (startRank == 2 && direction <= 2 && direction > 0) return true;
                    if (direction == 1) return true;
                }
                // Diagonal capture
                if (fileDiff == 1 && direction == 1 && targetPiece) return true;
            } else {
                // Black pawns move down (decreasing rank)
                int direction = startRank - endRank;
                int fileDiff = abs(endFile - startFile);

                // Forward move
                if (fileDiff == 0 && !targetPiece) {
                    if (startRank == 7 && direction <= 2 && direction > 0) return true;
                    if (direction == 1) return true;
                }
                // Diagonal capture
                if (fileDiff == 1 && direction == 1 && targetPiece) return true;
            }
            return false;
        }

        case PieceType::ROOK: {
            // Rook moves horizontally or vertically
            return (startRank == endRank || startFile == endFile);
        }

        case PieceType::KNIGHT: {
            // Knight moves in L-shape
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            return (rankDiff == 2 && fileDiff == 1) || (rankDiff == 1 && fileDiff == 2);
        }

        case PieceType::BISHOP: {
            // Bishop moves diagonally
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            return rankDiff == fileDiff;
        }

        case PieceType::QUEEN: {
            // Queen moves like rook or bishop
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            return (startRank == endRank || startFile == endFile || rankDiff == fileDiff);
        }

        case PieceType::KING: {
            // King moves one square in any direction
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            return rankDiff <= 1 && fileDiff <= 1;
        }

        default:
            return false;
    }
}

void Board::handleClick(const std::string& square) {
    ChessPiece* piece = getPieceAt(square);
    if (piece) {
        selectedPiece = piece;
        clickedSquare = square;
        currentState = PIECE_CLICKED;
        std::cout << "Picked up " << (piece->color == PieceColor::WHITE ? "white " : "black ");

        switch (piece->type) {
            case PieceType::PAWN: std::cout << "pawn"; break;
            case PieceType::KNIGHT: std::cout << "knight"; break;
            case PieceType::BISHOP: std::cout << "bishop"; break;
            case PieceType::ROOK: std::cout << "rook"; break;
            case PieceType::QUEEN: std::cout << "queen"; break;
            case PieceType::KING: std::cout << "king"; break;
            default: break;
        }
        std::cout << " from " << square << std::endl;
    }
}

void Board::handleRelease(const std::string& square) {
    if (currentState == PIECE_CLICKED && selectedPiece) {
        if (isMoveLegal(*selectedPiece, clickedSquare, square)) {
            // Check if capturing
            ChessPiece* capturedPiece = getPieceAt(square);
            if (capturedPiece) {
                std::cout << "Captured " << (capturedPiece->color == PieceColor::WHITE ? "white " : "black ");
                switch (capturedPiece->type) {
                    case PieceType::PAWN: std::cout << "pawn"; break;
                    case PieceType::KNIGHT: std::cout << "knight"; break;
                    case PieceType::BISHOP: std::cout << "bishop"; break;
                    case PieceType::ROOK: std::cout << "rook"; break;
                    case PieceType::QUEEN: std::cout << "queen"; break;
                    case PieceType::KING: std::cout << "king"; break;
                    default: break;
                }
                std::cout << " at " << square << std::endl;
                capturedPiece->isActive = false;
            }

            // Move the piece
            selectedPiece->position = square;
            std::cout << "Moved to " << square << std::endl;
        } else {
            std::cout << "Illegal move from " << clickedSquare << " to " << square << std::endl;
        }

        selectedPiece = nullptr;
        currentState = INITIAL;
    }
}

void Board::setState(State state) {
    currentState = state;
}
