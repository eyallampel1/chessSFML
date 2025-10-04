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

Game::Game(){
	this->initWindow();
	board = new Board(this->window);

	// Initialize engine components
	engine = new StockfishEngine();
	evalBar = new EvalBar(this->window, &font, 352, 0, 40, 352);  // Moved to right of board
	arrowManager = new ArrowManager(this->window);
	engineInitialized = false;
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

				// Update eval bar
				evalBar->setEvaluation(lines[0].score);

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

	this->window->display();
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

			// Check button clicks first
			bool buttonClicked = false;
			for (auto btn : buttons) {
				if (btn->contains(mousePos)) {
					btn->handleClick(mousePos);
					buttonClicked = true;
					break;
				}
			}

			// Only process board clicks if no button was clicked
			if (!buttonClicked) {
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
				endPosition=userWantedString;
				board->handleRelease(endPosition);
				remainWithThisColor=true;
				printSecTextLine=true;

				// Trigger analysis update after move (non-blocking)
				std::string currentFEN = board->getFEN();
				if (currentFEN != lastAnalyzedFEN) {
					updateAnalysis();
					lastAnalyzedFEN = currentFEN;
				}
			}
		}
	}
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
	if (!engineInitialized) return;

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

	// Render status message (fades after 3 seconds)
	if (!statusMessage.empty() && statusClock.getElapsedTime().asSeconds() < 3.0f) {
		sf::Text statusText;
		statusText.setFont(font);
		statusText.setString(statusMessage);
		statusText.setCharacterSize(18);
		statusText.setFillColor(sf::Color::Green);
		statusText.setPosition(400.0f, 180.0f);
		window->draw(statusText);
	}
}

void Game::setStatusMessage(const std::string& message) {
	statusMessage = message;
	statusClock.restart();
}

void Game::renderAnalysisPanel() {
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
		if (std::abs(displayScore) >= 10000) {
			evalStr << "M" << (displayScore > 0 ? "+" : "-");
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



