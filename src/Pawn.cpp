#include "Pawn.h"
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Mouse.hpp>
#include <iostream>
#include <string>
#include <vector>
#include "Coordinate.h"


#define A 0
#define B 44
#define C 44*2
#define D 44*3
#define E 44*4
#define F 44*5
#define G 44*6
#define H 44*7
#define r2 263
#define r3 219

Pawn::Pawn(sf::RenderTarget* target,sf::RenderWindow* window):Piece(target,window) {
	for (int i = 2; i < 8; i++) {

		pawnNameVector.push_back("A" + std::to_string(i));
		pawnNameVector.push_back("B" + std::to_string(i));
		pawnNameVector.push_back("C" + std::to_string(i));
		pawnNameVector.push_back("D" + std::to_string(i));
		pawnNameVector.push_back("E" + std::to_string(i));
		pawnNameVector.push_back("F" + std::to_string(i));
		pawnNameVector.push_back("G" + std::to_string(i));
		pawnNameVector.push_back("H" + std::to_string(i));
	}
}

Pawn::~Pawn(){


}

void Pawn::renderCertinPawn(const std::string& pawnName){
	std::cout <<"inside renderCertinPawn:\n"<<"pawn to render:"<<pawnName<<std::endl;
	for (int i=0; i<pawnNameVector.size(); i++) {
		if(pawnName==pawnNameVector[i]){
			chessPiecesSprite.setPosition(spriteCoordinate.getX(pawnNameVector[i]),
					spriteCoordinate.getY(pawnNameVector[i]));//a3
			this->target->draw(chessPiecesSprite);
		}

	}

}
//}

//add render all pawn except
//
//
//add render all pawn

void Pawn::renderAllPawnsExcepts(std::string PawnToNotRender){

	//int positioninVector=spriteCoordinate.getCoordinate("B3");	
	//std::cout << "position in vector is: "<<positioninVector<< std::endl;



	if(PawnToNotRender!="A2"){	
		//0,263
		chessPiecesSprite.setPosition(spriteCoordinate.getX("A2"),
				spriteCoordinate.getY("A2"));//a2
		this->target->draw(chessPiecesSprite);
	}
	if(PawnToNotRender!="B2"){	
		chessPiecesSprite.setPosition(spriteCoordinate.getX("B2"),
				spriteCoordinate.getY("B2"));//a2
		this->target->draw(chessPiecesSprite);
	}

	if(PawnToNotRender!="C2"){	
		chessPiecesSprite.setPosition(spriteCoordinate.getX("C2"),
				spriteCoordinate.getY("C2"));//a2
		this->target->draw(chessPiecesSprite);
	}
	if(PawnToNotRender!="D2"){	
		chessPiecesSprite.setPosition(spriteCoordinate.getX("D2"),
				spriteCoordinate.getY("D2"));//a2
		this->target->draw(chessPiecesSprite);
	}
	if(PawnToNotRender!="E2"){	
		chessPiecesSprite.setPosition(E,r2);//a2
		this->target->draw(chessPiecesSprite);
	}
	if(PawnToNotRender!="F2"){	
		chessPiecesSprite.setPosition(spriteCoordinate.getX("F2"),
				spriteCoordinate.getY("F2"));//a2
		this->target->draw(chessPiecesSprite);
	}
	if(PawnToNotRender!="G2"){	
		chessPiecesSprite.setPosition(spriteCoordinate.getX("G2"),
				spriteCoordinate.getY("G2"));//a2
		this->target->draw(chessPiecesSprite);
	}
	if(PawnToNotRender!="H2"){	
		chessPiecesSprite.setPosition(spriteCoordinate.getX("H2"),
				spriteCoordinate.getY("H2"));//a2
		this->target->draw(chessPiecesSprite);
	}
}



void Pawn::attachToMouseCertinPawn(std::string pawnName){

	mPosition = sf::Mouse::getPosition(*window);
	chessPiecesSprite.setPosition(mPosition.x-22,mPosition.y-22);

	if(pawnName=="A2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="B2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="C2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="D2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="E2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="F2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="G2")	
		this->target->draw(chessPiecesSprite);
	if(pawnName=="H2")	
		this->target->draw(chessPiecesSprite);

}

void Pawn::render(){
	std::cout << "inside pawn render!!" << std::endl;


	this->target->draw(chessBoardSprite);


	//chessPiecesSprite.setPosition(46,263);
	if(currentState==initial){
		renderAllPawns();
	}
	else if (currentState==pieceClicked) {
		//clickedCoordinate is cooming from the Game.cpp
		////clickedCoordinate="A1"
		renderAllPawnsExcepts(clickedCoordinate);
		attachToMouseCertinPawn(clickedCoordinate);
		//renderCertinPawn(pawnNameVector[0]);

	}
	else if (currentState==pieceReleased) {
		//clickedCoordinate is cooming from the Game.cpp
		////clickedCoordinate="A1"

		//if clicked==a1 and released=a1 the i dont want to render all pawn exceps
		//if(clickedCoordinate!=releasedCoordinate){
		
		if(CheckIfLegalObject.checkPawnMoveLegal(clickedCoordinate,
					releasedCoordinate)){
		renderAllPawnsExcepts(clickedCoordinate);
			renderCertinPawn(releasedCoordinate); 
		}
		else {
			currentState=initial;
		}
	}
}


void Pawn::renderAllPawns(){
	//	int positioninVector=spriteCoordinate.getCoordinate("b3");	
	//	std::cout << "position in vector is: "<<positioninVector<< std::endl;

	for (int i=0; i<8; i++) {
		chessPiecesSprite.setPosition(44*i,r2);
		this->target->draw(chessPiecesSprite);
	}

}

