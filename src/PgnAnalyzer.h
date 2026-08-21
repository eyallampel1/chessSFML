#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace analysis {

struct AnalyzerOptions {
    std::string heroName;          // optional preferred player name
    int analysisDepth = 16;        // engine depth
    int minCentipawnSwing = 150;   // minimum loss to record (centipawns)
    int maxPrincipalVariation = 6; // number of UCI moves to store in puzzle line
    int engineThreads = 2;         // worker threads for stockfish instance
};

struct AnalyzerProgress {
    std::size_t gameIndex = 0;
    std::size_t gameCount = 0;
    std::size_t positionIndex = 0;
    std::size_t positionCount = 0;
    std::string currentLabel;
};

class PgnBulkAnalyzer {
public:
    bool analyzeFile(const std::string& pgnPath,
                     const std::string& outputCsvPath,
                     const AnalyzerOptions& options,
                     const std::function<void(const AnalyzerProgress&)>& progressCallback,
                     std::atomic<bool>& cancelFlag,
                     std::string& errorMessage);
};

} // namespace analysis
