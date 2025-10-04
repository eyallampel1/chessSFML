#include "Board.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Window/Mouse.hpp>
#include <iostream>
#include <cctype>
#include <sstream>

Board::Board(sf::RenderWindow* window)
    : window(window), selectedPiece(nullptr), currentState(INITIAL), hoveredSquare(""), currentTurn(PieceColor::WHITE), isCheck(false),
      whiteKingsideCastle(true), whiteQueensideCastle(true), blackKingsideCastle(true), blackQueensideCastle(true) {
    loadTextures();
    initializePieces();

    // Load sound effects
    if (moveSoundBuffer.loadFromFile("src/assets/sounds/Move.ogg")) {
        moveSound.setBuffer(moveSoundBuffer);
        moveSound.setVolume(50);  // 50% volume
    } else {
        std::cout << "Warning: Could not load move sound" << std::endl;
    }
    if (captureSoundBuffer.loadFromFile("src/assets/sounds/Capture.ogg")) {
        captureSound.setBuffer(captureSoundBuffer);
        captureSound.setVolume(50);  // 50% volume
    } else {
        std::cout << "Warning: Could not load capture sound" << std::endl;
    }
}

Board::~Board() {
}

void Board::reset() {
    // Reset board state to initial game
    selectedPiece = nullptr;
    clickedSquare.clear();
    hoveredSquare.clear();
    currentTurn = PieceColor::WHITE;
    isCheck = false;
    whiteKingsideCastle = whiteQueensideCastle = true;
    blackKingsideCastle = blackQueensideCastle = true;
    enPassantTarget = "-";
    checkmateFlag = false;
    stalemateFlag = false;
    moveHistory.clear();

    initializePieces();
}

static PieceType pieceTypeFromChar(char c) {
    switch (std::tolower(c)) {
        case 'p': return PieceType::PAWN;
        case 'n': return PieceType::KNIGHT;
        case 'b': return PieceType::BISHOP;
        case 'r': return PieceType::ROOK;
        case 'q': return PieceType::QUEEN;
        case 'k': return PieceType::KING;
        default:  return PieceType::NONE;
    }
}

bool Board::setFEN(const std::string& fen) {
    // Expect: piece placement / active color / castling / en passant / halfmove / fullmove
    std::istringstream iss(fen);
    std::string placement, active, castling, ep, half, full;
    if (!(iss >> placement >> active >> castling >> ep >> half >> full)) {
        std::cout << "Invalid FEN: " << fen << std::endl;
        return false;
    }

    // Reset state
    pieces.clear();
    moveHistory.clear();
    selectedPiece = nullptr;
    clickedSquare.clear();
    hoveredSquare.clear();
    checkmateFlag = false;
    stalemateFlag = false;
    isCheck = false;

    // Piece placement
    int rank = 8;
    int fileIndex = 0; // 0..7 for A..H
    for (size_t i = 0; i < placement.size(); ++i) {
        char c = placement[i];
        if (c == '/') {
            rank--;
            fileIndex = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            fileIndex += c - '0';
            continue;
        }
        if (fileIndex > 7 || rank < 1) {
            std::cout << "Invalid FEN board placement bounds" << std::endl;
            return false;
        }
        PieceType pt = pieceTypeFromChar(c);
        PieceColor pc = std::isupper(static_cast<unsigned char>(c)) ? PieceColor::WHITE : PieceColor::BLACK;
        char fileChar = static_cast<char>('A' + fileIndex);
        std::string pos = std::string(1, fileChar) + std::to_string(rank);
        if (pt != PieceType::NONE) {
            pieces.emplace_back(pt, pc, pos);
        }
        fileIndex++;
    }

    // Active color
    currentTurn = (active == "w") ? PieceColor::WHITE : PieceColor::BLACK;

    // Castling rights
    whiteKingsideCastle  = castling.find('K') != std::string::npos;
    whiteQueensideCastle = castling.find('Q') != std::string::npos;
    blackKingsideCastle  = castling.find('k') != std::string::npos;
    blackQueensideCastle = castling.find('q') != std::string::npos;

    // En passant
    if (ep == "-") enPassantTarget = "-";
    else {
        // Convert to uppercase file
        if (ep.size() >= 2) {
            std::string upp = ep;
            upp[0] = std::toupper(static_cast<unsigned char>(upp[0]));
            enPassantTarget = upp;
        } else {
            enPassantTarget = "-";
        }
    }

    return true;
}

std::string Board::getLastMoveUCI() const {
    if (moveHistory.empty()) return std::string();
    const auto& r = moveHistory.back();
    if (r.from.size() < 2 || r.to.size() < 2) return std::string();
    std::string from = r.from; std::string to = r.to;
    from[0] = std::tolower(static_cast<unsigned char>(from[0]));
    to[0] = std::tolower(static_cast<unsigned char>(to[0]));
    std::string uci = from + to;
    if (r.wasPromotion) {
        char promo = 'q';
        switch (r.promotionType) {
            case PieceType::QUEEN: promo = 'q'; break;
            case PieceType::ROOK: promo = 'r'; break;
            case PieceType::BISHOP: promo = 'b'; break;
            case PieceType::KNIGHT: promo = 'n'; break;
            default: promo = 'q'; break;
        }
        uci.push_back(promo);
    }
    return uci;
}

bool Board::applyUCIMove(const std::string& uci) {
    if (uci.size() < 4) return false;
    std::string from;
    from.push_back(std::toupper(static_cast<unsigned char>(uci[0])));
    from.push_back(uci[1]);
    std::string to;
    to.push_back(std::toupper(static_cast<unsigned char>(uci[2])));
    to.push_back(uci[3]);
    size_t prev = moveHistory.size();
    handleClick(from);
    handleRelease(to);
    bool ok = moveHistory.size() > prev;
    if (ok && uci.size() >= 5) {
        char pc = std::tolower(static_cast<unsigned char>(uci[4]));
        PieceType pt = PieceType::QUEEN;
        if (pc == 'q') pt = PieceType::QUEEN;
        else if (pc == 'r') pt = PieceType::ROOK;
        else if (pc == 'b') pt = PieceType::BISHOP;
        else if (pc == 'n') pt = PieceType::KNIGHT;
        setLastMovePromotion(pt);
    }
    return ok;
}

void Board::loadTextures() {
    // Load chess board
    if (!chessBoardTexture.loadFromFile("src/assets/chess_board.png")) {
        chessBoardTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/chess_board.png");
    }
    chessBoardSprite.setTexture(chessBoardTexture);

    // Load white pieces
    if (!whitePawnTexture.loadFromFile("src/assets/white_pawn.png")) {
        whitePawnTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/white_pawn.png");
    }
    if (!whiteKnightTexture.loadFromFile("src/assets/white_knight.png")) {
        whiteKnightTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/white_knight.png");
    }
    if (!whiteBishopTexture.loadFromFile("src/assets/white_bishof.png")) {
        whiteBishopTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/white_bishof.png");
    }
    if (!whiteRookTexture.loadFromFile("src/assets/white_rook.png")) {
        whiteRookTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/white_rook.png");
    }
    if (!whiteQueenTexture.loadFromFile("src/assets/white_queen.png")) {
        whiteQueenTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/white_queen.png");
    }
    if (!whiteKingTexture.loadFromFile("src/assets/white_king.png")) {
        whiteKingTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/white_king.png");
    }

    // Load black pieces
    if (!blackPawnTexture.loadFromFile("src/assets/black_pawn.png")) {
        blackPawnTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/black_pawn.png");
    }
    if (!blackKnightTexture.loadFromFile("src/assets/black_knight.png")) {
        blackKnightTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/black_knight.png");
    }
    if (!blackBishopTexture.loadFromFile("src/assets/black_bishop.png")) {
        blackBishopTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/black_bishop.png");
    }
    if (!blackRookTexture.loadFromFile("src/assets/black_rook.png")) {
        blackRookTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/black_rook.png");
    }
    if (!blackQueenTexture.loadFromFile("src/assets/black_queen.png")) {
        blackQueenTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/black_queen.png");
    }
    if (!blackKingTexture.loadFromFile("src/assets/black_king.png")) {
        blackKingTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/black_king.png");
    }
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

    // Render hover highlight when dragging a piece
    if (currentState == PIECE_CLICKED && !hoveredSquare.empty()) {
        int x = spriteCoordinate.getX(hoveredSquare);
        int y = spriteCoordinate.getY(hoveredSquare);

        // Draw a semi-transparent white outline around the hovered square
        sf::RectangleShape highlight(sf::Vector2f(44, 44));
        highlight.setPosition(x, y);
        highlight.setFillColor(sf::Color::Transparent);
        highlight.setOutlineColor(sf::Color(255, 255, 255, 200));  // White with transparency
        highlight.setOutlineThickness(3);
        window->draw(highlight);
    }
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
    // Select the appropriate texture based on piece type and color
    sf::Texture* texture = nullptr;

    if (piece.color == PieceColor::WHITE) {
        switch (piece.type) {
            case PieceType::PAWN:   texture = &whitePawnTexture; break;
            case PieceType::KNIGHT: texture = &whiteKnightTexture; break;
            case PieceType::BISHOP: texture = &whiteBishopTexture; break;
            case PieceType::ROOK:   texture = &whiteRookTexture; break;
            case PieceType::QUEEN:  texture = &whiteQueenTexture; break;
            case PieceType::KING:   texture = &whiteKingTexture; break;
            default: return;
        }
    } else {
        switch (piece.type) {
            case PieceType::PAWN:   texture = &blackPawnTexture; break;
            case PieceType::KNIGHT: texture = &blackKnightTexture; break;
            case PieceType::BISHOP: texture = &blackBishopTexture; break;
            case PieceType::ROOK:   texture = &blackRookTexture; break;
            case PieceType::QUEEN:  texture = &blackQueenTexture; break;
            case PieceType::KING:   texture = &blackKingTexture; break;
            default: return;
        }
    }

    if (texture) {
        pieceSprite.setTexture(*texture);
        pieceSprite.setPosition(x, y);
        window->draw(pieceSprite);
    }
}

ChessPiece* Board::getPieceAt(const std::string& position) {
    for (auto& piece : pieces) {
        if (piece.isActive && piece.position == position) {
            return &piece;
        }
    }
    return nullptr;
}

bool Board::isPathClear(const std::string& from, const std::string& to) {
    // Check if there are any pieces blocking the path from 'from' to 'to'
    // This assumes the move is already validated as a valid direction (horizontal, vertical, or diagonal)

    if (from == to) return false;

    char startFile = from[0];
    char endFile = to[0];
    int startRank = from[1] - '0';
    int endRank = to[1] - '0';

    // Calculate direction
    int fileStep = 0;
    int rankStep = 0;

    if (endFile > startFile) fileStep = 1;
    else if (endFile < startFile) fileStep = -1;

    if (endRank > startRank) rankStep = 1;
    else if (endRank < startRank) rankStep = -1;

    // Check each square along the path (excluding start and end)
    char currentFile = startFile + fileStep;
    int currentRank = startRank + rankStep;

    while (currentFile != endFile || currentRank != endRank) {
        std::string currentSquare = std::string(1, currentFile) + std::to_string(currentRank);

        if (getPieceAt(currentSquare) != nullptr) {
            return false;  // Path is blocked
        }

        currentFile += fileStep;
        currentRank += rankStep;
    }

    return true;  // Path is clear
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
                if (fileDiff == 1 && direction == 1) {
                    if (targetPiece) return true;
                    // En passant capture to empty square if matches enPassantTarget
                    if (enPassantTarget != "-" && to == enPassantTarget) return true;
                }
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
                if (fileDiff == 1 && direction == 1) {
                    if (targetPiece) return true;
                    // En passant capture to empty square if matches enPassantTarget
                    if (enPassantTarget != "-" && to == enPassantTarget) return true;
                }
            }
            return false;
        }

        case PieceType::ROOK: {
            // Rook moves horizontally or vertically
            if (startRank == endRank || startFile == endFile) {
                return isPathClear(from, to);
            }
            return false;
        }

        case PieceType::KNIGHT: {
            // Knight moves in L-shape and can jump over pieces
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            return (rankDiff == 2 && fileDiff == 1) || (rankDiff == 1 && fileDiff == 2);
        }

        case PieceType::BISHOP: {
            // Bishop moves diagonally
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            if (rankDiff == fileDiff && rankDiff > 0) {
                return isPathClear(from, to);
            }
            return false;
        }

        case PieceType::QUEEN: {
            // Queen moves like rook or bishop
            int rankDiff = abs(endRank - startRank);
            int fileDiff = abs(endFile - startFile);
            bool validDirection = (startRank == endRank || startFile == endFile || rankDiff == fileDiff);
            if (validDirection) {
                return isPathClear(from, to);
            }
            return false;
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
        // Only allow picking up pieces of the current turn
        if (piece->color != currentTurn) {
            std::cout << "It's " << (currentTurn == PieceColor::WHITE ? "white" : "black") << "'s turn!" << std::endl;
            return;
        }

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
        // Check for castling attempt (king moving 2 squares)
        if (selectedPiece->type == PieceType::KING) {
            int fileDiff = abs(square[0] - clickedSquare[0]);
            if (fileDiff == 2) {
                // Castling attempt
                bool kingside = (square[0] > clickedSquare[0]);
                if (canCastle(selectedPiece->color, kingside)) {
                    executeCastle(selectedPiece->color, kingside);
                    moveSound.play();
                    // En passant target invalidated after any legal move
                    enPassantTarget = "-";

                    // Remove castling rights for this color
                    if (selectedPiece->color == PieceColor::WHITE) {
                        whiteKingsideCastle = false;
                        whiteQueensideCastle = false;
                    } else {
                        blackKingsideCastle = false;
                        blackQueensideCastle = false;
                    }

                    // Switch turn
                    currentTurn = (currentTurn == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
                    isCheck = isKingInCheck(currentTurn);
                    if (isCheck) {
                        std::cout << "CHECK! " << (currentTurn == PieceColor::WHITE ? "White" : "Black") << " king is in check!" << std::endl;
                    }
                    std::cout << "It's now " << (currentTurn == PieceColor::WHITE ? "white" : "black") << "'s turn" << std::endl;

                    selectedPiece = nullptr;
                    hoveredSquare = "";
                    currentState = INITIAL;
                    return;
                } else {
                    std::cout << "Cannot castle!" << std::endl;
                    selectedPiece = nullptr;
                    hoveredSquare = "";
                    currentState = INITIAL;
                    return;
                }
            }
        }

        if (isMoveLegal(*selectedPiece, clickedSquare, square)) {
            // Check if this move would leave/put the king in check
            if (wouldMoveCauseCheck(selectedPiece, clickedSquare, square)) {
                std::cout << "Illegal move: Your king would be in check!" << std::endl;
                selectedPiece = nullptr;
                hoveredSquare = "";
                currentState = INITIAL;
                return;
            }

            // Check if capturing (including en passant)
            ChessPiece* capturedPiece = getPieceAt(square);
            bool wasEnPassant = false;
            if (!capturedPiece && selectedPiece->type == PieceType::PAWN && isEnPassantCapture(*selectedPiece, clickedSquare, square)) {
                // En passant capture: captured pawn is behind 'to' square
                char file = square[0];
                int rank = square[1] - '0';
                int capRank = (selectedPiece->color == PieceColor::WHITE) ? (rank - 1) : (rank + 1);
                std::string capSquare = std::string(1, file) + std::to_string(capRank);
                capturedPiece = getPieceAt(capSquare);
                wasEnPassant = (capturedPiece != nullptr);
            }
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

                // Play capture sound
                captureSound.play();
            } else {
                // Play move sound
                moveSound.play();
            }

            // Update castling rights when king or rook moves
            if (selectedPiece->type == PieceType::KING) {
                if (selectedPiece->color == PieceColor::WHITE) {
                    whiteKingsideCastle = false;
                    whiteQueensideCastle = false;
                } else {
                    blackKingsideCastle = false;
                    blackQueensideCastle = false;
                }
            } else if (selectedPiece->type == PieceType::ROOK) {
                if (selectedPiece->color == PieceColor::WHITE) {
                    if (clickedSquare == "H1") whiteKingsideCastle = false;
                    if (clickedSquare == "A1") whiteQueensideCastle = false;
                } else {
                    if (clickedSquare == "H8") blackKingsideCastle = false;
                    if (clickedSquare == "A8") blackQueensideCastle = false;
                }
            }

            // Record move in history
            MoveRecord record;
            record.from = clickedSquare;
            record.to = square;
            record.movedPiece = selectedPiece->type;
            record.movedColor = selectedPiece->color;
            record.capturedPiece = capturedPiece;
            record.wasCastle = false;
            record.wasEnPassant = wasEnPassant;
            record.wasPromotion = false;
            record.promotionType = PieceType::NONE;
            record.wasCheck = isCheck;
            record.whiteKCastle = whiteKingsideCastle;
            record.whiteQCastle = whiteQueensideCastle;
            record.blackKCastle = blackKingsideCastle;
            record.blackQCastle = blackQueensideCastle;
            record.prevEnPassantTarget = enPassantTarget;
            moveHistory.push_back(record);

            // Move the piece
            selectedPiece->position = square;
            std::cout << "Moved to " << square << std::endl;

            // Update en passant target: only set when a pawn moves two squares; otherwise clear
            {
                char sFile = clickedSquare[0];
                int sRank = clickedSquare[1] - '0';
                char tFile = square[0];
                int tRank = square[1] - '0';
                enPassantTarget = "-";
                if (selectedPiece->type == PieceType::PAWN && sFile == tFile && std::abs(tRank - sRank) == 2) {
                    int epRank = (selectedPiece->color == PieceColor::WHITE) ? (sRank + 1) : (sRank - 1);
                    enPassantTarget = std::string(1, tFile) + std::to_string(epRank);
                }
            }

            // Switch turn after successful move
            currentTurn = (currentTurn == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;

            // Check if the opponent is now in check
            isCheck = isKingInCheck(currentTurn);
            if (isCheck) {
                std::cout << "CHECK! " << (currentTurn == PieceColor::WHITE ? "White" : "Black") << " king is in check!" << std::endl;
            }

            // Determine checkmate/stalemate for side to move
            checkmateFlag = false;
            stalemateFlag = false;
            if (!hasAnyLegalMove(currentTurn)) {
                if (isCheck) {
                    checkmateFlag = true;
                    std::cout << "CHECKMATE!" << std::endl;
                } else {
                    stalemateFlag = true;
                    std::cout << "STALEMATE!" << std::endl;
                }
            }

            std::cout << "It's now " << (currentTurn == PieceColor::WHITE ? "white" : "black") << "'s turn" << std::endl;
        } else {
            std::cout << "Illegal move from " << clickedSquare << " to " << square << std::endl;
        }

        selectedPiece = nullptr;
        hoveredSquare = "";
        currentState = INITIAL;
    }
}

bool Board::hasAnyLegalMove(PieceColor color) {
    // Brute-force: try all moves for all pieces of 'color'
    for (auto& piece : pieces) {
        if (!piece.isActive || piece.color != color) continue;
        std::string from = piece.position;
        for (char file = 'A'; file <= 'H'; ++file) {
            for (int rank = 1; rank <= 8; ++rank) {
                std::string to = std::string(1, file) + std::to_string(rank);
                if (from == to) continue;
                if (isMoveLegal(piece, from, to)) {
                    if (!wouldMoveCauseCheck(&piece, from, to)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void Board::handleRightClick() {
    // Cancel the current move if a piece is selected
    if (currentState == PIECE_CLICKED && selectedPiece) {
        std::cout << "Move cancelled - piece returned to " << clickedSquare << std::endl;

        // Reset state
        selectedPiece = nullptr;
        hoveredSquare = "";
        clickedSquare = "";
        currentState = INITIAL;
    }
}

void Board::updateHoveredSquare(const std::string& square) {
    if (currentState == PIECE_CLICKED) {
        hoveredSquare = square;
    }
}

void Board::setState(State state) {
    currentState = state;
}

std::string Board::getFEN() const {
    // FEN format: piece placement / active color / castling / en passant / halfmove / fullmove
    std::string fen;

    // 1. Piece placement (from rank 8 to rank 1)
    for (int rank = 8; rank >= 1; rank--) {
        int emptyCount = 0;
        for (char file = 'A'; file <= 'H'; file++) {
            std::string square = std::string(1, file) + std::to_string(rank);
            ChessPiece* piece = const_cast<Board*>(this)->getPieceAt(square);

            if (piece) {
                if (emptyCount > 0) {
                    fen += std::to_string(emptyCount);
                    emptyCount = 0;
                }

                // Add piece character
                char pieceChar;
                switch (piece->type) {
                    case PieceType::PAWN:   pieceChar = 'p'; break;
                    case PieceType::KNIGHT: pieceChar = 'n'; break;
                    case PieceType::BISHOP: pieceChar = 'b'; break;
                    case PieceType::ROOK:   pieceChar = 'r'; break;
                    case PieceType::QUEEN:  pieceChar = 'q'; break;
                    case PieceType::KING:   pieceChar = 'k'; break;
                    default: pieceChar = '?';
                }

                // White pieces are uppercase
                if (piece->color == PieceColor::WHITE) {
                    pieceChar = toupper(pieceChar);
                }

                fen += pieceChar;
            } else {
                emptyCount++;
            }
        }

        if (emptyCount > 0) {
            fen += std::to_string(emptyCount);
        }

        if (rank > 1) {
            fen += '/';
        }
    }

    // 2. Active color
    fen += ' ';
    fen += (currentTurn == PieceColor::WHITE) ? 'w' : 'b';

    // 3. Castling availability
    fen += ' ';
    std::string castling;
    if (whiteKingsideCastle) castling += 'K';
    if (whiteQueensideCastle) castling += 'Q';
    if (blackKingsideCastle) castling += 'k';
    if (blackQueensideCastle) castling += 'q';
    if (castling.empty()) castling = "-";
    fen += castling;

    // 4. En passant target square
    fen += ' ';
    if (enPassantTarget.empty() || enPassantTarget == "-") {
        fen += "-";
    } else {
        // FEN uses lowercase files and ranks
        std::string ep = enPassantTarget;
        ep[0] = tolower(ep[0]);
        fen += ep;
    }

    // 5. Halfmove clock (not tracked yet, default to 0)
    fen += " 0";

    // 6. Fullmove number (simplified, always 1 for now)
    fen += " 1";

    return fen;
}

std::string Board::findKing(PieceColor color) {
    for (const auto& piece : pieces) {
        if (piece.isActive && piece.type == PieceType::KING && piece.color == color) {
            return piece.position;
        }
    }
    return "";  // Should never happen in a valid game
}

bool Board::isSquareUnderAttack(const std::string& square, PieceColor byColor) {
    // Check if any piece of 'byColor' can attack 'square'
    for (const auto& piece : pieces) {
        if (!piece.isActive || piece.color != byColor) continue;

        // Check if this piece can legally move to the square (ignoring check rules for this test)
        if (isMoveLegal(piece, piece.position, square)) {
            return true;
        }
    }
    return false;
}

bool Board::isKingInCheck(PieceColor kingColor) {
    std::string kingPos = findKing(kingColor);
    if (kingPos.empty()) return false;

    // Check if the king's position is under attack by the opponent
    PieceColor opponentColor = (kingColor == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
    return isSquareUnderAttack(kingPos, opponentColor);
}

bool Board::wouldMoveCauseCheck(ChessPiece* piece, const std::string& from, const std::string& to) {
    // Simulate the move temporarily
    std::string originalPos = piece->position;
    ChessPiece* capturedPiece = getPieceAt(to);
    ChessPiece* epCaptured = nullptr;
    bool capturedWasActive = false;

    // Handle en passant simulation (capture is not on 'to' square)
    if (!capturedPiece && piece->type == PieceType::PAWN && isEnPassantCapture(*piece, from, to)) {
        // Determine the captured pawn square relative to 'to'
        char file = to[0];
        int rank = to[1] - '0';
        int capRank = (piece->color == PieceColor::WHITE) ? (rank - 1) : (rank + 1);
        std::string capSquare = std::string(1, file) + std::to_string(capRank);
        epCaptured = getPieceAt(capSquare);
        if (epCaptured) {
            capturedPiece = epCaptured;
        }
    }

    if (capturedPiece) {
        capturedWasActive = capturedPiece->isActive;
        capturedPiece->isActive = false;
    }

    piece->position = to;

    // Check if this move puts or leaves the current player's king in check
    bool causesCheck = isKingInCheck(piece->color);

    // Restore the board state
    piece->position = originalPos;
    if (capturedPiece) {
        capturedPiece->isActive = capturedWasActive;
    }

    return causesCheck;
}

bool Board::isEnPassantCapture(const ChessPiece& piece, const std::string& from, const std::string& to) const {
    if (enPassantTarget == "-" || piece.type != PieceType::PAWN) return false;
    if (to != enPassantTarget) return false;
    // Must be a diagonal step by 1 rank towards opponent
    int startRank = (int)from.at(1) - '0';
    int endRank = (int)to.at(1) - '0';
    char startFile = from.at(0);
    char endFile = to.at(0);
    int fileDiff = std::abs(endFile - startFile);
    if (piece.color == PieceColor::WHITE) {
        return (endRank - startRank == 1) && (fileDiff == 1);
    } else {
        return (startRank - endRank == 1) && (fileDiff == 1);
    }
}

bool Board::canCastle(PieceColor color, bool kingside) {
    // Check castling rights
    if (color == PieceColor::WHITE) {
        if (kingside && !whiteKingsideCastle) return false;
        if (!kingside && !whiteQueensideCastle) return false;
    } else {
        if (kingside && !blackKingsideCastle) return false;
        if (!kingside && !blackQueensideCastle) return false;
    }

    // Check if king is in check
    if (isKingInCheck(color)) return false;

    // Define squares for castling
    std::string kingStart = (color == PieceColor::WHITE) ? "E1" : "E8";
    std::string kingEnd, rookStart;
    std::vector<std::string> pathSquares;

    if (kingside) {
        kingEnd = (color == PieceColor::WHITE) ? "G1" : "G8";
        rookStart = (color == PieceColor::WHITE) ? "H1" : "H8";
        pathSquares = (color == PieceColor::WHITE) ?
            std::vector<std::string>{"F1", "G1"} : std::vector<std::string>{"F8", "G8"};
    } else {
        kingEnd = (color == PieceColor::WHITE) ? "C1" : "C8";
        rookStart = (color == PieceColor::WHITE) ? "A1" : "A8";
        pathSquares = (color == PieceColor::WHITE) ?
            std::vector<std::string>{"D1", "C1", "B1"} : std::vector<std::string>{"D8", "C8", "B8"};
    }

    // Check path is clear (no pieces between king and rook)
    for (const auto& sq : pathSquares) {
        if (sq != kingEnd && getPieceAt(sq) != nullptr) return false;
    }

    // Check king doesn't pass through or land on attacked square
    PieceColor opponent = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
    std::vector<std::string> kingPath = {kingStart, (kingside ?
        (color == PieceColor::WHITE ? "F1" : "F8") :
        (color == PieceColor::WHITE ? "D1" : "D8")), kingEnd};

    for (const auto& sq : kingPath) {
        if (isSquareUnderAttack(sq, opponent)) return false;
    }

    return true;
}

void Board::executeCastle(PieceColor color, bool kingside) {
    std::string kingStart = (color == PieceColor::WHITE) ? "E1" : "E8";
    std::string kingEnd, rookStart, rookEnd;

    if (kingside) {
        kingEnd = (color == PieceColor::WHITE) ? "G1" : "G8";
        rookStart = (color == PieceColor::WHITE) ? "H1" : "H8";
        rookEnd = (color == PieceColor::WHITE) ? "F1" : "F8";
    } else {
        kingEnd = (color == PieceColor::WHITE) ? "C1" : "C8";
        rookStart = (color == PieceColor::WHITE) ? "A1" : "A8";
        rookEnd = (color == PieceColor::WHITE) ? "D1" : "D8";
    }

    // Move king
    ChessPiece* king = getPieceAt(kingStart);
    if (king) king->position = kingEnd;

    // Move rook
    ChessPiece* rook = getPieceAt(rookStart);
    if (rook) rook->position = rookEnd;

    std::cout << "Castled " << (kingside ? "kingside" : "queenside") << std::endl;
}

void Board::undoLastMove() {
    if (moveHistory.empty()) return;
    MoveRecord rec = moveHistory.back();
    moveHistory.pop_back();

    // Find moved piece at destination
    ChessPiece* moved = getPieceAt(rec.to);
    if (!moved) {
        for (auto& p : pieces) {
            if (p.isActive && p.color == rec.movedColor && p.position == rec.to) { moved = &p; break; }
        }
    }
    if (moved) {
        if (rec.wasPromotion) moved->type = PieceType::PAWN;
        moved->position = rec.from;
    }

    // Restore captured piece, including en passant square
    if (rec.capturedPiece) {
        rec.capturedPiece->isActive = true;
        if (rec.wasEnPassant) {
            char file = rec.to[0];
            int rank = rec.to[1] - '0';
            int capRank = (rec.movedColor == PieceColor::WHITE) ? (rank - 1) : (rank + 1);
            rec.capturedPiece->position = std::string(1, file) + std::to_string(capRank);
        } else {
            rec.capturedPiece->position = rec.to;
        }
    }

    // Restore rights and ep target
    whiteKingsideCastle = rec.whiteKCastle;
    whiteQueensideCastle = rec.whiteQCastle;
    blackKingsideCastle = rec.blackKCastle;
    blackQueensideCastle = rec.blackQCastle;
    enPassantTarget = rec.prevEnPassantTarget;

    // Switch turn back
    currentTurn = (currentTurn == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
    isCheck = isKingInCheck(currentTurn);
    checkmateFlag = false;
    stalemateFlag = false;
}

bool Board::setLastMovePromotion(PieceType promoteTo) {
    if (moveHistory.empty()) return false;
    MoveRecord& rec = moveHistory.back();
    ChessPiece* moved = getPieceAt(rec.to);
    if (!moved) return false;
    if (moved->type != PieceType::PAWN) return false;
    int toRank = rec.to[1] - '0';
    if (!((moved->color == PieceColor::WHITE && toRank == 8) || (moved->color == PieceColor::BLACK && toRank == 1))) return false;
    moved->type = promoteTo;
    rec.wasPromotion = true;
    rec.promotionType = promoteTo;
    return true;
}

PieceType Board::getPieceTypeAt(const std::string& square) const {
    for (const auto& p : pieces) {
        if (p.isActive && p.position == square) return p.type;
    }
    return PieceType::NONE;
}

