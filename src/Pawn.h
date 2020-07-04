#ifndef PAWN_H
#define PAWN_H
#include "Piece.h"
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <string>
#include <vector>
#include "Coordinate.h"
#include "checkIfLegal.h"
class Pawn:public Piece
{
	private:
		//a vector of strings contining the pawn name
		std::vector<std::string>pawnNameVector;
		bool continueLoop=true;
CheckIfLegal CheckIfLegalObject;
		//coordinate
//		Coordinate spriteCoordinate;

	private:
		void attachToMouseCertinPawn(std::string pawnName);
		void renderCertinPawn(const std::string& pawnName);
		void renderAllPawns();
		void renderAllPawnsExcepts(std::string pawnToNotRender);

	public:
		Pawn(sf::RenderTarget* target,
		sf::RenderWindow* window);
		virtual ~Pawn();
		void render();
		sf::Vector2i mPosition;
};

#endif /* PAWN_H */
