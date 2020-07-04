#include "Coordinate.h"
#include <SFML/System/Vector2.hpp>
#include <string>
#include <algorithm>
Coordinate::Coordinate(){

	//chessPieceCoordinate(file,rank)	
	//
	//a2
	//chessPieceCoordinate.push_back(sf::Vector2i(0,263));
	//b2
	//chessPieceCoordinate.push_back(sf::Vector2i(44,263));	
	//c2
	//chessPieceCoordinate.push_back(sf::Vector2i(44*2,263));
	//d2
	//chessPieceCoordinate.push_back(sf::Vector2i(44*3,263));
	//"a2"
	//pieceName is a vector of strings with the name of each square
	//

	char rankArrayChar[8]={'A','B','C','D','E','F','G','H'};

	for(int j=0;j<9;j++){
		for (int i=0; i<9; i++) {
			//	pushToVectors('A', i);
			name.at(0)=rankArrayChar[j];
			s = std::to_string(i+1);
			name=name+s;
			chessPieceCoordinate.push_back(sf::Vector2i(44*j,307-44*i));
			pieceName.push_back(name);
			name="X";

		}
	}

}
Coordinate::~Coordinate(){


}

int Coordinate::getCoordinate(std::string squareName){

	int index=0;
	std::vector<std::string>::iterator it =std::find(pieceName.begin(),
			pieceName.end(), squareName);	

	return 	index = std::distance(pieceName.begin(), it);	

}

int Coordinate::getX(std::string CoordinateString)
{
	int index=getCoordinate(CoordinateString);
	return chessPieceCoordinate[index].x;
}

int Coordinate::getY(std::string CoordinateString)
{
	int index=getCoordinate(CoordinateString);
	return chessPieceCoordinate[index].y;
}
