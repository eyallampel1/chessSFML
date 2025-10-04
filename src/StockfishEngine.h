#ifndef STOCKFISH_ENGINE_H
#define STOCKFISH_ENGINE_H

#include <string>
#include <vector>
#include <windows.h>

struct EngineLine {
    std::string move;           // Best move in this line (e.g., "e2e4")
    std::vector<std::string> pv; // Principal variation (sequence of moves)
    int score;                  // Evaluation in centipawns (+100 = +1.0 pawn advantage)
    int depth;                  // Search depth
    int mate;                   // Mate in plies (0 = checkmate on board, >0 side to move mates in N plies, <0 side to move is mated in N plies)
};

class StockfishEngine {
private:
    // Process handles
    HANDLE childStdInRd;
    HANDLE childStdInWr;
    HANDLE childStdOutRd;
    HANDLE childStdOutWr;
    PROCESS_INFORMATION piProcInfo;

    bool isRunning;
    bool isReady;

    // Helper methods
    bool createChildProcess(const std::string& exePath);
    void writeToPipe(const std::string& command);
    std::string readFromPipe(int timeoutMs = 100);
    std::vector<std::string> readAllLines(int timeoutMs = 1000);

public:
    StockfishEngine();
    ~StockfishEngine();

    // Engine lifecycle
    bool initialize(const std::string& enginePath = "engines/stockfish.exe");
    void shutdown();

    // UCI commands
    bool sendPosition(const std::string& fen);
    bool startAnalysis(int depth = 20, int multiPV = 1);
    void stopAnalysis();

    // Get analysis results
    std::vector<EngineLine> getBestLines(int count = 3);
    int getEvaluation(); // Returns centipawns (+100 = white is up 1 pawn)
    std::string getBestMove();

    // Configuration
    void setMultiPV(int count);
    void setSkillLevel(int level); // 0-20, lower = weaker
    void setThreads(int count);

    // Status
    bool isEngineReady() const { return isReady; }
    bool isEngineRunning() const { return isRunning; }
};

#endif // STOCKFISH_ENGINE_H
