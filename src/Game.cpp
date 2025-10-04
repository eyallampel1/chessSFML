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

Game::Game(){
	this->initWindow();
	board = new Board(this->window);

	// Initialize engine components
	engine = new StockfishEngine();
	evalBar = new EvalBar(this->window, &font, 360, 0, 40, 352);
	arrowManager = new ArrowManager(this->window);
	engineInitialized = false;
	analysisRequested = false;
	lastAnalyzedFEN = "";

	// Initialize engine in background
	initEngine();
}

Game::~Game(){
	delete board;
	delete engine;
	delete evalBar;
	delete arrowManager;
}

void Game::run(){
	while (this->window->isOpen()) {
		//clear screen
		std::cout << "\033[2J\033[1;1H";
		//std::system("clear");
		this->printMousePosition();
		this->updateDt();
		this->processEvents();

		// Update hovered square continuously while mouse is moved
		board->updateHoveredSquare(userWantedString);

		// Check for analysis updates periodically (non-blocking)
		if (engineInitialized && analysisRequested && analysisClock.getElapsedTime().asMilliseconds() > 500) {
			// Get best lines without blocking
			auto lines = engine->getBestLines(3);

			if (!lines.empty()) {
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

	this->window->display();
}


void Game::updateDt(){
	dt=dtClock.restart().asSeconds();
	std::cout << dt << std::endl;
}

void Game::centerWindow(){


	desktop = sf::VideoMode::getDesktopMode();
	getWindow.x=desktop.width/2 - window->getSize().x/2;
	getWindow.y=desktop.height/2-window->getSize().y/2;
	this->window->setPosition(getWindow);


}


void Game::initWindow(){
	// Increased width to accommodate eval bar (352 + 40 + 82 = 474)
	this->window=new sf::RenderWindow(sf::VideoMode(474,352),
			"lampel",sf::Style::Titlebar|sf::Style::Close);
	this->centerWindow();
	this->window->setFramerateLimit(120);
	this->window->setVerticalSyncEnabled(true);

}

void Game::printMousePosition(){
	position = sf::Mouse::getPosition(*window);
	std::cout<<"mouse position x="<<position.x<<std::endl;
	std::cout<<"mouse position y="<<position.y<<std::endl;
	convertMousePositionToCordinate();
}

void Game::processEvents(){
	std::cout<< "inside process Events" << std::endl;
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
			std::cout << "A" <<8-i<< std::endl;
			userWantedString="A"+std::to_string(8-i);
		}

		if(position.x>45 && position.x<45*2 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "B" <<8-i<< std::endl;
			userWantedString="B"+std::to_string(8-i);
		}
		if(position.x>45*2 && position.x<45*3 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "C" <<8-i<< std::endl;
			userWantedString="C"+std::to_string(8-i);
		}
		if(position.x>45*3 && position.x<45*4 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "D" <<8-i<< std::endl;
			userWantedString="D"+std::to_string(8-i);
		}
		if(position.x>45*4 && position.x<45*5 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "E" <<8-i<< std::endl;
			userWantedString="E"+std::to_string(8-i);
		}
		if(position.x>45*5 && position.x<45*6 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "F" <<8-i<< std::endl;
			userWantedString="F"+std::to_string(8-i);
		}
		if(position.x>44*6 && position.x<44*7 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "G" <<8-i<< std::endl;
			userWantedString="G"+std::to_string(8-i);
		}
		if(position.x>44*7 && position.x<44*8 && position.y >i*45 && position.y<(i+1)*45)
		{
			std::cout << "H" <<8-i<< std::endl;
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
	text.setPosition(360,yPosition);
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



