#include "StockfishEngine.h"
#include <iostream>
#include <sstream>
#include <algorithm>

StockfishEngine::StockfishEngine()
    : isRunning(false), isReady(false),
      childStdInRd(NULL), childStdInWr(NULL),
      childStdOutRd(NULL), childStdOutWr(NULL) {
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
}

StockfishEngine::~StockfishEngine() {
    shutdown();
}

bool StockfishEngine::initialize(const std::string& enginePath) {
    std::cout << "Initializing Stockfish engine..." << std::endl;

    if (!createChildProcess(enginePath)) {
        std::cout << "Failed to start Stockfish process" << std::endl;
        return false;
    }

    isRunning = true;

    // Send UCI command and wait for uciok
    writeToPipe("uci\n");
    auto lines = readAllLines(2000);

    bool uciOk = false;
    for (const auto& line : lines) {
        std::cout << "Engine: " << line << std::endl;
        if (line.find("uciok") != std::string::npos) {
            uciOk = true;
            break;
        }
    }

    if (!uciOk) {
        std::cout << "Engine did not respond with uciok" << std::endl;
        return false;
    }

    // Send isready and wait for readyok
    writeToPipe("isready\n");
    lines = readAllLines(1000);

    for (const auto& line : lines) {
        if (line.find("readyok") != std::string::npos) {
            isReady = true;
            std::cout << "Engine is ready!" << std::endl;
            return true;
        }
    }

    std::cout << "Engine did not respond with readyok" << std::endl;
    return false;
}

void StockfishEngine::shutdown() {
    if (isRunning) {
        writeToPipe("quit\n");
        Sleep(100);

        if (piProcInfo.hProcess) {
            TerminateProcess(piProcInfo.hProcess, 0);
            CloseHandle(piProcInfo.hProcess);
            CloseHandle(piProcInfo.hThread);
        }

        if (childStdInRd) CloseHandle(childStdInRd);
        if (childStdInWr) CloseHandle(childStdInWr);
        if (childStdOutRd) CloseHandle(childStdOutRd);
        if (childStdOutWr) CloseHandle(childStdOutWr);

        isRunning = false;
        isReady = false;
        std::cout << "Engine shut down" << std::endl;
    }
}

bool StockfishEngine::createChildProcess(const std::string& exePath) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create pipes for child process's STDOUT
    if (!CreatePipe(&childStdOutRd, &childStdOutWr, &saAttr, 0)) {
        std::cout << "StdoutRd CreatePipe failed" << std::endl;
        return false;
    }
    if (!SetHandleInformation(childStdOutRd, HANDLE_FLAG_INHERIT, 0)) {
        std::cout << "Stdout SetHandleInformation failed" << std::endl;
        return false;
    }

    // Create pipes for child process's STDIN
    if (!CreatePipe(&childStdInRd, &childStdInWr, &saAttr, 0)) {
        std::cout << "StdinWr CreatePipe failed" << std::endl;
        return false;
    }
    if (!SetHandleInformation(childStdInWr, HANDLE_FLAG_INHERIT, 0)) {
        std::cout << "Stdin SetHandleInformation failed" << std::endl;
        return false;
    }

    // Set up process startup info
    STARTUPINFOA siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = childStdOutWr;
    siStartInfo.hStdOutput = childStdOutWr;
    siStartInfo.hStdInput = childStdInRd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Create the child process
    BOOL success = CreateProcessA(
        exePath.c_str(),    // Application name
        NULL,               // Command line
        NULL,               // Process security attributes
        NULL,               // Primary thread security attributes
        TRUE,               // Handles are inherited
        CREATE_NO_WINDOW,   // Creation flags
        NULL,               // Use parent's environment
        NULL,               // Use parent's current directory
        &siStartInfo,       // STARTUPINFO pointer
        &piProcInfo         // PROCESS_INFORMATION pointer
    );

    if (!success) {
        std::cout << "CreateProcess failed: " << GetLastError() << std::endl;
        return false;
    }

    return true;
}

void StockfishEngine::writeToPipe(const std::string& command) {
    DWORD dwWritten;
    WriteFile(childStdInWr, command.c_str(), command.length(), &dwWritten, NULL);
}

std::string StockfishEngine::readFromPipe(int timeoutMs) {
    DWORD dwRead;
    CHAR chBuf[4096];
    std::string result;

    DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < (DWORD)timeoutMs) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(childStdOutRd, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            DWORD bytesToRead = min(bytesAvailable, sizeof(chBuf) - 1);
            if (ReadFile(childStdOutRd, chBuf, bytesToRead, &dwRead, NULL) && dwRead > 0) {
                chBuf[dwRead] = '\0';
                result += chBuf;
            }
        }
        Sleep(10);
    }

    return result;
}

std::vector<std::string> StockfishEngine::readAllLines(int timeoutMs) {
    std::string allText = readFromPipe(timeoutMs);
    std::vector<std::string> lines;
    std::istringstream stream(allText);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    return lines;
}

bool StockfishEngine::sendPosition(const std::string& fen) {
    if (!isReady) return false;

    std::string command = "position fen " + fen + "\n";
    writeToPipe(command);
    return true;
}

bool StockfishEngine::startAnalysis(int depth, int multiPV) {
    if (!isReady) return false;

    setMultiPV(multiPV);

    std::string command = "go depth " + std::to_string(depth) + "\n";
    writeToPipe(command);
    return true;
}

void StockfishEngine::stopAnalysis() {
    if (isRunning) {
        writeToPipe("stop\n");
    }
}

std::vector<EngineLine> StockfishEngine::getBestLines(int count) {
    std::vector<EngineLine> lines;

    // Read all available output
    auto outputLines = readAllLines(500);

    // Parse info lines
    for (const auto& line : outputLines) {
        if (line.find("info") == 0 && line.find("pv") != std::string::npos) {
            EngineLine engineLine;
            std::istringstream iss(line);
            std::string token;

            bool readingPV = false;
            while (iss >> token) {
                if (token == "depth") {
                    iss >> engineLine.depth;
                }
                else if (token == "score") {
                    iss >> token; // "cp" or "mate"
                    if (token == "cp") {
                        iss >> engineLine.score;
                    }
                    else if (token == "mate") {
                        int mateIn;
                        iss >> mateIn;
                        engineLine.score = (mateIn > 0) ? 10000 : -10000;
                    }
                }
                else if (token == "pv") {
                    readingPV = true;
                }
                else if (readingPV) {
                    engineLine.pv.push_back(token);
                    if (engineLine.move.empty()) {
                        engineLine.move = token;
                    }
                }
            }

            if (!engineLine.move.empty()) {
                lines.push_back(engineLine);
            }
        }
    }

    // Return only the requested count
    if (lines.size() > (size_t)count) {
        lines.resize(count);
    }

    return lines;
}

int StockfishEngine::getEvaluation() {
    auto lines = getBestLines(1);
    if (!lines.empty()) {
        return lines[0].score;
    }
    return 0;
}

std::string StockfishEngine::getBestMove() {
    auto lines = getBestLines(1);
    if (!lines.empty()) {
        return lines[0].move;
    }
    return "";
}

void StockfishEngine::setMultiPV(int count) {
    if (!isReady) return;
    std::string command = "setoption name MultiPV value " + std::to_string(count) + "\n";
    writeToPipe(command);
}

void StockfishEngine::setSkillLevel(int level) {
    if (!isReady) return;
    if (level < 0) level = 0;
    if (level > 20) level = 20;
    std::string command = "setoption name Skill Level value " + std::to_string(level) + "\n";
    writeToPipe(command);
}

void StockfishEngine::setThreads(int count) {
    if (!isReady) return;
    std::string command = "setoption name Threads value " + std::to_string(count) + "\n";
    writeToPipe(command);
}
