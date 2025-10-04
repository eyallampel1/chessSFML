#include "Game.h"
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
#include <windows.h>
#include <commdlg.h>
#include <cctype>

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

	// Initialize UI buttons
	initButtons();

	// Initialize status message
	statusMessage = "";
}

Game::~Game(){
	delete board;
	delete engine;
	delete evalBar;
	delete arrowManager;

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

		// Update hovered square continuously while mouse is moved
		board->updateHoveredSquare(userWantedString);

		// Update buttons
		updateButtons();

		// Check for analysis updates periodically (non-blocking)
		if (engineInitialized && analysisRequested && analysisClock.getElapsedTime().asMilliseconds() > 500) {
			// Get best lines without blocking
			auto lines = engine->getBestLines(3);

			if (!lines.empty()) {
				// Store lines for display
				currentLines = lines;

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
							setStatusMessage("Checkmate — game over");
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
			}

			analysisRequested = false;
		}

		this->render();
	}

}

void Game::render(){
	this->window->clear();
	this->board->render();

	// Render arrows (behind pieces)
	if (arrowManager) {
		arrowManager->render();
	}

	// Update gameOver flag from board checkmate
	if (board->getIsCheckmate()) {
		if (!gameOver) {
			// Stop analysis once when mate detected
			if (engineInitialized) {
				engine->stopAnalysis();
				analysisRequested = false;
			}
			gameOver = true;
		}
	}

    // Persistent checkmate banner over the board area
    if (gameOver) {
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
	// Window size: 352 (board) + 40 (eval bar) + 200 (UI panel) = 592 width, 450 height for buttons
	this->window=new sf::RenderWindow(sf::VideoMode(592, 450),
			"Chess - Stockfish Analysis",sf::Style::Titlebar|sf::Style::Close);
	this->centerWindow();
	this->window->setFramerateLimit(120);
	this->window->setVerticalSyncEnabled(true);

}

void Game::printMousePosition(){
	position = sf::Mouse::getPosition(*window);
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
			handleKeyboard(event.key);
		}

        if(event.type == sf::Event::MouseButtonPressed)
        {
            sf::Vector2i mousePos = sf::Mouse::getPosition(*window);

            // If promotion dialog open, only handle promotion clicks
            if (promotionOpen && event.mouseButton.button == sf::Mouse::Left) {
                // Map click to one of the 4 options
                float btnY = 158.0f;
                for (int i = 0; i < 4; ++i) {
                    float btnX = 86.0f + i * 50.0f;
                    sf::FloatRect rect(btnX, btnY, 44.0f, 38.0f);
                    if (rect.contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y))) {
                        PieceType pt = PieceType::QUEEN;
                        if (i == 1) pt = PieceType::ROOK;
                        else if (i == 2) pt = PieceType::BISHOP;
                        else if (i == 3) pt = PieceType::KNIGHT;
                        if (board->setLastMovePromotion(pt)) {
                            promotionOpen = false;
                            // After promotion selection, run puzzle validation if active
                            if (puzzleMode && puzzleIndex < puzzleMoves.size()) {
                                std::string last = board->getLastMoveUCI();
                                if (last == puzzleMoves[puzzleIndex]) {
                                    puzzleIndex++;
                                    setStatusMessage("Correct!");
                                    if (puzzleIndex >= puzzleMoves.size()) {
                                        puzzleSolved = true;
                                        puzzleSolvedClock.restart();
                                        setStatusMessage("Puzzle solved! Great job");
                                    } else {
                                        const std::string& reply = puzzleMoves[puzzleIndex];
                                        if (board->applyUCIMove(reply)) {
                                            puzzleIndex++;
                                            std::string turn = (board->getCurrentTurn() == PieceColor::WHITE) ? "White" : "Black";
                                            setStatusMessage("Reply played. " + turn + " to move");
                                        }
                                    }
                                } else {
                                    setStatusMessage("Incorrect move");
                                    board->undoLastMove();
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

            		// Puzzle validation: only if a new move was actually made and not waiting for promotion
            		if (puzzleMode && !openedPromotion) {
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
									setStatusMessage("Puzzle solved! Great job");
								} else {
									// Auto-play opponent reply if any
									const std::string& reply = puzzleMoves[puzzleIndex];
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
								setStatusMessage("Incorrect move");
								board->undoLastMove();
							}
						}
					}
				}
				remainWithThisColor=true;
				printSecTextLine=true;

				// Trigger analysis update after move (non-blocking)
				std::string currentFEN = board->getFEN();
				if (currentFEN != lastAnalyzedFEN) {
					if (!puzzleMode) updateAnalysis();
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

    sf::Text title;
    title.setFont(font);
    title.setString("Promote to:");
    title.setCharacterSize(18);
    title.setFillColor(sf::Color::White);
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

        sf::Text lbl;
        lbl.setFont(font);
        lbl.setCharacterSize(12);
        lbl.setFillColor(sf::Color::White);
        lbl.setString(labels[i]);
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

	// Try to initialize engine (path relative to executable)
	if (engine->initialize("engines/stockfish.exe")) {
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
	if (!engineInitialized || gameOver || puzzleMode) return;

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
        if (puzzleFilePath.empty()) {
            std::string path = openCSVFileDialog();
            if (path.empty()) { setStatusMessage("Puzzle load cancelled"); return; }
            if (path.size() >= 4 && path.substr(path.size()-4) == ".zst") {
                setStatusMessage("Decompress .zst to .csv and select it");
                return;
            }
            puzzleFilePath = path;
        }
        puzzleMode = true;
        puzzleSolved = false;
        arrowManager->clearArrows();
        currentLines.clear();
        gameOver = false;
        if (evalBar) evalBar->setEvaluation(0.0f);
        if (engineInitialized) {
            engine->stopAnalysis();
            analysisRequested = false;
        }
        if (loadRandomPuzzle()) setStatusMessage("Puzzle loaded");
        else setStatusMessage("Failed to load puzzle");
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
    });
    buttons.push_back(nextPuzzleButton);
}

void Game::updateButtons() {
	sf::Vector2i mousePos = sf::Mouse::getPosition(*window);
	for (auto btn : buttons) {
		btn->update(mousePos);
	}
}

void Game::renderUI() {
	// Draw UI panel background
	sf::RectangleShape panel;
	panel.setSize(sf::Vector2f(200.0f, 450.0f));
	panel.setPosition(392.0f, 0.0f);
	panel.setFillColor(sf::Color(30, 30, 30));
	window->draw(panel);

	// Render buttons
	for (auto btn : buttons) {
		btn->render();
	}

	// Turn indicator (below the last button)
	{
		sf::Text turnText;
		turnText.setFont(font);
		turnText.setCharacterSize(16);
		turnText.setFillColor(sf::Color(200, 200, 200));
		std::string turn = (board->getCurrentTurn() == PieceColor::WHITE) ? "White to move" : "Black to move";
		turnText.setString("Turn: " + turn);
		// Place just below the last button (Next Puzzle ends at ~270)
		turnText.setPosition(400.0f, 280.0f);
		window->draw(turnText);
	}

	// Render status message (fades after 3 seconds)
	if (!statusMessage.empty() && statusClock.getElapsedTime().asSeconds() < 3.0f) {
		sf::Text statusText;
		statusText.setFont(font);
		statusText.setString(statusMessage);
		statusText.setCharacterSize(18);
		statusText.setFillColor(sf::Color::Green);
		// Place below turn indicator, above help text
		statusText.setPosition(400.0f, 310.0f);
		window->draw(statusText);
	}

	// Keyboard shortcuts help text
	// Place help/tooltips below the status area to avoid overlap
	float helpY = 335.0f;
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

    // Try up to a few attempts to sample a valid line quickly
    std::string chosen;
    for (int attempt = 0; attempt < 5 && chosen.empty(); ++attempt) {
        // Random offset
        std::streamoff offset = static_cast<std::streamoff>(rand()) % fileSize;
        f.seekg(offset, std::ios::beg);

        // If not at start, skip to next newline to align to line boundary
        if (offset > 0) {
            std::string dummy;
            std::getline(f, dummy);
        }

        // Read the next non-header, non-empty line; if EOF, wrap to start
        std::string line;
        if (!std::getline(f, line)) {
            f.clear();
            f.seekg(0, std::ios::beg);
            // Skip header
            std::getline(f, line);
        }

        // If we landed on header, try the next line
        if (line.rfind("PuzzleId,", 0) == 0) {
            if (!std::getline(f, line)) {
                f.clear(); f.seekg(0, std::ios::beg);
                std::getline(f, line); // skip header again
                std::getline(f, line);
            }
        }
        // Trim trailing CR if present (Windows newlines)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) chosen = line;
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
    if (fenIdx < 0) fenIdx = 1; // fallback
    if (movesIdx < 0) movesIdx = 2; // fallback

    std::string fen = (fenIdx >= 0 && (size_t)fenIdx < fields.size()) ? fields[fenIdx] : std::string();
    std::string moves = (movesIdx >= 0 && (size_t)movesIdx < fields.size()) ? fields[movesIdx] : std::string();
    if (!board->setFEN(fen)) return false;
    puzzleMoves = splitUciMoves(moves);
    puzzleIndex = 0;
    lastAnalyzedFEN = board->getFEN();

    // Play the first move from the CSV to show the opponent's last/first move,
    // then let the user solve from the other side.
    if (!puzzleMoves.empty()) {
        const std::string& first = puzzleMoves[0];
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
    // In puzzle mode, repurpose this area to show puzzle metadata
    if (puzzleMode) {
        renderPuzzleMetadataPanel();
        return;
    }
    if (!engineInitialized || currentLines.empty()) return;

	// Analysis panel background - below the board
	sf::RectangleShape panel;
	panel.setSize(sf::Vector2f(352.0f, 98.0f));  // Board width, space below board
	panel.setPosition(0.0f, 352.0f);
	panel.setFillColor(sf::Color(20, 20, 20));
	window->draw(panel);

	// Get current turn - we'll show evaluation from their perspective
	PieceColor currentTurn = board->getCurrentTurn();

	// Render each line
	float yOffset = 360.0f;
	for (size_t i = 0; i < currentLines.size() && i < 3; i++) {
		const EngineLine& line = currentLines[i];

		// Line number indicator
		sf::CircleShape indicator(8.0f);
		indicator.setPosition(8.0f, yOffset + 4.0f);
		if (i == 0) indicator.setFillColor(sf::Color(0, 200, 0));      // Green
		else if (i == 1) indicator.setFillColor(sf::Color(200, 200, 0)); // Yellow
		else indicator.setFillColor(sf::Color(255, 165, 0));            // Orange
		window->draw(indicator);

		// Evaluation text
		sf::Text evalText;
		evalText.setFont(font);
		evalText.setCharacterSize(14);
		evalText.setFillColor(sf::Color::White);

		// Show from current player's perspective
		// Stockfish gives White's perspective, so flip if it's Black's turn
		int displayScore = line.score;
		if (currentTurn == PieceColor::BLACK) {
			displayScore = -displayScore;
		}

		std::ostringstream evalStr;
		// Prefer mate display if available
		if (line.mate != 0) {
			int moves = (std::abs(line.mate) + 1) / 2; // convert plies to moves
			bool currentMating = (currentTurn == PieceColor::WHITE) ? (line.mate > 0) : (line.mate < 0);
			if (currentMating) {
				evalStr << "M" << moves;
			} else {
				evalStr << "M-" << moves;
			}
		} else {
			float pawns = displayScore / 100.0f;
			evalStr << (pawns >= 0 ? "+" : "") << std::fixed << std::setprecision(1) << pawns;
		}

		evalText.setString(evalStr.str());
		evalText.setPosition(25.0f, yOffset);
		window->draw(evalText);

		// Move sequence (first 5 moves)
		sf::Text movesText;
		movesText.setFont(font);
		movesText.setCharacterSize(13);
		movesText.setFillColor(sf::Color(180, 180, 180));

		std::ostringstream movesStr;
		for (size_t j = 0; j < line.pv.size() && j < 5; j++) {
			std::string move = line.pv[j];
			// Convert UCI to readable (just show the move)
			if (move.length() >= 4) {
				std::string from = move.substr(0, 2);
				std::string to = move.substr(2, 2);
				from[0] = toupper(from[0]);
				to[0] = toupper(to[0]);
				movesStr << from << to;
				if (j < line.pv.size() - 1 && j < 4) movesStr << " ";
			}
		}
		movesText.setString(movesStr.str());
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
    panel.setSize(sf::Vector2f(352.0f, 98.0f));
    panel.setPosition(0.0f, 352.0f);
    panel.setFillColor(sf::Color(20, 20, 20));
    window->draw(panel);

    // Title
    sf::Text title;
    title.setFont(font);
    title.setString("Puzzle Info");
    title.setCharacterSize(14);
    title.setFillColor(sf::Color(255, 255, 255));
    title.setPosition(8.0f, 356.0f);
    window->draw(title);

    // List all metadata as key: value, wrapping columns if needed
    float x = 8.0f;
    float y = 374.0f;
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
                kv.setString(lineAccum + "…");
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
        if (entry.size() > 120) entry = entry.substr(0, 117) + "…";

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
        if (y + lineHeight > 446.0f) break;
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
    // Start in suggested puzzles directory if present
    ofn.lpstrInitialDir = "puzzel_lichess";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}



