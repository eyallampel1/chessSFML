#ifndef PIECE_H
#define PIECE_H
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Mouse.hpp>
#include <string>
#include <vector>
#include <iostream>
#include "checkIfLegal.h"
#include "Coordinate.h"

//rank == row
//file == column
#define PAWN 1.0f
#define ROOK 5.0f
class Piece
{
	private:
		int sourceRank,destRank;
		char sourceFile,destFile;
	protected:
		sf::RenderTarget* target;	
		sf::RenderWindow* window;	


		//textures
		sf::Texture chessBoardTexture;
		sf::Sprite  chessBoardSprite;	
		sf::Texture chessPiecesTexture;
		sf::Sprite  chessPiecesSprite;

		//sprite stuff

		Coordinate spriteCoordinate;
		sf::IntRect rectSprite;
		sf::Vector2u imageCount,currentImage;
		std::string clickedCoordinate;
		std::string releasedCoordinate;
		sf::Vector2i position;
		enum state{initial,pieceClicked,pieceReleased};
		state currentState;

	public:
		virtual void render()=0 ;
		//virtual void update() = 0;
		Piece(sf::RenderTarget* target,
				sf::RenderWindow* window);
		virtual ~Piece();
		void clickedCoardinate(std::string clickedCoordinate);
		void releasedCoardinate(std::string releasedCoordinate);
		void loadTexture();
		void setCurrentState(int numState);

		bool coordinateIsLegal(float Piece,const char* sourceCoordinate,
				const char* destCoordinate);
};

#endif /* PIECE_H */
