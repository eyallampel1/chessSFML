#ifndef BOARD_H
#define BOARD_H

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <string>
#include <vector>
#include <memory>
#include "Coordinate.h"

enum class PieceType {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    NONE = 6
};

enum class PieceColor {
    WHITE = 0,
    BLACK = 1,
    NONE = 2
};

struct ChessPiece {
    PieceType type;
    PieceColor color;
    std::string position;  // e.g., "A2", "E4"
    bool isActive;         // false if piece is captured

    ChessPiece(PieceType t, PieceColor c, std::string pos)
        : type(t), color(c), position(pos), isActive(true) {}
};

class Board {
private:
    sf::RenderWindow* window;
    sf::Texture chessBoardTexture;
    sf::Sprite chessBoardSprite;
    sf::Texture chessPiecesTexture;
    sf::Sprite chessPiecesSprite;

    std::vector<ChessPiece> pieces;
    Coordinate spriteCoordinate;

    sf::IntRect rectSprite;
    sf::Vector2u imageCount;

    ChessPiece* selectedPiece;
    std::string clickedSquare;

    enum State { INITIAL, PIECE_CLICKED, PIECE_RELEASED };
    State currentState;

    void loadTextures();
    void initializePieces();
    void renderBoard();
    void renderPieces();
    void renderPiece(const ChessPiece& piece, int x, int y);
    ChessPiece* getPieceAt(const std::string& position);
    bool isMoveLegal(const ChessPiece& piece, const std::string& from, const std::string& to);

public:
    Board(sf::RenderWindow* window);
    ~Board();

    void render();
    void handleClick(const std::string& square);
    void handleRelease(const std::string& square);
    void setState(State state);
};

#endif /* BOARD_H */
