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
#include "Pawn.h"
#include "Piece.h"


#define STATE_INITIAL 0
#define STATE_PIECE_CLICKED 1
#define STATE_PIECE_RELEASED 2

Game::Game(){
	this->initWindow();
	//this->myPiece=new Piece(this->window);
	

//	currentState=initial;

	pMyPawn=new Pawn(this->window,this->window);
	myPiece=pMyPawn;
	//initial state
		myPiece->setCurrentState(STATE_INITIAL);	

}

Game::~Game(){


}

void Game::run(){
	while (this->window->isOpen()) {
		//clear screen
		std::cout << "\033[2J\033[1;1H";
		//std::system("clear");
		this->printMousePosition();
		this->updateDt();
		this->processEvents();
		this->render();
	}

}

void Game::render(){
	this->window->clear();
	this->myPiece->render();
	if (remainWithThisColor) {	
		this->renderText();
	}
	else {
		this->renderText(sf::Color::Red);
	}
	if(printSecTextLine){
		this->renderText(sf::Color::Red,42,startingPosition+endPosition);
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
		if(event.type == sf::Event::MouseButtonPressed)
		{
		myPiece->setCurrentState(STATE_PIECE_CLICKED );	
		//userWantedString is the startingPosition without ->
			myPiece->clickedCoardinate(userWantedString);
			startingPosition=userWantedString+"->";	
			remainWithThisColor=false;
		}
		if(event.type == sf::Event::MouseButtonReleased)
		{
			endPosition=userWantedString;
			remainWithThisColor=true;
			printSecTextLine=true;
	//	myPiece->setCurrentState(STATE_INITIAL);	
	
		myPiece->releasedCoardinate(endPosition);
		myPiece->setCurrentState(STATE_PIECE_RELEASED);	

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

	font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans-ExtraLight.ttf");

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



