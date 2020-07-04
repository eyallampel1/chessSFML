#ifndef GAME_H
#define GAME_H
#include "Piece.h"
#include "Pawn.h"
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/System/Clock.hpp>
#include <iostream>
#include <string>

class Game
{
	private:
		//render text stuff
		sf::Font font; 
		sf::Text text;
		std::string userWantedString;
		std::string startingPosition;
		std::string endPosition;
		bool remainWithThisColor=true;
		bool printSecTextLine=false;

		//enum state{initial,pieceClicked};
		//state currentState;

		Piece* myPiece;
		Pawn* pMyPawn;
		sf::RenderWindow* window;	
		sf::VideoMode desktop; 
		sf::Vector2i getWindow;
		sf::Event event;
		sf::Clock dtClock;
		float dt;
		sf::Vector2i position; 
	public:

		Game();
		virtual ~Game();
		void run();
		void render();
		void updateDt();
		void initWindow();
		void centerWindow();
		void processEvents();
		void printMousePosition();
		void convertMousePositionToCordinate();
		void renderText();
		void renderText(sf::Color color);
		void renderText(sf::Color color,int yPosition);
		void renderText(sf::Color color,int yPosition,std::string textToRender);

};

#endif /* GAME_H */
