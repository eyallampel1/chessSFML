#ifndef COORDINATE_H
#define COORDINATE_H

#include <SFML/System/Vector2.hpp>
#include <iostream>
#include <string>
#include <vector>

class Coordinate
{
	private:

		std::string name="X";
		std::string s="X";
		int x;
		int y;
		sf::Vector2i xyCoordinate;
		std::vector<std::string> pieceName;
	public:
		Coordinate();
		virtual ~Coordinate();
		int getX(std::string CoordinateString);
		int getY(std::string CoordinateString);
		int getCoordinate(std::string squareName);
//		void pushToVectors(char fileChar,int rankInt);
		std::vector<sf::Vector2i> chessPieceCoordinate;
};

#endif /* COORDINATE_H */
