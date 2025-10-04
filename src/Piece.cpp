#include "Piece.h"
#include "Coordinate.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Mouse.hpp>
#include <iostream>
#include <string>

Piece::Piece(sf::RenderTarget* target,sf::RenderWindow* window){
	std::cout<<"inside piece class";
	this->target=target;
	this->window=window;
	loadTexture();
	coordinateIsLegal(ROOK, "d2","d4" );
}

Piece::~Piece(){

}

void Piece::clickedCoardinate(std::string clickedCoordinate){
	this->clickedCoordinate=clickedCoordinate;
} 


void Piece::releasedCoardinate(std::string releasedCoordinate){
	this->releasedCoordinate=releasedCoordinate;
} 

void Piece::loadTexture(){

	if (!chessBoardTexture.loadFromFile("src/assets/chess_board.png")) {
		chessBoardTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/chess_board.png");
	}
	chessBoardSprite.setTexture(chessBoardTexture);

	if (!chessPiecesTexture.loadFromFile("src/assets/chess_sprite.png")) {
		chessPiecesTexture.loadFromFile("/home/kingl/c++Try/sfmlWithClass/src/assets/chess_sprite.png");
	}

	//total number of image in our sprite is 6 column 2 rows
	imageCount.x=6;
	imageCount.y=2;

	//calculate the height and width of a single image
	rectSprite.height=chessPiecesTexture.getSize().y/float(imageCount.y);	
	rectSprite.width=chessPiecesTexture.getSize().x/float(imageCount.x);	

	//currentImage
	currentImage.y=0;
	currentImage.x=0;
	rectSprite.left=currentImage.x*rectSprite.width;
	rectSprite.top=currentImage.y*rectSprite.height;
	

	chessPiecesSprite.setTextureRect(rectSprite);
	chessPiecesSprite.setTexture(chessPiecesTexture);

}



bool Piece::coordinateIsLegal(float Piece,const char* sourceCoordinate,
		const char* destCoordinate){
	sourceRank=*sourceCoordinate;
	sourceFile=*(sourceCoordinate+1);

	destRank=*destCoordinate;
	destFile=*(destCoordinate+1);
	return true;
}



void Piece::setCurrentState(int numState){
	if(numState==0){
		this->currentState=initial;

	}
	else if(numState==1){
		this->currentState=pieceClicked;

	}
	else if (numState==2) {
	this->currentState=pieceReleased;
	}
}


