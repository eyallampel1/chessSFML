#ifndef BOARD_H
#define BOARD_H

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Audio/Sound.hpp>
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

    // Individual piece textures
    sf::Texture whitePawnTexture, whiteKnightTexture, whiteBishopTexture;
    sf::Texture whiteRookTexture, whiteQueenTexture, whiteKingTexture;
    sf::Texture blackPawnTexture, blackKnightTexture, blackBishopTexture;
    sf::Texture blackRookTexture, blackQueenTexture, blackKingTexture;

    sf::Sprite pieceSprite;

    std::vector<ChessPiece> pieces;
    Coordinate spriteCoordinate;

    // Sound effects
    sf::SoundBuffer moveSoundBuffer;
    sf::SoundBuffer captureSoundBuffer;
    sf::Sound moveSound;
    sf::Sound captureSound;

    ChessPiece* selectedPiece;
    std::string clickedSquare;
    std::string hoveredSquare;
    PieceColor currentTurn;
    bool isCheck;

    // Castling rights
    bool whiteKingsideCastle;
    bool whiteQueensideCastle;
    bool blackKingsideCastle;
    bool blackQueensideCastle;

    enum State { INITIAL, PIECE_CLICKED, PIECE_RELEASED };
    State currentState;

    void loadTextures();
    void initializePieces();
    void renderBoard();
    void renderPieces();
    void renderPiece(const ChessPiece& piece, int x, int y);
    ChessPiece* getPieceAt(const std::string& position);
    bool isMoveLegal(const ChessPiece& piece, const std::string& from, const std::string& to);
    bool isPathClear(const std::string& from, const std::string& to);
    bool isSquareUnderAttack(const std::string& square, PieceColor byColor);
    std::string findKing(PieceColor color);
    bool isKingInCheck(PieceColor kingColor);
    bool wouldMoveCauseCheck(ChessPiece* piece, const std::string& from, const std::string& to);
    bool canCastle(PieceColor color, bool kingside);
    void executeCastle(PieceColor color, bool kingside);

public:
    Board(sf::RenderWindow* window);
    ~Board();

    void render();
    void handleClick(const std::string& square);
    void handleRelease(const std::string& square);
    void updateHoveredSquare(const std::string& square);
    void setState(State state);
    bool getIsCheck() const { return isCheck; }
    PieceColor getCurrentTurn() const { return currentTurn; }
};

#endif /* BOARD_H */
