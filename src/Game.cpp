#include "Game.h"
#include "AppPaths.h"
#include "HeadlessPosition.h"
#include "PgnAnalyzer.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/WindowStyle.hpp>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <windows.h>
#include <commdlg.h>
#include <cctype>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

namespace {

// Engine output as the panels want to show it: evaluation from White's point
// of view, and the principal variation in the board's uppercase-file form.
struct EngineText {
	std::string eval;
	std::string pv;
};

EngineText formatEngineLine(const EngineLine& line, PieceColor turn,
                            const std::string& fen, std::size_t maxPly) {
	EngineText out;

	std::ostringstream evalStr;
	if (line.mate != 0) {
		const int moves = (std::abs(line.mate) + 1) / 2;
		const bool currentMating = (turn == PieceColor::WHITE) ? (line.mate > 0) : (line.mate < 0);
		evalStr << (currentMating ? "M" : "M-") << moves;
	} else {
		const int forWhite = (turn == PieceColor::BLACK) ? -line.score : line.score;
		const float pawns = forWhite / 100.0f;
		evalStr << (pawns >= 0 ? "+" : "") << std::fixed << std::setprecision(2) << pawns;
	}
	out.eval = evalStr.str();

	// Real notation - "12.Nf3 d6 13.d4 cxd4" - rather than raw UCI pairs.
	out.pv = analysis::formatPvSan(fen, line.pv, maxPly);

	return out;
}

} // namespace

Game::Game(){
	this->initWindow();
	board = new Board(this->window);

	// Initialize engine components
	engine = new StockfishEngine();
	evalBar = new EvalBar(this->window, &font, 352, 0, 40, 352);  // Moved to right of board
	arrowManager = new ArrowManager(this->window);
	engineInitialized = false;
	gameOver = false;
	puzzleSolved = false;
	analysisRequested = false;
	lastAnalyzedFEN = "";

	// Initialize engine in background
	initEngine();

	// Seed RNG once for varied puzzle sampling
	std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // Initialize user DB stored next to the executable for consistent loads
    std::string dbPath = apppaths::executableDir() + "\\userdb.db";
    userDb = new UserDB(dbPath);
    if (userDb->load()) {
        std::cout << "UserDB loaded from: " << dbPath << " (" << (userDb->isSQLiteEnabled() ? "SQLite" : "Text") << ")" << std::endl;
    } else {
        std::cout << "UserDB load failed at: " << dbPath << std::endl;
    }
    if (puzzleFilePath.empty()) {
        puzzleFilePath = userDb->getLastCsvPath();
        if (!puzzleFilePath.empty()) {
            std::cout << "Loaded CSV path from DB: " << puzzleFilePath << std::endl;
        }
    }

	// Initialize UI buttons
	initButtons();

	// Opening-course trainer: shares the board, the DB and the button font,
	// which initButtons() has just loaded.
	normalWindowSize = window->getSize();
	applyViews(normalWindowSize.x, normalWindowSize.y, 1.0f);
	course = new CourseTrainer(window, &font, board, userDb);
	course->setStatusSink([this](const std::string& m) { setStatusMessage(m); });

	// Initialize status message
	statusMessage = "";
}

Game::~Game(){
	delete course;
	delete board;
	delete engine;
	delete evalBar;
	delete arrowManager;
	if (userDb) { userDb->save(); delete userDb; userDb = nullptr; }

	bulkAnalysisCancel.store(true);
	if (bulkAnalysisThread.joinable()) {
		bulkAnalysisThread.join();
	}

	// Clean up buttons
	for (auto btn : buttons) {
		delete btn;
	}
}

void Game::run(){
	while (this->window->isOpen()) {
		// Don't clear console so we can see button messages
		// std::cout << "\033[2J\033[1;1H";
		//std::system("clear");
		this->printMousePosition();  // MUST call this to update userWantedString!
		this->updateDt();
		this->processEvents();

		// Update board (animations, etc.)
		board->update(dt * 1000.0f);

		// Update hovered square continuously while mouse is moved
		board->updateHoveredSquare(userWantedString);

		// Update buttons
		updateButtons();

		// Course mode drives its own board navigation and reply timing
		if (course && course->isActive()) {
			course->updateHover(sf::Mouse::getPosition(*window));
			course->update(dt * 1000.0f);

			// The trainer moves the pieces itself, so watch the position
			// rather than trying to hook every navigation path.
			if (course->engineEnabled()) {
				const std::string fen = board->getFEN();
				if (fen != lastAnalyzedFEN) {
					updateAnalysis();
					lastAnalyzedFEN = fen;
				} else if (!analysisRequested &&
				           analysisClock.getElapsedTime().asMilliseconds() > 700) {
					// The position is being studied, not played, so keep
					// re-reading the search instead of freezing at whatever
					// depth the first poll happened to catch.
					analysisRequested = true;
					analysisClock.restart();
				}
			} else if (!currentLines.empty()) {
				if (engineInitialized) {
					engine->stopAnalysis();
					analysisRequested = false;
				}
				currentLines.clear();
				currentLineText.clear();
				if (arrowManager) arrowManager->clearArrows();
				if (evalBar) evalBar->setEvaluation(0.0f);
				course->clearEngineLines();
				lastAnalyzedFEN.clear();
			}
		}

		// Check for analysis updates periodically (non-blocking)
		if (engineInitialized && analysisRequested && analysisClock.getElapsedTime().asMilliseconds() > 500) {
			// Get best lines without blocking
			auto lines = engine->getBestLines(3);

			if (!lines.empty()) {
				// Store lines for display
				currentLines = lines;

				{
					const PieceColor turn = board->getCurrentTurn();
					const std::string fen = board->getFEN();
					currentLineText.clear();
					for (size_t i = 0; i < lines.size() && i < 3; ++i) {
						const EngineText t = formatEngineLine(lines[i], turn, fen, 5);
						currentLineText.push_back({t.eval, t.pv});
					}
				}

				// Update eval bar with mate distance if available (White POV)
				{
					const EngineLine& top = lines[0];
					PieceColor turnForEval = board->getCurrentTurn();
					if (top.mate != 0) {
						bool whiteWinning = (turnForEval == PieceColor::WHITE) ? (top.mate > 0) : (top.mate < 0);
						evalBar->setMateEvaluation(top.mate, whiteWinning);

						// Immediate mate on board => stop analysis and mark game over
						if (top.mate == 0 && !gameOver) {
							engine->stopAnalysis();
							analysisRequested = false;
							gameOver = true;
							setStatusMessage("Checkmate - game over");
						}
					} else {
						int evalForWhite = (turnForEval == PieceColor::BLACK) ? -top.score : top.score;
						evalBar->setEvaluation(static_cast<float>(evalForWhite));
					}
				}

				// Update arrows
				arrowManager->clearArrows();
				for (size_t i = 0; i < lines.size() && i < 3; i++) {
					if (lines[i].pv.size() >= 1) {
						std::string move = lines[i].pv[0];
						if (move.length() >= 4) {
							std::string from = move.substr(0, 2);
							std::string to = move.substr(2, 2);
							from[0] = toupper(from[0]);
							to[0] = toupper(to[0]);
							arrowManager->addArrow(from, to, i);
						}
					}
				}

				// The course panel shows the same three ranked lines as the
				// normal analysis panel, which the board area has no room for
				// in that mode.
				if (course && course->isActive() && course->engineEnabled()) {
					const PieceColor turn = board->getCurrentTurn();
					const std::string fen = board->getFEN();
					std::vector<CourseTrainer::EngineReadout> readouts;
					for (size_t i = 0; i < lines.size() && i < 3; ++i) {
						// The course panel is wide enough for a longer line.
						const EngineText t = formatEngineLine(lines[i], turn, fen, 8);
						readouts.push_back({t.eval, t.pv});
					}
					course->setEngineLines(std::move(readouts), lines[0].depth);
				}
			}

			analysisRequested = false;
		}

		this->render();
	}

}

void Game::render(){
	this->window->clear();

	const bool courseActive = (course && course->isActive());

	// Board, arrows and course markings all draw in the board's own 0..352
	// coordinates, so they go through the board view together and scale as one.
	this->window->setView(boardView);
	this->board->render();
	if (arrowManager && (!courseActive || course->linesVisible())) {
		arrowManager->render();
	}
	if (courseActive) course->renderBoardOverlay();
	this->window->setView(uiView);

	if (courseActive) {
		// The trainer owns everything outside the board in this mode; the
		// normal button column is hidden entirely (Exit Course brings it back)
		// so nothing competes with the course controls.
		if (course->evalVisible() && evalBar) evalBar->render();
		course->render();
		if (promotionOpen) {
			// The dialog is drawn in board coordinates, so it scales too.
			window->setView(boardView);
			renderPromotionDialog();
			window->setView(uiView);
		}
		this->window->display();
		return;
	}

    // Update gameOver flag from board checkmate (only in normal game mode)
    if (!puzzleMode && board->getIsCheckmate()) {
        if (!gameOver) {
            // Stop analysis once when mate detected
            if (engineInitialized) {
                engine->stopAnalysis();
                analysisRequested = false;
            }
            gameOver = true;
        }
    }

    // Persistent checkmate banner over the board area (not during puzzle mode)
    if (!puzzleMode && gameOver) {
        renderCheckmateBanner();
    }

    // Puzzle solved banner (if in puzzle mode)
    if (puzzleMode && puzzleSolved) {
        renderPuzzleSolvedBanner();
    }

	if (remainWithThisColor) {
		this->renderText();
	}
	else {
		this->renderText(sf::Color::Red);
	}
	if(printSecTextLine){
		this->renderText(sf::Color::Red,42,startingPosition+endPosition);
	}

	// Display CHECK! message if in check
	if (board->getIsCheck()) {
		this->renderText(sf::Color::Red, 70, "CHECK!");
	}

	// Render eval bar
	if (evalBar) {
		evalBar->render();
	}

    // Render UI
    renderUI();

    // Render analysis panel
    renderAnalysisPanel();

    // Render promotion dialog on top if open
    if (promotionOpen) {
        renderPromotionDialog();
    }

	this->window->display();
}

void Game::renderCheckmateBanner() {
    // Semi-transparent overlay over the 352x352 board area
    sf::RectangleShape overlay;
    overlay.setSize(sf::Vector2f(352.0f, 352.0f));
    overlay.setPosition(0.0f, 0.0f);
    overlay.setFillColor(sf::Color(0, 0, 0, 150));
    window->draw(overlay);

    // Centered CHECKMATE text
    sf::Text title;
    title.setFont(font);
    title.setString("CHECKMATE");
    title.setCharacterSize(48);
    title.setStyle(sf::Text::Bold);
    title.setFillColor(sf::Color::White);

    sf::FloatRect tb = title.getLocalBounds();
    title.setOrigin(tb.left + tb.width / 2.0f, tb.top + tb.height / 2.0f);
    title.setPosition(352.0f / 2.0f, 352.0f / 2.0f - 10.0f);
    window->draw(title);

    // Subtext
    sf::Text sub;
    sub.setFont(font);
    sub.setString("Game Over");
    sub.setCharacterSize(22);
    sub.setFillColor(sf::Color(220, 220, 220));
    sf::FloatRect sb = sub.getLocalBounds();
    sub.setOrigin(sb.left + sb.width / 2.0f, sb.top + sb.height / 2.0f);
    sub.setPosition(352.0f / 2.0f, 352.0f / 2.0f + 28.0f);
    window->draw(sub);
}


void Game::updateDt(){
	dt=dtClock.restart().asSeconds();
	// std::cout << dt << std::endl;  // Commented out to avoid spam
}

void Game::centerWindow(){


	desktop = sf::VideoMode::getDesktopMode();
	getWindow.x=desktop.width/2 - window->getSize().x/2;
	getWindow.y=desktop.height/2-window->getSize().y/2;
	this->window->setPosition(getWindow);


}


void Game::initWindow(){
	// Window size: 352 (board) + 40 (eval bar) + 200 (UI panel) = 592 width
	// Increase height to show more puzzle metadata below the board
    this->window=new sf::RenderWindow(sf::VideoMode(592, 640),
            "Made By Eyal Lampel", sf::Style::Titlebar|sf::Style::Close);
	this->centerWindow();
	this->window->setFramerateLimit(120);
	this->window->setVerticalSyncEnabled(true);

	// Title-bar and alt-tab icon. The executable's own Explorer icon comes
	// from the same artwork, compiled in as a resource by src/app.rc.
	{
		sf::Image icon;
		if (icon.loadFromFile(apppaths::resource("src/assets/icon.png"))) {
			this->window->setIcon(icon.getSize().x, icon.getSize().y, icon.getPixelsPtr());
		}
	}

}

void Game::printMousePosition(){
	position = sf::Mouse::getPosition(*window);
	if (boardScale != 1.0f) {
		// Squares are worked out in unscaled board coordinates, so undo the
		// board view before asking which square the cursor is over.
		const sf::Vector2f onBoard = window->mapPixelToCoords(position, boardView);
		position = sf::Vector2i(static_cast<int>(onBoard.x), static_cast<int>(onBoard.y));
	}
	// std::cout<<"mouse position x="<<position.x<<std::endl;  // Commented out to avoid spam
	// std::cout<<"mouse position y="<<position.y<<std::endl;  // Commented out to avoid spam
	convertMousePositionToCordinate();
}

void Game::processEvents(){
	// std::cout<< "inside process Events" << std::endl;  // Commented out to avoid spam
	while( this->window->pollEvent(event)){

		// Close window : exit
		if (event.type == sf::Event::Closed)
			this->window->close();

		

		// Keyboard events
		if (event.type == sf::Event::KeyPressed)
		{
			if (course && course->isActive() && course->handleKey(event.key.code)) {
				if (!course->isActive()) setCourseLayout(false);
				continue;
			}
			handleKeyboard(event.key);
		}

		if (event.type == sf::Event::MouseWheelScrolled && course && course->isActive()) {
			if (course->handleMouseWheel(sf::Mouse::getPosition(*window),
			                             event.mouseWheelScroll.delta)) {
				continue;
			}
		}

        if(event.type == sf::Event::MouseButtonPressed)
        {
            sf::Vector2i mousePos = sf::Mouse::getPosition(*window);

            // The trainer gets first refusal on clicks while course mode is on
            if (course && course->isActive() && !promotionOpen) {
                if (event.mouseButton.button == sf::Mouse::Left && course->handleMousePress(mousePos)) {
                    if (!course->isActive()) setCourseLayout(false);
                    continue;
                }
                if (course->boardLocked()) continue;
            }

            // If promotion dialog open, only handle promotion clicks
            if (promotionOpen && event.mouseButton.button == sf::Mouse::Left) {
                // The dialog is laid out in board coordinates, so test there.
                const sf::Vector2f onBoard = window->mapPixelToCoords(mousePos, boardView);
                // Map click to one of the 4 options
                float btnY = 158.0f;
                for (int i = 0; i < 4; ++i) {
                    float btnX = 86.0f + i * 50.0f;
                    sf::FloatRect rect(btnX, btnY, 44.0f, 38.0f);
                    if (rect.contains(onBoard.x, onBoard.y)) {
                        PieceType pt = PieceType::QUEEN;
                        if (i == 1) pt = PieceType::ROOK;
                        else if (i == 2) pt = PieceType::BISHOP;
                        else if (i == 3) pt = PieceType::KNIGHT;
                        if (board->setLastMovePromotion(pt)) {
                            promotionOpen = false;
                            if (course && course->isActive()) {
                                course->onUserMove();
                                break;
                            }
                            // After promotion selection, run puzzle validation if active (disabled in analysis mode)
                            if (puzzleMode && !puzzleAnalysisEnabled && puzzleIndex < puzzleMoves.size()) {
                                std::string last = board->getLastMoveUCI();
                                if (last == puzzleMoves[puzzleIndex]) {
                                    puzzleIndex++;
                                    setStatusMessage("Correct!");
                                    if (puzzleIndex >= puzzleMoves.size()) {
                                        puzzleSolved = true;
                                        puzzleSolvedClock.restart();
                                        setStatusMessage("Puzzle solved! Great job"); if (userDb) { userDb->adjustRating(15); if (!currentPuzzleId.empty()) userDb->removeFromReview(currentPuzzleId); userDb->save(); }
                                    } else {
                                        const std::string& reply = puzzleMoves[puzzleIndex];
                                        // Slow reply, no initial wait
                                        board->setNextProgrammaticAnimation(700.0f, 0.0f);
                                        if (board->applyUCIMove(reply)) {
                                            puzzleIndex++;
                                            std::string turn = (board->getCurrentTurn() == PieceColor::WHITE) ? "White" : "Black";
                                            setStatusMessage("Reply played. " + turn + " to move");
                                        }
                                    }
                                } else if (!puzzleAnalysisEnabled) {
                                    setStatusMessage("Incorrect move"); if (userDb) { userDb->adjustRating(-15); if (!currentPuzzleId.empty()) userDb->pushReview(currentPuzzleId); userDb->save(); }                                    board->undoLastMove();
                                }
                            }
                        }
                        break;
                    }
                }
                // Skip normal processing when dialog is open
                continue;
            }

            // Check button clicks first
            bool buttonClicked = false;
            for (auto btn : buttons) {
                if (btn->contains(mousePos)) {
                    btn->handleClick(mousePos);
					buttonClicked = true;
					break;
				}
			}

            // Only process board clicks if no button was clicked and no promotion dialog
            if (!buttonClicked && !promotionOpen) {
				// Left click - pick up piece
				if (event.mouseButton.button == sf::Mouse::Left) {
					board->handleClick(userWantedString);
					startingPosition=userWantedString+"->";
					remainWithThisColor=false;
				}
				// Right click - cancel move
				else if (event.mouseButton.button == sf::Mouse::Right) {
					board->handleRightClick();
					remainWithThisColor=true;
					printSecTextLine=false;
				}
			}
		}
		if(event.type == sf::Event::MouseButtonReleased)
		{
			// Only process left mouse button release
			if (event.mouseButton.button == sf::Mouse::Left && course && course->isActive()) {
				if (!course->boardLocked()) {
					const size_t prevCount = board->getMoveCount();
					board->handleRelease(userWantedString);
					if (board->getMoveCount() > prevCount) {
						// Wait for the piece choice before judging a promotion
						std::string last = board->getLastMoveUCI();
						bool awaitingPromotion = false;
						if (last.size() >= 4) {
							std::string to;
							to.push_back(std::toupper(static_cast<unsigned char>(last[2])));
							to.push_back(last[3]);
							const int rank = last[3] - '0';
							if (board->getPieceTypeAt(to) == PieceType::PAWN && (rank == 8 || rank == 1)) {
								promotionOpen = true;
								awaitingPromotion = true;
							}
						}
						if (!awaitingPromotion) course->onUserMove();
					}
				}
				remainWithThisColor = true;
				printSecTextLine = false;
				continue;
			}

			if (event.mouseButton.button == sf::Mouse::Left) {
				// If puzzle is already solved, ignore further moves and prompt next
				if (puzzleMode && puzzleSolved) {
					setStatusMessage("Puzzle solved! Click Next Puzzle");
					remainWithThisColor = true;
					printSecTextLine = false;
				} else {
					endPosition=userWantedString;
            		// Track move count before release to detect if a legal move was executed
            		size_t prevCount = board->getMoveCount();
            		board->handleRelease(endPosition);

            		// If promotion is required, open dialog and delay validation
            		bool openedPromotion = false;
            		{
            			size_t postCount = board->getMoveCount();
            			if (postCount > prevCount) {
            				std::string last = board->getLastMoveUCI();
            				if (last.size() >= 4) {
            					std::string to;
            					to.push_back(std::toupper(static_cast<unsigned char>(last[2])));
            					to.push_back(last[3]);
            					PieceType ptAt = board->getPieceTypeAt(to);
            					int rank = last[3] - '0';
            					if (ptAt == PieceType::PAWN && (rank == 8 || rank == 1)) {
            						promotionOpen = true;
            						openedPromotion = true;
            					}
            				}
            			}
            		}

            		// Puzzle validation: only if a new move was actually made and not waiting for promotion (disabled in analysis mode)
            		if (puzzleMode && !puzzleAnalysisEnabled && !openedPromotion) {
            			size_t postCount = board->getMoveCount();
            			if (postCount > prevCount && puzzleIndex < puzzleMoves.size()) {
             				std::string last = board->getLastMoveUCI();
							if (last == puzzleMoves[puzzleIndex]) {
								puzzleIndex++;
								setStatusMessage("Correct!");
								// If no more moves in the puzzle, mark solved
								if (puzzleIndex >= puzzleMoves.size()) {
									puzzleSolved = true;
									puzzleSolvedClock.restart();
									setStatusMessage("Puzzle solved! Great job"); if (userDb) { userDb->adjustRating(15); if (!currentPuzzleId.empty()) userDb->removeFromReview(currentPuzzleId); userDb->save(); }
								} else {
            // Auto-play opponent reply if any (slow, no wait)
            const std::string& reply = puzzleMoves[puzzleIndex];
            board->setNextProgrammaticAnimation(700.0f, 0.0f);
            if (board->applyUCIMove(reply)) {
										puzzleIndex++;
										std::string turn = (board->getCurrentTurn() == PieceColor::WHITE) ? "White" : "Black";
										setStatusMessage("Reply played. " + turn + " to move");
									} else {
										setStatusMessage("Failed to play puzzle reply");
									}
								}
            						} else if (postCount > prevCount) {
								// Legal move made but not matching puzzle
								if (!puzzleAnalysisEnabled) {
									setStatusMessage("Incorrect move"); if (userDb) { userDb->adjustRating(-15); if (!currentPuzzleId.empty()) userDb->pushReview(currentPuzzleId); userDb->save(); }                                    board->undoLastMove();
								}
							}
						}
					}
				}
				remainWithThisColor=true;
				printSecTextLine=true;

				// Trigger analysis update after move (non-blocking)
				std::string currentFEN = board->getFEN();
				if (currentFEN != lastAnalyzedFEN) {
					if (!puzzleMode || puzzleAnalysisEnabled) updateAnalysis();
					lastAnalyzedFEN = currentFEN;
				}
			}
		}
	}
}

void Game::renderPromotionDialog() {
    // Dark overlay over the board
    sf::RectangleShape overlay;
    overlay.setSize(sf::Vector2f(352.0f, 352.0f));
    overlay.setPosition(0.0f, 0.0f);
    overlay.setFillColor(sf::Color(0, 0, 0, 160));
    window->draw(overlay);

    // Dialog box
    sf::RectangleShape box;
    box.setSize(sf::Vector2f(220.0f, 120.0f));
    box.setPosition(66.0f, 116.0f);
    box.setFillColor(sf::Color(40, 40, 40));
    box.setOutlineColor(sf::Color(200, 200, 200));
    box.setOutlineThickness(2.0f);
    window->draw(box);

    // This dialog draws in board coordinates, which course mode magnifies, so
    // glyphs are rasterised at their on-screen size and scaled back down.
    auto boardText = [this](const std::string& s, unsigned int size) {
        sf::Text t;
        t.setFont(font);
        t.setCharacterSize(static_cast<unsigned int>(size * boardScale + 0.5f));
        t.setScale(1.0f / boardScale, 1.0f / boardScale);
        t.setFillColor(sf::Color::White);
        t.setString(s);
        return t;
    };

    sf::Text title = boardText("Promote to:", 18);
    title.setPosition(86.0f, 128.0f);
    window->draw(title);

    // Options: Q R B N as simple buttons
    const char* labels[4] = {"Queen", "Rook", "Bishop", "Knight"};
    for (int i = 0; i < 4; ++i) {
        float btnX = 86.0f + i * 50.0f;
        float btnY = 158.0f;
        sf::RectangleShape btn(sf::Vector2f(44.0f, 38.0f));
        btn.setPosition(btnX, btnY);
        btn.setFillColor(sf::Color(70, 70, 70));
        btn.setOutlineColor(sf::Color(150, 150, 150));
        btn.setOutlineThickness(1.0f);
        window->draw(btn);

        sf::Text lbl = boardText(labels[i], 12);
        sf::FloatRect lb = lbl.getLocalBounds();
        lbl.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        lbl.setPosition(btnX + 22.0f, btnY + 19.0f);
        window->draw(lbl);
    }
}

void Game::renderPuzzleSolvedBanner() {
    // Semi-transparent overlay over the board area
    sf::RectangleShape overlay;
    overlay.setSize(sf::Vector2f(352.0f, 352.0f));
    overlay.setPosition(0.0f, 0.0f);
    overlay.setFillColor(sf::Color(0, 100, 0, 140)); // green tint
    window->draw(overlay);

    // Main text
    sf::Text title;
    title.setFont(font);
    title.setString("PUZZLE SOLVED!");
    title.setCharacterSize(42);
    title.setStyle(sf::Text::Bold);
    title.setFillColor(sf::Color(255, 255, 255));
    sf::FloatRect tb = title.getLocalBounds();
    title.setOrigin(tb.left + tb.width / 2.0f, tb.top + tb.height / 2.0f);
    title.setPosition(352.0f / 2.0f, 352.0f / 2.0f - 10.0f);
    window->draw(title);

    // Subtext
    sf::Text sub;
    sub.setFont(font);
    sub.setString("Click Next Puzzle");
    sub.setCharacterSize(20);
    sub.setFillColor(sf::Color(230, 255, 230));
    sf::FloatRect sb = sub.getLocalBounds();
    sub.setOrigin(sb.left + sb.width / 2.0f, sb.top + sb.height / 2.0f);
    sub.setPosition(352.0f / 2.0f, 352.0f / 2.0f + 24.0f);
    window->draw(sub);
}


void Game::convertMousePositionToCordinate(){
	for (int i = 0; i < 8; i++) {

		if(position.x>0 && position.x<45 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "A" <<8-i<< std::endl;
			userWantedString="A"+std::to_string(8-i);
		}

		if(position.x>45 && position.x<45*2 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "B" <<8-i<< std::endl;
			userWantedString="B"+std::to_string(8-i);
		}
		if(position.x>45*2 && position.x<45*3 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "C" <<8-i<< std::endl;
			userWantedString="C"+std::to_string(8-i);
		}
		if(position.x>45*3 && position.x<45*4 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "D" <<8-i<< std::endl;
			userWantedString="D"+std::to_string(8-i);
		}
		if(position.x>45*4 && position.x<45*5 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "E" <<8-i<< std::endl;
			userWantedString="E"+std::to_string(8-i);
		}
		if(position.x>45*5 && position.x<45*6 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "F" <<8-i<< std::endl;
			userWantedString="F"+std::to_string(8-i);
		}
		if(position.x>44*6 && position.x<44*7 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "G" <<8-i<< std::endl;
			userWantedString="G"+std::to_string(8-i);
		}
		if(position.x>44*7 && position.x<44*8 && position.y >i*45 && position.y<(i+1)*45)
		{
			// std::cout << "H" <<8-i<< std::endl;
			userWantedString="H"+std::to_string(8-i);
		}

	}

}

void Game::renderText(){
	//defult color is white
	renderText(sf::Color::White);
}

void Game::renderText(sf::Color color){
	//defult y position is 0
	renderText(color,0);
}

void Game::renderText(sf::Color color,int yPosition){
	//defult is text from global userWantedString
	renderText(color,yPosition,userWantedString);
}

void Game::renderText(sf::Color color,int yPosition,std::string textToRender){

	// Try Windows font first, fall back to Linux path
	if (!font.loadFromFile("C:/Windows/Fonts/arial.ttf")) {
		font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans-ExtraLight.ttf");
	}
	if (board) board->setFont(&font);

	// select the font
	text.setFont(font); // font is a sf::Font
	// set the string to display
	text.setString(textToRender);

	// set the character size
	text.setCharacterSize(24); // in pixels, not points!

	// set the color
	text.setFillColor(color);
	text.setPosition(400, yPosition + 150);  // Position in UI panel below buttons
	text.setStyle(sf::Text::Bold); //| sf::Text::Underlined);
	this->window->draw(text);

}

void Game::initEngine() {
	std::cout << "Initializing Stockfish engine..." << std::endl;

	// Try to initialize engine (resolved against the executable, so a shortcut
	// or an installed copy finds it too)
	if (engine->initialize(apppaths::resource("engines/stockfish.exe"))) {
		engineInitialized = true;
		std::cout << "Engine initialized successfully!" << std::endl;

		// Start initial analysis
		updateAnalysis();
	} else {
		std::cout << "Failed to initialize engine. Analysis features disabled." << std::endl;
		engineInitialized = false;
	}
}

void Game::updateAnalysis() {
	if (!engineInitialized || gameOver || (puzzleMode && !puzzleAnalysisEnabled)) return;
	// In course mode the engine only runs when the user asks for it.
	if (course && course->isActive() && !course->engineEnabled()) return;

	// Get current position from board
	std::string fen = board->getFEN();

	// Send position to engine
	engine->sendPosition(fen);

	// Start analysis (depth 15, 3 lines) - NON-BLOCKING
	engine->startAnalysis(15, 3);

	// Mark that we've requested analysis and restart timer
	analysisRequested = true;
	analysisClock.restart();

	std::cout << "Analysis started (non-blocking)" << std::endl;
}

void Game::handleKeyboard(sf::Event::KeyEvent key) {
	// E key - toggle eval bar
	if (key.code == sf::Keyboard::E) {
		if (evalBar) {
			evalBar->toggleVisibility();
			std::cout << "Eval bar toggled" << std::endl;
		}
	}

	// A key - toggle arrows
	if (key.code == sf::Keyboard::A) {
		if (arrowManager) {
			arrowManager->toggleVisibility();
			std::cout << "Arrows toggled" << std::endl;
		}
	}
}

void Game::initButtons() {
	// Load font for buttons
	if (!font.loadFromFile("C:/Windows/Fonts/arial.ttf")) {
		font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans-ExtraLight.ttf");
	}

	// UI panel starts at x=392 (352 board + 40 eval bar)
	float panelX = 392.0f;
	float buttonWidth = 180.0f;
	float buttonHeight = 35.0f;
	float spacing = 10.0f;

    // Undo button
    undoButton = new Button(window, &font, "Undo", panelX + 10, 10, buttonWidth, buttonHeight);
	undoButton->setOnClick([this]() {
		if (board->canUndo()) {
			board->undoLastMove();
			updateAnalysis();
			setStatusMessage("Move undone");
			std::cout << "Move undone" << std::endl;
		} else {
			setStatusMessage("No moves to undo");
			std::cout << "No moves to undo" << std::endl;
		}
	});
	buttons.push_back(undoButton);

    // Save PGN button
    savePgnButton = new Button(window, &font, "Save PGN", panelX + 10, 10 + buttonHeight + spacing, buttonWidth, buttonHeight);
	savePgnButton->setOnClick([this]() {
		try {
			std::cout << "Save PGN clicked - opening file dialog..." << std::endl;
			std::string filePath = saveFileDialog();

			if (filePath.empty()) {
				std::cout << "Save cancelled by user" << std::endl;
				setStatusMessage("Save cancelled");
				return;
			}

			std::cout << "Saving to: " << filePath << std::endl;
			std::string pgn = board->getPGN();

			std::ofstream file(filePath, std::ios::out | std::ios::trunc);
			if (file.is_open()) {
				file << pgn;
				file.flush();
				file.close();
				setStatusMessage("Game saved!");
				std::cout << "SUCCESS: Game saved to " << filePath << std::endl;
			} else {
				setStatusMessage("Save failed!");
				std::cout << "ERROR: Could not open file for writing" << std::endl;
			}
		} catch (const std::exception& e) {
			setStatusMessage("Save error!");
			std::cout << "EXCEPTION in Save PGN: " << e.what() << std::endl;
		}
	});
	buttons.push_back(savePgnButton);

    // Load PGN button
    loadPgnButton = new Button(window, &font, "Load PGN", panelX + 10, 10 + (buttonHeight + spacing) * 2, buttonWidth, buttonHeight);
	loadPgnButton->setOnClick([this]() {
		try {
			std::cout << "Load PGN clicked - opening file dialog..." << std::endl;
			std::string filePath = openFileDialog();

			if (filePath.empty()) {
				std::cout << "Load cancelled by user" << std::endl;
				setStatusMessage("Load cancelled");
				return;
			}

			std::cout << "Loading from: " << filePath << std::endl;
			std::ifstream file(filePath);
			if (file.is_open()) {
				std::cout << "File opened, reading..." << std::endl;
				std::string pgn((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
				file.close();
				std::cout << "File read, loading into board..." << std::endl;
				board->loadPGN(pgn);
				setStatusMessage("Game loaded!");
				std::cout << "SUCCESS: PGN loaded from " << filePath << std::endl;
				updateAnalysis();
			} else {
				setStatusMessage("File open failed!");
				std::cout << "ERROR: Could not open " << filePath << std::endl;
			}
		} catch (const std::exception& e) {
			setStatusMessage("Load error!");
			std::cout << "EXCEPTION in Load PGN: " << e.what() << std::endl;
		}
	});
    buttons.push_back(loadPgnButton);

    // New Game button
    newGameButton = new Button(window, &font, "New Game", panelX + 10, 10 + (buttonHeight + spacing) * 3, buttonWidth, buttonHeight);
    newGameButton->setOnClick([this]() {
        // Reset game state
        board->reset();
        arrowManager->clearArrows();
        currentLines.clear();
				currentLineText.clear();
        gameOver = false;
        setStatusMessage("New game started");

        // Reset eval bar and analysis
        if (evalBar) evalBar->setEvaluation(0.0f);
        lastAnalyzedFEN.clear();
        if (engineInitialized) {
            updateAnalysis();
            lastAnalyzedFEN = board->getFEN();
        }
    });
    buttons.push_back(newGameButton);

    // Puzzle Mode button
    puzzleModeButton = new Button(window, &font, "Puzzle Mode", panelX + 10, 10 + (buttonHeight + spacing) * 4, buttonWidth, buttonHeight);
    puzzleModeButton->setOnClick([this]() {
        // If memory path is empty but DB has a saved path, use it
        if (puzzleFilePath.empty() && userDb && !userDb->getLastCsvPath().empty()) {
            puzzleFilePath = userDb->getLastCsvPath();
        }
        if (puzzleFilePath.empty()) {
            std::string path = openCSVFileDialog();
            if (path.empty()) { setStatusMessage("Puzzle load cancelled"); return; }
            if (path.size() >= 4 && path.substr(path.size()-4) == ".zst") {
                setStatusMessage("Decompress .zst to .csv and select it");
                return;
            }
            puzzleFilePath = path; if (userDb) { userDb->setLastCsvPath(path); userDb->save(); }
        }
        puzzleMode = true;
        puzzleSolved = false;
        arrowManager->clearArrows();
        currentLines.clear();
				currentLineText.clear();
        gameOver = false;
        if (evalBar) evalBar->setEvaluation(0.0f);
        if (engineInitialized) {
            // Default off in puzzle mode unless explicitly enabled
            engine->stopAnalysis();
            analysisRequested = false;
        }
        if (loadRandomPuzzle()) setStatusMessage("Puzzle loaded");
        else setStatusMessage("Failed to load puzzle");
        // If puzzle analysis is enabled, kick off analysis now
        if (puzzleAnalysisEnabled && engineInitialized) {
            updateAnalysis();
        }
    });
    buttons.push_back(puzzleModeButton);

    // Next Puzzle button
    nextPuzzleButton = new Button(window, &font, "Next Puzzle", panelX + 10, 10 + (buttonHeight + spacing) * 5, buttonWidth, buttonHeight);
    nextPuzzleButton->setOnClick([this]() {
        if (!puzzleMode) { setStatusMessage("Enable Puzzle Mode first"); return; }
        if (puzzleFilePath.empty()) { setStatusMessage("Select puzzle CSV first"); return; }
        puzzleSolved = false;
        if (loadRandomPuzzle()) setStatusMessage("Next puzzle loaded");
        else setStatusMessage("Failed to load puzzle");
        if (puzzleAnalysisEnabled && engineInitialized) {
            updateAnalysis();
        }
    });
    buttons.push_back(nextPuzzleButton);

    // Puzzle Analysis toggle button (default Off)
    puzzleAnalysisButton = new Button(window, &font, "Analysis: Off", panelX + 10, 10 + (buttonHeight + spacing) * 6, buttonWidth, buttonHeight);
    puzzleAnalysisButton->setOnClick([this]() {
        puzzleAnalysisEnabled = !puzzleAnalysisEnabled;
        puzzleAnalysisButton->setLabel(puzzleAnalysisEnabled ? std::string("Analysis: On") : std::string("Analysis: Off"));
        if (puzzleMode) {
            if (puzzleAnalysisEnabled) {
                if (engineInitialized) updateAnalysis();
            } else {
                if (engineInitialized) {
                    engine->stopAnalysis();
                    analysisRequested = false;
                }
                arrowManager->clearArrows();
                currentLines.clear();
				currentLineText.clear();
                if (evalBar) evalBar->setEvaluation(0.0f);
            }
        }
    });
    buttons.push_back(puzzleAnalysisButton);

    // Back to Puzzle button: restore starting puzzle position (after applying first move)
    backToPuzzleButton = new Button(window, &font, "Back To Puzzle", panelX + 10, 10 + (buttonHeight + spacing) * 7, buttonWidth, buttonHeight);
    backToPuzzleButton->setOnClick([this]() {
        if (!puzzleMode) { setStatusMessage("Enable Puzzle Mode first"); return; }
        if (puzzleStartFEN.empty()) { setStatusMessage("No baseline puzzle state"); return; }
        // Reset to FEN and reapply the first move if present
        if (board->setFEN(puzzleStartFEN)) {
            puzzleIndex = 0;
            if (!puzzleFirstMove.empty()) {
                board->setNextProgrammaticAnimation(300.0f, 0.0f);
                if (board->applyUCIMove(puzzleFirstMove)) {
                    puzzleIndex = 1;
                }
            }
            puzzleSolved = false;
            setStatusMessage("Returned to puzzle");
            arrowManager->clearArrows();
            currentLines.clear();
				currentLineText.clear();
            if (evalBar) evalBar->setEvaluation(0.0f);
            if (puzzleAnalysisEnabled && engineInitialized) updateAnalysis();
        }
    });
    buttons.push_back(backToPuzzleButton);

    // Change CSV button
    changeCsvButton = new Button(window, &font, "Change Puzzles CSV", panelX + 10, 10 + (buttonHeight + spacing) * 8, buttonWidth, buttonHeight);
    changeCsvButton->setOnClick([this]() {
        std::string path = openCSVFileDialog();
        if (!path.empty()) {
            puzzleFilePath = path;
            if (userDb) { userDb->setLastCsvPath(path); userDb->save(); }
            setStatusMessage("CSV updated");
        }
    });
    buttons.push_back(changeCsvButton);

    analyzePgnButton = new Button(window, &font, "Analyze PGN", panelX + 10, 10 + (buttonHeight + spacing) * 9, buttonWidth, buttonHeight);
    analyzePgnButton->setOnClick([this]() {
        if (bulkAnalysisActive.load()) {
            setStatusMessage("Analysis already running");
            return;
        }
        std::string path = openFileDialog();
        if (path.empty()) {
            setStatusMessage("Analysis cancelled");
            return;
        }
        startBulkAnalysis(path);
    });
    buttons.push_back(analyzePgnButton);

    // Hakobyan full 1.d4 course
    courseButton = new Button(window, &font, "Hakobyan 1.d4 Course", panelX + 10, 10 + (buttonHeight + spacing) * 10, buttonWidth, buttonHeight);
    courseButton->setOnClick([this]() {
        if (!course) return;
        if (course->isActive()) {
            course->leave();
            setCourseLayout(false);
            setStatusMessage("Left course mode");
            return;
        }
        // Course mode takes the board over, so drop whatever else was running.
        puzzleMode = false;
        puzzleSolved = false;
        gameOver = false;
        currentLines.clear();
				currentLineText.clear();
        if (arrowManager) arrowManager->clearArrows();
        if (evalBar) evalBar->setEvaluation(0.0f);
        if (engineInitialized) {
            engine->stopAnalysis();
            analysisRequested = false;
        }

        setCourseLayout(true);
        if (!course->enter([this]() { return openCourseFolderDialog(); })) {
            setCourseLayout(false);
        }
    });
    buttons.push_back(courseButton);
}

void Game::applyViews(unsigned int width, unsigned int height, float scale) {
    boardScale = scale;
    uiView = sf::View(sf::FloatRect(0.0f, 0.0f,
                                    static_cast<float>(width), static_cast<float>(height)));

    // The board keeps drawing itself in 0..352 coordinates; the viewport says
    // which part of the window that square gets stretched over.
    const float boardPx = CourseTrainer::BOARD_UNIT * scale;
    boardView = sf::View(sf::FloatRect(0.0f, 0.0f,
                                       CourseTrainer::BOARD_UNIT, CourseTrainer::BOARD_UNIT));
    boardView.setViewport(sf::FloatRect(0.0f, 0.0f,
                                        boardPx / static_cast<float>(width),
                                        boardPx / static_cast<float>(height)));
    window->setView(uiView);

    // The board draws its own coordinate labels and needs the same figure to
    // rasterise them sharply.
    if (board) board->setRenderScale(scale);
}

void Game::setCourseLayout(bool on) {
    CourseTrainer::Layout layout;   // defaults describe the normal 1:1 geometry
    float scale = 1.0f;

    if (on) {
        // Pick the largest board that still leaves room for the panels on this
        // screen: the right panel wants ~480px, the move tree under the board
        // ~200px, and we stay clear of the taskbar.
        const sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
        const float availW = desktop.width * 0.92f;
        const float availH = desktop.height * 0.88f;

        static const float kScales[] = { 2.0f, 1.75f, 1.5f, 1.25f, 1.0f };
        float chosenW = 1100.0f;
        float chosenH = 700.0f;
        for (float candidate : kScales) {
            const float boardPx = CourseTrainer::BOARD_UNIT * candidate;
            const float w = boardPx + layout.evalBarW + 480.0f;
            const float h = boardPx + 200.0f;
            if (w <= availW && h <= availH) {
                scale = candidate;
                chosenW = w;
                chosenH = h;
                break;
            }
        }

        layout.boardSize = CourseTrainer::BOARD_UNIT * scale;
        layout.panelX = layout.boardSize + layout.evalBarW;
        layout.windowW = chosenW;
        layout.windowH = chosenH;
        layout.panelW = chosenW - layout.panelX;
        course->setLayout(layout);
    }

    const sf::Vector2u target = on
        ? sf::Vector2u(static_cast<unsigned int>(layout.windowW),
                       static_cast<unsigned int>(layout.windowH))
        : normalWindowSize;

    if (window->getSize() != target) {
        window->setSize(target);
        centerWindow();
    }
    applyViews(target.x, target.y, on ? scale : 1.0f);

    // The eval bar sits beside the board, wherever that now ends.
    if (evalBar) {
        const int edge = static_cast<int>(on ? layout.boardSize : 352.0f);
        evalBar->setGeometry(edge, 0, static_cast<int>(layout.evalBarW), edge);
    }

    if (!on) {
        // Back to a normal game on a fresh board, so pick the engine back up.
        gameOver = false;
        currentLines.clear();
				currentLineText.clear();
        if (arrowManager) arrowManager->clearArrows();
        if (evalBar) evalBar->setEvaluation(0.0f);
        lastAnalyzedFEN.clear();
        if (engineInitialized) {
            updateAnalysis();
            lastAnalyzedFEN = board->getFEN();
        }
    }
}

std::string Game::openCourseFolderDialog() {
    // There is no plain folder picker in commdlg, so ask for any PGN inside
    // the course folder and take its parent. This one uses the wide API on
    // purpose: chapter names such as "Grunfeld" are spelt with a u-umlaut and
    // would not survive a round trip through the ANSI code page.
    OPENFILENAMEW ofn;
    wchar_t file[MAX_PATH] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window->getSystemHandle();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PGN Files (*.pgn)\0*.pgn\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Pick any chapter PGN inside the course folder";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    static std::wstring initialDir;
    initialDir.clear();
    if (userDb) {
        const std::string saved = userDb->getSetting("course_dir");
        if (!saved.empty()) initialDir = std::filesystem::u8path(saved).wstring();
    }
    ofn.lpstrInitialDir = initialDir.empty() ? NULL : initialDir.c_str();

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::filesystem::path(std::wstring(ofn.lpstrFile)).u8string();
    }
    return "";
}

void Game::updateButtons() {
	sf::Vector2i mousePos = sf::Mouse::getPosition(*window);
	// The whole column is hidden in course mode, so there is nothing to update
	// until Exit Course brings it back.
	if (course && course->isActive()) return;

	const bool bulkRunning = bulkAnalysisActive.load();
	for (auto btn : buttons) {
		btn->setEnabled(btn != analyzePgnButton || !bulkRunning);
		btn->update(mousePos);
	}
}

void Game::renderUI() {
	// Draw UI panel background (full window height)
	sf::RectangleShape panel;
	panel.setSize(sf::Vector2f(200.0f, static_cast<float>(window->getSize().y)));
	panel.setPosition(392.0f, 0.0f);
	panel.setFillColor(sf::Color(30, 30, 30));
	window->draw(panel);

	// Render buttons
	for (auto btn : buttons) {
		btn->render();
	}

	// Turn indicator (below the last button row)
	{
		const float buttonHeight = 35.0f;
		const float spacing = 10.0f;
		const float baseY = 10.0f + (buttonHeight + spacing) * 11;
		sf::Text turnText;
		turnText.setFont(font);
		turnText.setCharacterSize(16);
		turnText.setFillColor(sf::Color(200, 200, 200));
		std::string turn = (board->getCurrentTurn() == PieceColor::WHITE) ? "White to move" : "Black to move";
		turnText.setString("Turn: " + turn);
		turnText.setPosition(400.0f, baseY);
		window->draw(turnText);

		// Puzzle rating
		if (userDb) {
			sf::Text ratingText;
			ratingText.setFont(font);
			ratingText.setCharacterSize(16);
			ratingText.setFillColor(sf::Color(200, 200, 200));
			ratingText.setString("Puzzle Rating: " + std::to_string(userDb->getRating()));
			ratingText.setPosition(400.0f, baseY + 20.0f);
			window->draw(ratingText);
		}

		// Render status message (fades after 3 seconds) just below turn indicator
		if (!statusMessage.empty() && statusClock.getElapsedTime().asSeconds() < 3.0f) {
			sf::Text statusText;
			statusText.setFont(font);
			statusText.setString(statusMessage);
			statusText.setCharacterSize(18);
			statusText.setFillColor(sf::Color::Green);
			statusText.setPosition(400.0f, baseY + 30.0f);
			window->draw(statusText);
		}

		// Keyboard shortcuts help text below status
		float helpY = baseY + 55.0f;
		sf::Text helpText;
		helpText.setFont(font);
		helpText.setCharacterSize(14);
		helpText.setFillColor(sf::Color(150, 150, 150));

		helpText.setString("Keyboard Shortcuts:");
		helpText.setPosition(400.0f, helpY);
		window->draw(helpText);

		// E key - Eval bar toggle
		helpY += 25.0f;
		helpText.setCharacterSize(12);
		std::string evalStatus = evalBar && evalBar->getVisible() ? "ON" : "OFF";
		sf::Color evalColor = evalBar && evalBar->getVisible() ? sf::Color::Green : sf::Color::Red;
		helpText.setString("E - Eval Bar");
		helpText.setFillColor(sf::Color(180, 180, 180));
		helpText.setPosition(400.0f, helpY);
		window->draw(helpText);

		sf::Text evalIndicator;
		evalIndicator.setFont(font);
		evalIndicator.setCharacterSize(12);
		evalIndicator.setString(evalStatus);
		evalIndicator.setFillColor(evalColor);
		evalIndicator.setStyle(sf::Text::Bold);
		evalIndicator.setPosition(520.0f, helpY);
		window->draw(evalIndicator);

		// A key - Arrows toggle
		helpY += 20.0f;
		std::string arrowStatus = arrowManager && arrowManager->getVisible() ? "ON" : "OFF";
		sf::Color arrowColor = arrowManager && arrowManager->getVisible() ? sf::Color::Green : sf::Color::Red;
		helpText.setString("A - Arrows");
		helpText.setFillColor(sf::Color(180, 180, 180));
		helpText.setPosition(400.0f, helpY);
		window->draw(helpText);

		sf::Text arrowIndicator;
		arrowIndicator.setFont(font);
		arrowIndicator.setCharacterSize(12);
		arrowIndicator.setString(arrowStatus);
		arrowIndicator.setFillColor(arrowColor);
		arrowIndicator.setStyle(sf::Text::Bold);
		arrowIndicator.setPosition(520.0f, helpY);
		window->draw(arrowIndicator);
	}

{
	BulkProgress progressSnapshot;
	std::string messageCopy;
	bool active = bulkAnalysisActive.load();
	{
		std::lock_guard<std::mutex> lock(bulkProgressMutex);
		progressSnapshot = bulkProgress;
		messageCopy = bulkMessage;
	}
	if (!messageCopy.empty() && bulkMessageClock.getElapsedTime().asSeconds() > 8.0f) {
		std::lock_guard<std::mutex> lock(bulkProgressMutex);
		if (bulkMessageClock.getElapsedTime().asSeconds() > 8.0f) {
			bulkMessage.clear();
			messageCopy.clear();
		}
	}
	if (active || !messageCopy.empty()) {
		const float progressX = 392.0f + 10.0f;
		// Pinned to the bottom of the panel: the button column now reaches
		// far enough down that a fixed offset would land on the help text.
		const float progressY = static_cast<float>(window->getSize().y) - 46.0f;
		const float barWidth = 180.0f;
		const float barHeight = 14.0f;

		float ratio = 0.0f;
		if (progressSnapshot.positionCount > 0) {
			ratio = static_cast<float>(progressSnapshot.positionIndex) /
			        static_cast<float>(progressSnapshot.positionCount);
			if (ratio < 0.0f) ratio = 0.0f; else if (ratio > 1.0f) ratio = 1.0f;
		}

		sf::RectangleShape bg(sf::Vector2f(barWidth, barHeight));
		bg.setPosition(progressX, progressY);
		bg.setFillColor(sf::Color(45, 45, 45));
		bg.setOutlineColor(sf::Color(90, 90, 90));
		bg.setOutlineThickness(1.0f);
		window->draw(bg);

		sf::RectangleShape fill(sf::Vector2f(barWidth * ratio, barHeight));
		fill.setPosition(progressX, progressY);
		fill.setFillColor(sf::Color(120, 180, 250));
		window->draw(fill);

		sf::Text progressText;
		progressText.setFont(font);
		progressText.setCharacterSize(12);
		progressText.setFillColor(sf::Color(200, 200, 200));
		std::ostringstream statusLine;
		if (!progressSnapshot.label.empty() && active) {
			statusLine << progressSnapshot.label << "  ";
		}
		if (progressSnapshot.positionCount > 0) {
			statusLine << progressSnapshot.positionIndex << "/" << progressSnapshot.positionCount;
		} else if (active) {
			statusLine << "Preparing...";
		}
		progressText.setString(statusLine.str());
		progressText.setPosition(progressX, progressY - 18.0f);
		window->draw(progressText);

		if (!messageCopy.empty()) {
			sf::Text msg;
			msg.setFont(font);
			msg.setCharacterSize(12);
			msg.setFillColor(sf::Color(210, 210, 210));
			msg.setString(messageCopy);
			msg.setPosition(progressX, progressY + barHeight + 6.0f);
			window->draw(msg);
		}
	}
}
}

void Game::setStatusMessage(const std::string& message) {
	statusMessage = message;
	statusClock.restart();
}

// --- Puzzle helpers ---
static std::vector<std::string> csvSplitLine(const std::string& line) {
    std::vector<std::string> fields; std::string cur; bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') inQuotes = !inQuotes;
        else if (c == ',' && !inQuotes) { fields.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    fields.push_back(cur);
    // Strip quotes for each field if present
    for (auto& f : fields) {
        if (f.size() >= 2 && f.front() == '"' && f.back() == '"') {
            f = f.substr(1, f.size()-2);
        }
    }
    return fields;
}

static std::vector<std::string> splitUciMoves(const std::string& moves) {
    std::vector<std::string> out; std::istringstream iss(moves); std::string m; while (iss >> m) out.push_back(m); return out;
}

bool Game::loadRandomPuzzle() {
    if (puzzleFilePath.empty()) return false;
    // Ensure headers are loaded
    if (puzzleHeaders.empty()) {
        std::ifstream hf(puzzleFilePath);
        if (hf.is_open()) {
            std::string header;
            if (std::getline(hf, header)) {
                puzzleHeaders = csvSplitLine(header);
            }
            hf.close();
        }
    }

    std::ifstream f(puzzleFilePath, std::ios::in | std::ios::binary);
    if (!f.is_open()) return false;

    // Get file size
    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    if (fileSize <= 0) { f.close(); return false; }
    // Working variable for selected CSV line
    std::string chosen;

        // If there is a pending review puzzle, serve it first
    if (userDb && !userDb->getReviewQueue().empty()) {
        // helper to lowercase strings
        auto toLower = [](std::string s){ for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
        std::string reviewId = userDb->getReviewQueue().front();
        // Scan file to find exact line by PuzzleId
        std::ifstream rf(puzzleFilePath);
        if (rf.is_open()) {
            std::string header; std::getline(rf, header);
            std::string line;
            while (std::getline(rf, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto fields2 = csvSplitLine(line);
                int idIdx = -1; // find PuzzleId column index (cached below)
                for (size_t i = 0; i < puzzleHeaders.size(); ++i) { if (toLower(puzzleHeaders[i]) == "puzzleid") { idIdx = (int)i; break; } }
                if (idIdx >= 0 && (size_t)idIdx < fields2.size() && fields2[idIdx] == reviewId) {
                    chosen = line;
                    // Pop served review item
                    if (userDb) { std::string tmp; userDb->popReview(tmp); userDb->save(); }
                    break;
                }
            }
            rf.close();
        }
        if (chosen.empty()) {
            // If not found, pop it from review to avoid infinite loop
            std::string tmp; if (userDb->popReview(tmp)) userDb->save();
        }
    }

    // Use rating window around current rating if not chosen via review queue
    int targetRating = userDb ? userDb->getRating() : 1500;
    int window = 100;
    for (int attempt = 0; attempt < 200 && chosen.empty(); ++attempt) {
        std::streamoff offset = static_cast<std::streamoff>(rand()) % fileSize;
        f.seekg(offset, std::ios::beg);
        if (offset > 0) { std::string dummy; std::getline(f, dummy); }
        std::string line;
        if (!std::getline(f, line)) { f.clear(); f.seekg(0, std::ios::beg); std::getline(f, line); }
        if (line.rfind("PuzzleId,", 0) == 0) { if (!std::getline(f, line)) { f.clear(); f.seekg(0, std::ios::beg); std::getline(f, line); std::getline(f, line); } }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto fieldsTry = csvSplitLine(line);
        int idIdx = -1, ratingIdx = -1;
        for (size_t i = 0; i < puzzleHeaders.size(); ++i) {
            std::string low = puzzleHeaders[i]; for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (low == "puzzleid") idIdx = (int)i;
            else if (low == "rating") ratingIdx = (int)i;
        }
        if (ratingIdx >= 0 && (size_t)ratingIdx < fieldsTry.size()) {
            int r = 0; try { r = std::stoi(fieldsTry[ratingIdx]); } catch (...) { r = targetRating; }
            if (r >= targetRating - window && r <= targetRating + window) {
                chosen = line;
                break;
            }
        }
    }

    f.close();
    if (chosen.empty()) return false;

    std::vector<std::string> fields = csvSplitLine(chosen);
    if (fields.size() < 3) return false;

    // Build metadata KVs for display
    puzzleMetaKVs.clear();
    const size_t count = std::min(puzzleHeaders.size(), fields.size());
    for (size_t i = 0; i < count; ++i) {
        puzzleMetaKVs.emplace_back(puzzleHeaders[i], fields[i]);
    }

    // Find FEN and Moves columns by name when possible
    auto toLower = [](std::string s){ for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
    auto findIndex = [&](const std::string& name) -> int {
        std::string needle = toLower(name);
        for (size_t i = 0; i < puzzleHeaders.size(); ++i) {
            if (toLower(puzzleHeaders[i]) == needle) return (int)i;
        }
        return -1;
    };
    int fenIdx = findIndex("FEN");
    int movesIdx = findIndex("Moves");
    int idIdx = findIndex("PuzzleId");
    if (fenIdx < 0) fenIdx = 1; // fallback
    if (movesIdx < 0) movesIdx = 2; // fallback

    std::string fen = (fenIdx >= 0 && (size_t)fenIdx < fields.size()) ? fields[fenIdx] : std::string();
    std::string moves = (movesIdx >= 0 && (size_t)movesIdx < fields.size()) ? fields[movesIdx] : std::string();
    if (idIdx >= 0 && (size_t)idIdx < fields.size()) currentPuzzleId = fields[idIdx];
    if (!board->setFEN(fen)) return false;
    // Save baseline puzzle position and first move to allow returning after side-lines
    puzzleStartFEN = fen;
    puzzleMoves = splitUciMoves(moves);
    puzzleIndex = 0;
    lastAnalyzedFEN = board->getFEN();

    // Play the first move from the CSV to show the opponent's last/first move,
    // then let the user solve from the other side.
    if (!puzzleMoves.empty()) {
        const std::string& first = puzzleMoves[0];
        puzzleFirstMove = first;
        board->setNextProgrammaticAnimation(1500.0f, 2000.0f);
        if (board->applyUCIMove(first)) {
            puzzleIndex = 1; // next expected move is user's response
            std::string turn = (board->getCurrentTurn() == PieceColor::WHITE) ? "White" : "Black";
            setStatusMessage("Puzzle start: Opponent played " + first + ". " + turn + " to move");
        } else {
            setStatusMessage("Could not apply first puzzle move");
        }
    }
    return true;
}

void Game::renderAnalysisPanel() {
    // In puzzle mode, show metadata; optionally overlay analysis at the top when enabled
    if (puzzleMode) {
        renderPuzzleMetadataPanel();
        if (!engineInitialized || currentLines.empty() || !puzzleAnalysisEnabled) return;

        // Draw an opaque overlay at the top of the metadata area for the 3 best lines
        const float overlayHeight = 98.0f;
        sf::RectangleShape overlay;
        overlay.setSize(sf::Vector2f(352.0f, overlayHeight));
        overlay.setPosition(0.0f, 352.0f);
        overlay.setFillColor(sf::Color(20, 20, 20, 240));
        window->draw(overlay);

        float yOffset = 360.0f;
        for (size_t i = 0; i < currentLineText.size() && i < 3; i++) {
            sf::CircleShape indicator(8.0f);
            indicator.setPosition(8.0f, yOffset + 4.0f);
            if (i == 0) indicator.setFillColor(sf::Color(0, 200, 0));
            else if (i == 1) indicator.setFillColor(sf::Color(200, 200, 0));
            else indicator.setFillColor(sf::Color(255, 165, 0));
            window->draw(indicator);

            sf::Text evalText;
            evalText.setFont(font);
            evalText.setCharacterSize(14);
            evalText.setFillColor(sf::Color::White);

            const CourseTrainer::EngineReadout& text = currentLineText[i];
            evalText.setString(text.eval);
            evalText.setPosition(25.0f, yOffset);
            window->draw(evalText);

            sf::Text movesText;
            movesText.setFont(font);
            movesText.setCharacterSize(13);
            movesText.setFillColor(sf::Color(180, 180, 180));
            movesText.setString(text.pv);
            movesText.setPosition(80.0f, yOffset);
            window->draw(movesText);

            yOffset += 30.0f;
        }
        return;
    }

    if (!engineInitialized || currentLines.empty()) return;

    // Analysis panel background - below the board (normal mode)
    sf::RectangleShape panel;
    panel.setSize(sf::Vector2f(352.0f, 98.0f));
    panel.setPosition(0.0f, 352.0f);
    panel.setFillColor(sf::Color(20, 20, 20));
    window->draw(panel);

    // Render each line
    float yOffset = 360.0f;
    for (size_t i = 0; i < currentLineText.size() && i < 3; i++) {
        sf::CircleShape indicator(8.0f);
        indicator.setPosition(8.0f, yOffset + 4.0f);
        if (i == 0) indicator.setFillColor(sf::Color(0, 200, 0));
        else if (i == 1) indicator.setFillColor(sf::Color(200, 200, 0));
        else indicator.setFillColor(sf::Color(255, 165, 0));
        window->draw(indicator);

        sf::Text evalText;
        evalText.setFont(font);
        evalText.setCharacterSize(14);
        evalText.setFillColor(sf::Color::White);

        const CourseTrainer::EngineReadout& text = currentLineText[i];
        evalText.setString(text.eval);
        evalText.setPosition(25.0f, yOffset);
        window->draw(evalText);

        sf::Text movesText;
        movesText.setFont(font);
        movesText.setCharacterSize(13);
        movesText.setFillColor(sf::Color(180, 180, 180));
        movesText.setString(text.pv);
        movesText.setPosition(80.0f, yOffset);
        window->draw(movesText);

        yOffset += 30.0f;
    }
}

std::string Game::openFileDialog() {
	OPENFILENAMEA ofn;
	char szFile[260] = { 0 };

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = window->getSystemHandle();
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "PGN Files (*.pgn)\0*.pgn\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileNameA(&ofn) == TRUE) {
		return std::string(ofn.lpstrFile);
	}
	return "";
}

void Game::renderPuzzleMetadataPanel() {
    // Panel below the board (same area as analysis panel)
    sf::RectangleShape panel;
    panel.setSize(sf::Vector2f(352.0f, std::max(0.0f, static_cast<float>(window->getSize().y) - 352.0f)));
    panel.setPosition(0.0f, 352.0f);
    panel.setFillColor(sf::Color(20, 20, 20));
    window->draw(panel);

    // Title
    sf::Text title;
    title.setFont(font);
    title.setString("Puzzle Info");
    title.setCharacterSize(14);
    title.setFillColor(sf::Color(255, 255, 255));
    title.setPosition(8.0f, 352.0f + 4.0f);
    window->draw(title);

    // List all metadata as key: value, wrapping columns if needed
    float x = 8.0f;
    float y = 352.0f + 22.0f;
    float maxWidth = 336.0f; // panel width - padding
    float lineHeight = 16.0f;

    sf::Text kv;
    kv.setFont(font);
    kv.setCharacterSize(12);
    kv.setFillColor(sf::Color(200, 200, 200));

    // Combine into a single multiline string, but wrap when exceeding width
    std::string lineAccum;
    auto flushLine = [&]() {
        if (lineAccum.empty()) return;
        kv.setString(lineAccum);
        // If too wide, break at last '; '
        if (kv.getLocalBounds().width > maxWidth) {
            // naive trim: shrink until fits
            while (!lineAccum.empty() && kv.getLocalBounds().width > maxWidth) {
                lineAccum.pop_back();
                kv.setString(lineAccum + "...");
            }
        }
        kv.setString(lineAccum);
        kv.setPosition(x, y);
        window->draw(kv);
        y += lineHeight;
        lineAccum.clear();
    };

    for (size_t i = 0; i < puzzleMetaKVs.size(); ++i) {
        const auto& kvp = puzzleMetaKVs[i];
        std::string entry = kvp.first + ": " + kvp.second;
        // Truncate very long values (like Themes) to keep within panel
        if (entry.size() > 120) entry = entry.substr(0, 117) + "...";

        if (lineAccum.empty()) lineAccum = entry;
        else lineAccum += "    |    " + entry;

        // If predicted width is too big, flush current and start new line
        kv.setString(lineAccum);
        if (kv.getLocalBounds().width > maxWidth) {
            // Remove last added and flush
            size_t cut = lineAccum.rfind("    |    ");
            if (cut != std::string::npos) {
                std::string toDraw = lineAccum.substr(0, cut);
                kv.setString(toDraw);
                kv.setPosition(x, y);
                window->draw(kv);
                y += lineHeight;
                lineAccum = entry;
            } else {
                flushLine();
            }
        }
        // Stop if panel vertical space exhausted
        if (y + lineHeight > (static_cast<float>(window->getSize().y) - 4.0f)) break;
    }
    flushLine();
}

std::string Game::saveFileDialog() {
	OPENFILENAMEA ofn;
	char szFile[260] = "game.pgn";

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = window->getSystemHandle();
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "PGN Files (*.pgn)\0*.pgn\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrDefExt = "pgn";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

	if (GetSaveFileNameA(&ofn) == TRUE) {
		return std::string(ofn.lpstrFile);
	}
	return "";
}

std::string Game::openCSVFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window->getSystemHandle();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    // Start in last-used directory if available; otherwise suggested folder
    static std::string initialDir;
    initialDir.clear();
    if (userDb && !userDb->getLastCsvPath().empty()) {
        const std::string& last = userDb->getLastCsvPath();
        size_t pos = last.find_last_of("/\\");
        if (pos != std::string::npos) {
            initialDir = last.substr(0, pos);
        }
    }
    ofn.lpstrInitialDir = initialDir.empty() ? "puzzel_lichess" : initialDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}
















void Game::startBulkAnalysis(const std::string& filePath) {
    if (bulkAnalysisActive.load()) {
        setStatusMessage("Analysis already running");
        return;
    }

    if (bulkAnalysisThread.joinable()) {
        bulkAnalysisCancel.store(true);
        bulkAnalysisThread.join();
    }

    bulkAnalysisCancel.store(false);
    bulkAnalysisActive.store(true);
    {
        std::lock_guard<std::mutex> lock(bulkProgressMutex);
        bulkProgress = BulkProgress{};
        bulkProgress.label = std::filesystem::path(filePath).filename().string();
        bulkOutputPath.clear();
        bulkMessage.clear();
        bulkAnalysisCompleted = false;
    }
    bulkMessageClock.restart();
    setStatusMessage("Analyzing PGN...");

    bulkAnalysisThread = std::thread([this, filePath]() {
        analysis::PgnBulkAnalyzer analyzer;
        analysis::AnalyzerOptions opts;
        opts.analysisDepth = 16;
        opts.minCentipawnSwing = 150;
        opts.maxPrincipalVariation = 6;
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 2;
        opts.engineThreads = static_cast<int>(std::max(1u, hw / 2));

        std::string outputPath;
        try {
            std::filesystem::path input(filePath);
            std::filesystem::path parent = input.parent_path();
            std::string stem = input.stem().string();
            if (stem.empty()) stem = "analysis";
            outputPath = (parent / (stem + "_tactics.csv")).string();
        } catch (...) {
            outputPath = filePath + "_tactics.csv";
        }

        auto progressCb = [this](const analysis::AnalyzerProgress& prog) {
            std::lock_guard<std::mutex> lock(bulkProgressMutex);
            bulkProgress.gameIndex = prog.gameIndex;
            bulkProgress.gameCount = prog.gameCount;
            bulkProgress.positionIndex = prog.positionIndex;
            bulkProgress.positionCount = prog.positionCount;
            bulkProgress.label = prog.currentLabel;
        };

        std::string error;
        bool ok = analyzer.analyzeFile(filePath, outputPath, opts, progressCb, bulkAnalysisCancel, error);

        {
            std::lock_guard<std::mutex> lock(bulkProgressMutex);
            bulkAnalysisCompleted = true;
            if (bulkAnalysisCancel.load()) {
                bulkMessage = "Analysis cancelled";
            } else if (ok) {
                bulkOutputPath = outputPath;
                bulkMessage = "Analysis complete: " + outputPath;
            } else {
                bulkMessage = error.empty() ? "Analysis failed" : error;
            }
            bulkMessageClock.restart();
        }

        bulkAnalysisActive.store(false);
    });
}
