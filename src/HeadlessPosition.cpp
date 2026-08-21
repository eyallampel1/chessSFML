#include "HeadlessPosition.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <cmath>

namespace analysis {

namespace {
PieceType pieceTypeFromChar(char c) {
    switch (c) {
        case 'K': return PieceType::King;
        case 'Q': return PieceType::Queen;
        case 'R': return PieceType::Rook;
        case 'B': return PieceType::Bishop;
        case 'N': return PieceType::Knight;
        default:  return PieceType::None;
    }
}

char charFromPiece(PieceType type, PieceColor color) {
    char base = ' ';
    switch (type) {
        case PieceType::Pawn:   base = 'p'; break;
        case PieceType::Knight: base = 'n'; break;
        case PieceType::Bishop: base = 'b'; break;
        case PieceType::Rook:   base = 'r'; break;
        case PieceType::Queen:  base = 'q'; break;
        case PieceType::King:   base = 'k'; break;
        default: return ' ';
    }
    if (color == PieceColor::White) base = static_cast<char>(std::toupper(static_cast<unsigned char>(base)));
    return base;
}

} // namespace

std::string Move::toUci() const {
    if (from < 0 || to < 0) return {};
    auto file = [](int index) { return index % 8; };
    auto rank = [](int index) { return index / 8; };
    std::string uci;
    uci.push_back(static_cast<char>('a' + file(from)));
    uci.push_back(static_cast<char>('1' + rank(from)));
    uci.push_back(static_cast<char>('a' + file(to)));
    uci.push_back(static_cast<char>('1' + rank(to)));
    if (promotion != PieceType::None) {
        char promo = 'q';
        switch (promotion) {
            case PieceType::Queen:  promo = 'q'; break;
            case PieceType::Rook:   promo = 'r'; break;
            case PieceType::Bishop: promo = 'b'; break;
            case PieceType::Knight: promo = 'n'; break;
            default: break;
        }
        uci.push_back(promo);
    }
    return uci;
}

bool HeadlessPosition::isEmptyPiece(const Piece& p) {
    return p.type == PieceType::None;
}

HeadlessPosition::HeadlessPosition() {
    reset();
}

void HeadlessPosition::reset() {
    board.fill(Piece{});

    // Place white pieces
    for (int file = 0; file < 8; ++file) {
        at(file, 1) = {PieceType::Pawn, PieceColor::White};
    }
    at(0, 0) = {PieceType::Rook, PieceColor::White};
    at(7, 0) = {PieceType::Rook, PieceColor::White};
    at(1, 0) = {PieceType::Knight, PieceColor::White};
    at(6, 0) = {PieceType::Knight, PieceColor::White};
    at(2, 0) = {PieceType::Bishop, PieceColor::White};
    at(5, 0) = {PieceType::Bishop, PieceColor::White};
    at(3, 0) = {PieceType::Queen, PieceColor::White};
    at(4, 0) = {PieceType::King, PieceColor::White};

    // Black pieces
    for (int file = 0; file < 8; ++file) {
        at(file, 6) = {PieceType::Pawn, PieceColor::Black};
    }
    at(0, 7) = {PieceType::Rook, PieceColor::Black};
    at(7, 7) = {PieceType::Rook, PieceColor::Black};
    at(1, 7) = {PieceType::Knight, PieceColor::Black};
    at(6, 7) = {PieceType::Knight, PieceColor::Black};
    at(2, 7) = {PieceType::Bishop, PieceColor::Black};
    at(5, 7) = {PieceType::Bishop, PieceColor::Black};
    at(3, 7) = {PieceType::Queen, PieceColor::Black};
    at(4, 7) = {PieceType::King, PieceColor::Black};

    side = PieceColor::White;
    whiteCastleK = whiteCastleQ = blackCastleK = blackCastleQ = true;
    enPassant = -1;
    halfmove = 0;
    fullmove = 1;
}

HeadlessPosition::Piece& HeadlessPosition::at(int file, int rank) {
    return board[index(file, rank)];
}

const HeadlessPosition::Piece& HeadlessPosition::at(int file, int rank) const {
    return board[index(file, rank)];
}

HeadlessPosition::Piece& HeadlessPosition::at(int idx) {
    return board[idx];
}

const HeadlessPosition::Piece& HeadlessPosition::at(int idx) const {
    return board[idx];
}

bool HeadlessPosition::inBounds(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

int HeadlessPosition::index(int file, int rank) {
    return rank * 8 + file;
}

int HeadlessPosition::fileOf(int idx) {
    return idx % 8;
}

int HeadlessPosition::rankOf(int idx) {
    return idx / 8;
}

char HeadlessPosition::fileChar(int file) {
    return static_cast<char>('a' + file);
}

char HeadlessPosition::rankChar(int rank) {
    return static_cast<char>('1' + rank);
}

std::string HeadlessPosition::normalizeSan(const std::string& san) {
    std::string trimmed;
    trimmed.reserve(san.size());
    for (char c : san) {
        if (!std::isspace(static_cast<unsigned char>(c))) trimmed.push_back(c);
    }
    while (!trimmed.empty()) {
        char last = trimmed.back();
        if (last == '+' || last == '#' || last == '!' || last == '?') {
            trimmed.pop_back();
        } else {
            break;
        }
    }
    return trimmed;
}

PieceColor HeadlessPosition::opposite(PieceColor color) {
    return (color == PieceColor::White) ? PieceColor::Black : PieceColor::White;
}

bool HeadlessPosition::decodeSan(const std::string& san, Move& move) const {
    std::string cleaned = normalizeSan(san);
    Move candidate;
    if (resolveCastling(cleaned, candidate)) {
        move = candidate;
        return true;
    }
    if (resolveStandardSan(cleaned, candidate)) {
        move = candidate;
        return true;
    }
    return false;
}

bool HeadlessPosition::resolveCastling(const std::string& san, Move& move) const {
    std::string upper = san;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (upper == "O-O" || upper == "0-0") {
        if (side == PieceColor::White) {
            if (!whiteCastleK) return false;
            if (!isEmptyPiece(at(5, 0)) || !isEmptyPiece(at(6, 0))) return false;
            if (isSquareAttacked(4, 0, PieceColor::Black) ||
                isSquareAttacked(5, 0, PieceColor::Black) ||
                isSquareAttacked(6, 0, PieceColor::Black)) {
                return false;
            }
            move.from = index(4, 0);
            move.to = index(6, 0);
        } else {
            if (!blackCastleK) return false;
            if (!isEmptyPiece(at(5, 7)) || !isEmptyPiece(at(6, 7))) return false;
            if (isSquareAttacked(4, 7, PieceColor::White) ||
                isSquareAttacked(5, 7, PieceColor::White) ||
                isSquareAttacked(6, 7, PieceColor::White)) {
                return false;
            }
            move.from = index(4, 7);
            move.to = index(6, 7);
        }
        move.moving = PieceType::King;
        move.isCastleKingSide = true;
        move.isCastleQueenSide = false;
        move.promotion = PieceType::None;
        move.isCapture = false;
        move.isEnPassant = false;
        if (wouldLeaveKingInCheck(move)) return false;
        return true;
    }
    if (upper == "O-O-O" || upper == "0-0-0") {
        if (side == PieceColor::White) {
            if (!whiteCastleQ) return false;
            if (!isEmptyPiece(at(1, 0)) || !isEmptyPiece(at(2, 0)) || !isEmptyPiece(at(3, 0))) return false;
            if (isSquareAttacked(4, 0, PieceColor::Black) ||
                isSquareAttacked(3, 0, PieceColor::Black) ||
                isSquareAttacked(2, 0, PieceColor::Black)) {
                return false;
            }
            move.from = index(4, 0);
            move.to = index(2, 0);
        } else {
            if (!blackCastleQ) return false;
            if (!isEmptyPiece(at(1, 7)) || !isEmptyPiece(at(2, 7)) || !isEmptyPiece(at(3, 7))) return false;
            if (isSquareAttacked(4, 7, PieceColor::White) ||
                isSquareAttacked(3, 7, PieceColor::White) ||
                isSquareAttacked(2, 7, PieceColor::White)) {
                return false;
            }
            move.from = index(4, 7);
            move.to = index(2, 7);
        }
        move.moving = PieceType::King;
        move.isCastleKingSide = false;
        move.isCastleQueenSide = true;
        move.promotion = PieceType::None;
        move.isCapture = false;
        move.isEnPassant = false;
        if (wouldLeaveKingInCheck(move)) return false;
        return true;
    }
    return false;
}

bool HeadlessPosition::resolveStandardSan(const std::string& san, Move& move) const {
    if (san.empty()) return false;

    PieceType pieceType = PieceType::Pawn;
    size_t idx = 0;
    if (std::isupper(static_cast<unsigned char>(san[0])) && san[0] != 'O') {
        pieceType = pieceTypeFromChar(san[0]);
        if (pieceType == PieceType::None) return false;
        idx = 1;
    }

    std::string remainder = san.substr(idx);
    PieceType promotion = PieceType::None;
    size_t eqPos = remainder.find('=');
    if (eqPos != std::string::npos) {
        if (eqPos + 1 >= remainder.size()) return false;
        promotion = pieceTypeFromChar(static_cast<char>(std::toupper(static_cast<unsigned char>(remainder[eqPos + 1]))));
        remainder = remainder.substr(0, eqPos);
    }

    int targetFile = -1;
    int targetRank = -1;
    int targetPairPos = -1;
    for (int i = static_cast<int>(remainder.size()) - 1; i >= 1; --i) {
        char cRank = remainder[static_cast<size_t>(i)];
        char cFile = remainder[static_cast<size_t>(i - 1)];
        if (cRank >= '1' && cRank <= '8' && cFile >= 'a' && cFile <= 'h') {
            targetFile = cFile - 'a';
            targetRank = cRank - '1';
            targetPairPos = i - 1;
            break;
        }
    }
    if (targetPairPos < 0) return false;

    std::string prefix = remainder.substr(0, static_cast<size_t>(targetPairPos));
    bool capture = prefix.find('x') != std::string::npos;
    std::string disambiguation;
    disambiguation.reserve(prefix.size());
    for (char c : prefix) {
        if (c != 'x') disambiguation.push_back(c);
    }

    char fileHint = 0;
    char rankHint = 0;
    for (char c : disambiguation) {
        if (c >= 'a' && c <= 'h') fileHint = c;
        else if (c >= '1' && c <= '8') rankHint = c;
    }

    int targetIndex = index(targetFile, targetRank);
    PieceColor mover = side;
    PieceColor opponent = opposite(mover);

    Move selected;
    bool found = false;

    for (int sq = 0; sq < 64; ++sq) {
        const Piece& piece = at(sq);
        if (piece.type != pieceType || piece.color != mover) continue;
        int fromFile = fileOf(sq);
        int fromRank = rankOf(sq);
        if (fileHint && fromFile != (fileHint - 'a')) continue;
        if (rankHint && fromRank != (rankHint - '1')) continue;

        bool isPromotion = promotion != PieceType::None;
        if (!canPieceReach(pieceType, mover, fromFile, fromRank, targetFile, targetRank, capture, isPromotion, promotion)) {
            continue;
        }

        Move candidate;
        candidate.from = sq;
        candidate.to = targetIndex;
        candidate.moving = pieceType;
        candidate.promotion = promotion;
        candidate.isCapture = capture;
        candidate.isCastleKingSide = false;
        candidate.isCastleQueenSide = false;
        candidate.isEnPassant = false;

        const Piece& targetPiece = at(targetIndex);
        bool targetOccupied = !isEmptyPiece(targetPiece);
        if (pieceType == PieceType::Pawn && capture && !targetOccupied) {
            if (enPassant != targetIndex) continue;
            candidate.isEnPassant = true;
        }

        if (!capture && targetOccupied) continue;
        if (capture && !candidate.isEnPassant) {
            if (targetOccupied && targetPiece.color == mover) continue;
            if (!targetOccupied) continue;
        }

        if (wouldLeaveKingInCheck(candidate)) {
            continue;
        }

        if (found) {
            return false; // ambiguous SAN
        }
        found = true;
        selected = candidate;
    }

    if (!found) return false;

    // Additional sanity checks for promotion square
    if (selected.moving == PieceType::Pawn && selected.promotion != PieceType::None) {
        if (mover == PieceColor::White && rankOf(selected.to) != 7) return false;
        if (mover == PieceColor::Black && rankOf(selected.to) != 0) return false;
    }

    move = selected;
    return true;
}

bool HeadlessPosition::applyMove(const Move& move) {
    if (move.from < 0 || move.to < 0) return false;
    Piece& movingPiece = at(move.from);
    PieceType movingType = movingPiece.type;
    PieceColor mover = movingPiece.color;

    Piece captured = at(move.to);

    if (move.isCastleKingSide || move.isCastleQueenSide) {
        int rank = (mover == PieceColor::White) ? 0 : 7;
        int rookFromFile = move.isCastleKingSide ? 7 : 0;
        int rookToFile = move.isCastleKingSide ? 5 : 3;

        at(move.to) = movingPiece;
        movingPiece = Piece{};

        Piece& rookFrom = at(rookFromFile, rank);
        Piece rookCopy = rookFrom;
        rookFrom = Piece{};
        at(rookToFile, rank) = rookCopy;
    } else {
        if (move.isEnPassant) {
            int toFile = fileOf(move.to);
            int toRank = rankOf(move.to);
            int capRank = mover == PieceColor::White ? toRank - 1 : toRank + 1;
            at(toFile, capRank) = Piece{};
        }

        at(move.to) = movingPiece;
        if (move.promotion != PieceType::None) {
            at(move.to).type = move.promotion;
        }
        movingPiece = Piece{};
    }

    enPassant = -1;
    if (movingType == PieceType::Pawn) {
        int fromRank = rankOf(move.from);
        int toRank = rankOf(move.to);
        if (std::abs(toRank - fromRank) == 2) {
            int midRank = (fromRank + toRank) / 2;
            enPassant = index(fileOf(move.from), midRank);
        }
    }

    updateCastlingRights(move);
    if (move.isCapture && !move.isEnPassant) {
        updateCastlingRightsForCapture(move.to);
    }

    if (movingType == PieceType::Pawn || move.isCapture || move.isEnPassant) {
        halfmove = 0;
    } else {
        ++halfmove;
    }

    if (mover == PieceColor::Black) {
        ++fullmove;
    }

    side = opposite(side);
    return true;
}

std::string HeadlessPosition::getFEN() const {
    std::ostringstream fen;
    for (int rank = 7; rank >= 0; --rank) {
        int emptyCount = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece& piece = at(file, rank);
            if (isEmptyPiece(piece)) {
                ++emptyCount;
            } else {
                if (emptyCount > 0) {
                    fen << emptyCount;
                    emptyCount = 0;
                }
                fen << charFromPiece(piece.type, piece.color);
            }
        }
        if (emptyCount > 0) fen << emptyCount;
        if (rank > 0) fen << '/';
    }
    fen << ' ' << (side == PieceColor::White ? 'w' : 'b') << ' ';
    std::string castling;
    if (whiteCastleK) castling.push_back('K');
    if (whiteCastleQ) castling.push_back('Q');
    if (blackCastleK) castling.push_back('k');
    if (blackCastleQ) castling.push_back('q');
    if (castling.empty()) castling = "-";
    fen << castling << ' ';
    if (enPassant >= 0) {
        fen << fileChar(fileOf(enPassant)) << rankChar(rankOf(enPassant));
    } else {
        fen << '-';
    }
    fen << ' ' << halfmove << ' ' << fullmove;
    return fen.str();
}

bool HeadlessPosition::isSquareAttacked(int file, int rank, PieceColor byColor) const {
    int pawnDir = (byColor == PieceColor::White) ? 1 : -1;
    int pawnRank = rank - pawnDir;
    if (inBounds(file - 1, pawnRank)) {
        const Piece& p = at(file - 1, pawnRank);
        if (p.type == PieceType::Pawn && p.color == byColor) return true;
    }
    if (inBounds(file + 1, pawnRank)) {
        const Piece& p = at(file + 1, pawnRank);
        if (p.type == PieceType::Pawn && p.color == byColor) return true;
    }

    static const int knightOffsets[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
        {1, -2}, {2, -1}, {-1, -2}, {-2, -1}
    };
    for (auto& off : knightOffsets) {
        int nf = file + off[0];
        int nr = rank + off[1];
        if (!inBounds(nf, nr)) continue;
        const Piece& p = at(nf, nr);
        if (p.type == PieceType::Knight && p.color == byColor) return true;
    }

    if (isLineAttacked(file, rank, 1, 0, byColor)) return true;
    if (isLineAttacked(file, rank, -1, 0, byColor)) return true;
    if (isLineAttacked(file, rank, 0, 1, byColor)) return true;
    if (isLineAttacked(file, rank, 0, -1, byColor)) return true;
    if (isLineAttacked(file, rank, 1, 1, byColor)) return true;
    if (isLineAttacked(file, rank, 1, -1, byColor)) return true;
    if (isLineAttacked(file, rank, -1, 1, byColor)) return true;
    if (isLineAttacked(file, rank, -1, -1, byColor)) return true;

    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int nf = file + df;
            int nr = rank + dr;
            if (!inBounds(nf, nr)) continue;
            const Piece& p = at(nf, nr);
            if (p.type == PieceType::King && p.color == byColor) return true;
        }
    }

    return false;
}

bool HeadlessPosition::isLineAttacked(int file, int rank, int dFile, int dRank, PieceColor byColor) const {
    int nf = file + dFile;
    int nr = rank + dRank;
    while (inBounds(nf, nr)) {
        const Piece& p = at(nf, nr);
        if (!isEmptyPiece(p)) {
            if (p.color == byColor) {
                if (dFile == 0 || dRank == 0) {
                    if (p.type == PieceType::Rook || p.type == PieceType::Queen) return true;
                }
                if (dFile != 0 && dRank != 0) {
                    if (p.type == PieceType::Bishop || p.type == PieceType::Queen) return true;
                }
            }
            break;
        }
        nf += dFile;
        nr += dRank;
    }
    return false;
}

bool HeadlessPosition::wouldLeaveKingInCheck(const Move& move) const {
    HeadlessPosition copy = *this;
    copy.applyMove(move);
    PieceColor mover = opposite(copy.side); // after applyMove toggles side
    int kingSquare = copy.findKingSquare(mover);
    if (kingSquare < 0) return true;
    return copy.isSquareAttacked(fileOf(kingSquare), rankOf(kingSquare), copy.side);
}

int HeadlessPosition::findKingSquare(PieceColor color) const {
    for (int i = 0; i < 64; ++i) {
        const Piece& p = at(i);
        if (p.type == PieceType::King && p.color == color) return i;
    }
    return -1;
}

bool HeadlessPosition::canPieceReach(PieceType type, PieceColor color, int fromFile, int fromRank,
                                      int toFile, int toRank, bool captureRequested,
                                      bool isPromotion, PieceType promotion) const {
    if (!inBounds(toFile, toRank)) return false;
    const Piece& target = at(toFile, toRank);
    if (!isEmptyPiece(target) && target.color == color) return false;

    int fileDiff = toFile - fromFile;
    int rankDiff = toRank - fromRank;
    int absFile = std::abs(fileDiff);
    int absRank = std::abs(rankDiff);

    switch (type) {
        case PieceType::Pawn: {
            int dir = (color == PieceColor::White) ? 1 : -1;
            if (captureRequested) {
                if (absFile != 1 || rankDiff != dir) return false;
                if (isEmptyPiece(target) && enPassant != index(toFile, toRank)) return false;
            } else {
                if (fileDiff != 0) return false;
                if (rankDiff == dir) {
                    if (!isEmptyPiece(target)) return false;
                } else if (rankDiff == 2 * dir) {
                    int startRank = (color == PieceColor::White) ? 1 : 6;
                    if (fromRank != startRank) return false;
                    if (!isEmptyPiece(target)) return false;
                    int midRank = fromRank + dir;
                    if (!isEmptyPiece(at(fromFile, midRank))) return false;
                } else {
                    return false;
                }
            }
            if (isPromotion) {
                if (color == PieceColor::White && toRank != 7) return false;
                if (color == PieceColor::Black && toRank != 0) return false;
                if (promotion == PieceType::None || promotion == PieceType::King || promotion == PieceType::Pawn) return false;
            }
            return true;
        }
        case PieceType::Knight:
            return (absFile == 1 && absRank == 2) || (absFile == 2 && absRank == 1);
        case PieceType::Bishop:
            if (absFile != absRank || absFile == 0) return false;
            return pathClear(fromFile, fromRank, toFile, toRank);
        case PieceType::Rook:
            if (fromFile != toFile && fromRank != toRank) return false;
            if (fromFile == toFile && fromRank == toRank) return false;
            return pathClear(fromFile, fromRank, toFile, toRank);
        case PieceType::Queen:
            if (fromFile == toFile || fromRank == toRank || absFile == absRank) {
                return pathClear(fromFile, fromRank, toFile, toRank);
            }
            return false;
        case PieceType::King:
            if (absFile <= 1 && absRank <= 1 && (absFile + absRank > 0)) return true;
            return false;
        default:
            break;
    }
    return false;
}

bool HeadlessPosition::pathClear(int fromFile, int fromRank, int toFile, int toRank) const {
    int dFile = (toFile > fromFile) ? 1 : (toFile < fromFile ? -1 : 0);
    int dRank = (toRank > fromRank) ? 1 : (toRank < fromRank ? -1 : 0);
    int file = fromFile + dFile;
    int rank = fromRank + dRank;
    while (file != toFile || rank != toRank) {
        if (!isEmptyPiece(at(file, rank))) return false;
        file += dFile;
        rank += dRank;
    }
    return true;
}

void HeadlessPosition::updateCastlingRights(const Move& move) {
    int fromFile = fileOf(move.from);
    int fromRank = rankOf(move.from);
    if (move.moving == PieceType::King) {
        if (fromRank == 0) {
            whiteCastleK = false;
            whiteCastleQ = false;
        } else {
            blackCastleK = false;
            blackCastleQ = false;
        }
    }
    if (move.moving == PieceType::Rook) {
        if (fromRank == 0 && fromFile == 0) whiteCastleQ = false;
        if (fromRank == 0 && fromFile == 7) whiteCastleK = false;
        if (fromRank == 7 && fromFile == 0) blackCastleQ = false;
        if (fromRank == 7 && fromFile == 7) blackCastleK = false;
    }
}

void HeadlessPosition::updateCastlingRightsForCapture(int targetIndex) {
    int file = fileOf(targetIndex);
    int rank = rankOf(targetIndex);
    if (rank == 0 && file == 0) whiteCastleQ = false;
    if (rank == 0 && file == 7) whiteCastleK = false;
    if (rank == 7 && file == 0) blackCastleQ = false;
    if (rank == 7 && file == 7) blackCastleK = false;
}

} // namespace analysis
