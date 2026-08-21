#include "PgnAnalyzer.h"

#include "HeadlessPosition.h"
#include "StockfishEngine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace analysis {
namespace {
struct PgnGame {
    std::unordered_map<std::string, std::string> tags;
    std::vector<std::string> sanMoves;
    std::string result;
};

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool parsePgn(const std::string& path, std::vector<PgnGame>& outGames, std::string& error) {
    std::ifstream input(path);
    if (!input.is_open()) {
        error = "Unable to open PGN file: " + path;
        return false;
    }

    std::string line;
    PgnGame current;
    std::string movesBlock;

    auto finalizeGame = [&]() {
        if (current.tags.empty() && movesBlock.empty()) return;
        if (!movesBlock.empty()) {
            std::string cleaned;
            cleaned.reserve(movesBlock.size());
            bool inBrace = false;
            bool inSemicolon = false;
            int parenDepth = 0;
            for (char c : movesBlock) {
                if (inSemicolon) {
                    if (c == '\n') inSemicolon = false;
                    continue;
                }
                if (inBrace) {
                    if (c == '}') inBrace = false;
                    continue;
                }
                if (c == '{') { inBrace = true; continue; }
                if (c == ';') { inSemicolon = true; continue; }
                if (c == '(') { ++parenDepth; continue; }
                if (c == ')') { if (parenDepth > 0) --parenDepth; continue; }
                if (parenDepth > 0) continue;
                cleaned.push_back(c);
            }

            std::istringstream iss(cleaned);
            std::string token;
            while (iss >> token) {
                token = trim(token);
                if (token.empty()) continue;
                if (token.find('.') != std::string::npos) continue;
                if (token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*") {
                    current.result = token;
                    continue;
                }
                current.sanMoves.push_back(token);
            }
        }
        if (!current.sanMoves.empty()) {
            outGames.push_back(current);
        }
        current = PgnGame{};
        movesBlock.clear();
    };

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            finalizeGame();
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            const auto spacePos = line.find(' ');
            const auto quote1 = line.find('"');
            const auto quote2 = line.find('"', quote1 + 1);
            if (spacePos != std::string::npos && quote1 != std::string::npos && quote2 != std::string::npos) {
                const std::string tag = line.substr(1, spacePos - 1);
                const std::string value = line.substr(quote1 + 1, quote2 - quote1 - 1);
                current.tags[tag] = value;
            }
        } else {
            if (!movesBlock.empty()) movesBlock.push_back(' ');
            movesBlock += line;
        }
    }
    finalizeGame();
    return !outGames.empty();
}

std::string escapeCsv(const std::string& value) {
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }
    std::string escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, 1, '"');
        pos += 2;
    }
    return '"' + escaped + '"';
}

std::string buildPuzzleId(const std::string& hero, std::size_t gameIndex, int fullmove, bool heroWhite) {
    std::ostringstream oss;
    oss << "PGN-";
    if (!hero.empty()) {
        for (char c : hero) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                oss << static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        oss << '-';
    }
    oss << (gameIndex + 1) << '-' << fullmove << (heroWhite ? 'W' : 'B');
    return oss.str();
}

int convertMateToScore(int matePlies) {
    if (matePlies == 0) return 0;
    const int sign = matePlies > 0 ? 1 : -1;
    const int magnitude = 100000 - (std::min)(std::abs(matePlies), 1000) * 10;
    return sign * magnitude;
}

int scoreFromLine(const EngineLine& line) {
    if (line.mate != 0) return convertMateToScore(line.mate);
    return line.score;
}

} // namespace

bool PgnBulkAnalyzer::analyzeFile(const std::string& pgnPath,
                                  const std::string& outputCsvPath,
                                  const AnalyzerOptions& options,
                                  const std::function<void(const AnalyzerProgress&)>& progressCallback,
                                  std::atomic<bool>& cancelFlag,
                                  std::string& errorMessage) {
    std::vector<PgnGame> games;
    if (!parsePgn(pgnPath, games, errorMessage)) {
        if (errorMessage.empty()) errorMessage = "No games found in PGN";
        return false;
    }

    std::unordered_map<std::string, int> playerFrequency;
    for (const auto& game : games) {
        if (auto it = game.tags.find("White"); it != game.tags.end()) {
            playerFrequency[toLower(it->second)]++;
        }
        if (auto it = game.tags.find("Black"); it != game.tags.end()) {
            playerFrequency[toLower(it->second)]++;
        }
    }

    std::string heroLower = toLower(options.heroName);
    if (heroLower.empty()) {
        int bestCount = 0;
        for (const auto& kv : playerFrequency) {
            if (kv.second > bestCount) {
                heroLower = kv.first;
                bestCount = kv.second;
            }
        }
    }
    if (heroLower.empty()) {
        errorMessage = "Could not determine player to analyze (hero name not provided).";
        return false;
    }

    std::size_t totalHeroMoves = 0;
    for (const auto& game : games) {
        const bool heroWhite = (game.tags.count("White") && toLower(game.tags.at("White")) == heroLower);
        const bool heroBlack = (game.tags.count("Black") && toLower(game.tags.at("Black")) == heroLower);
        if (!heroWhite && !heroBlack) continue;
        HeadlessPosition position;
        for (const auto& san : game.sanMoves) {
            Move mv;
            if (!position.decodeSan(san, mv)) {
                break;
            }
            if ((heroWhite && position.sideToMove() == PieceColor::White) ||
                (heroBlack && position.sideToMove() == PieceColor::Black)) {
                ++totalHeroMoves;
            }
            position.applyMove(mv);
        }
    }
    if (totalHeroMoves == 0) {
        errorMessage = "No moves by the specified player were found.";
        return false;
    }

    std::ofstream output(outputCsvPath, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        errorMessage = "Unable to create output file: " + outputCsvPath;
        return false;
    }

    output << "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags,Result,Opponent,Event,Date,Comment\n";

    StockfishEngine engine;
    if (!engine.initialize()) {
        errorMessage = "Failed to initialize Stockfish engine.";
        return false;
    }
    int threadCount = options.engineThreads;
    if (threadCount < 1) threadCount = 1;
    engine.setThreads(threadCount);
    engine.setMultiPV(1);

    AnalyzerProgress progress;
    progress.gameCount = games.size();
    progress.positionCount = totalHeroMoves;
    if (progressCallback) progressCallback(progress);

    std::size_t processedHeroMoves = 0;

    for (std::size_t gameIdx = 0; gameIdx < games.size(); ++gameIdx) {
        if (cancelFlag.load()) {
            errorMessage = "Analysis cancelled";
            return false;
        }
        progress.gameIndex = gameIdx;
        const auto& game = games[gameIdx];
        const std::string whiteName = (game.tags.count("White")) ? game.tags.at("White") : "White";
        const std::string blackName = (game.tags.count("Black")) ? game.tags.at("Black") : "Black";
        const bool heroWhite = (toLower(whiteName) == heroLower);
        const bool heroBlack = (toLower(blackName) == heroLower);
        if (!heroWhite && !heroBlack) continue;

        std::ostringstream status;
        status << whiteName << " vs " << blackName;
        if (auto it = game.tags.find("Date"); it != game.tags.end()) {
            status << " (" << it->second << ")";
        }
        progress.currentLabel = status.str();
        if (progressCallback) progressCallback(progress);

        HeadlessPosition position;
        for (const auto& san : game.sanMoves) {
            if (cancelFlag.load()) {
                errorMessage = "Analysis cancelled";
                return false;
            }
            Move decoded;
            if (!position.decodeSan(san, decoded)) {
                break;
            }

            const PieceColor mover = position.sideToMove();
            const bool heroMove = (mover == PieceColor::White && heroWhite) ||
                                  (mover == PieceColor::Black && heroBlack);

            const std::string fenBefore = position.getFEN();
            const int moveNumber = position.fullmoveNumber();
            const bool heroMovingWhite = (mover == PieceColor::White);

            position.applyMove(decoded);
            const std::string fenAfter = position.getFEN();

            if (heroMove) {
                EngineLine bestLine;
                if (!engine.analyzeBlocking(fenBefore, options.analysisDepth, bestLine)) {
                    processedHeroMoves++;
                    progress.positionIndex = processedHeroMoves;
                    if (progressCallback) progressCallback(progress);
                    continue;
                }
                EngineLine afterLine;
                if (!engine.analyzeBlocking(fenAfter, options.analysisDepth, afterLine)) {
                    processedHeroMoves++;
                    progress.positionIndex = processedHeroMoves;
                    if (progressCallback) progressCallback(progress);
                    continue;
                }

                const int scoreBefore = scoreFromLine(bestLine);
                const int scoreAfterHero = -scoreFromLine(afterLine);
                const int loss = scoreBefore - scoreAfterHero;
                const bool record = (loss >= options.minCentipawnSwing) || (bestLine.mate > 0 && scoreAfterHero < scoreBefore);

                if (record) {
                    const std::string pvMoves = bestLine.pv.empty() ? bestLine.move :
                        ([&]() {
                            std::ostringstream pv;
                            int count = 0;
                            for (const auto& m : bestLine.pv) {
                                if (count++ == options.maxPrincipalVariation) break;
                                if (count > 1) pv << ' ';
                                pv << m;
                            }
                            return pv.str();
                        })();
                    if (!pvMoves.empty()) {
                        std::string puzzleId = buildPuzzleId(heroLower, gameIdx, moveNumber, heroMovingWhite);
                        std::string themes = "realGame missed";
                        if (bestLine.mate > 0) themes += " mate";
                        std::string opponent = heroMovingWhite ? blackName : whiteName;
                        std::string event = (game.tags.count("Event")) ? game.tags.at("Event") : "PGN";
                        std::string date = (game.tags.count("Date")) ? game.tags.at("Date") : "";
                        std::string url = (game.tags.count("Link")) ? game.tags.at("Link") : "";
                        std::string eco = (game.tags.count("ECO")) ? game.tags.at("ECO") : "";
                        std::string result = !game.result.empty() ? game.result :
                            (game.tags.count("Result") ? game.tags.at("Result") : "*");

                        std::ostringstream comment;
                        comment << "Played " << san << " instead of " << bestLine.move
                                << ". Loss " << std::fixed << std::setprecision(2)
                                << (loss / 100.0) << " pawns.";

                        output << escapeCsv(puzzleId) << ','
                               << escapeCsv(fenBefore) << ','
                               << escapeCsv(pvMoves) << ','
                               << "1500,75,0,1," // default stats placeholders
                               << escapeCsv(themes) << ','
                               << escapeCsv(url) << ','
                               << escapeCsv(eco) << ','
                               << escapeCsv(result) << ','
                               << escapeCsv(opponent) << ','
                               << escapeCsv(event) << ','
                               << escapeCsv(date) << ','
                               << escapeCsv(comment.str())
                               << "\n";
                    }
                }

                processedHeroMoves++;
                progress.positionIndex = processedHeroMoves;
                if (progressCallback) progressCallback(progress);
            }
        }
    }

    progress.currentLabel = "Done";
    progress.positionIndex = progress.positionCount;
    if (progressCallback) progressCallback(progress);
    return true;
}

} // namespace analysis
