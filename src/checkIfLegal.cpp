#include "checkIfLegal.h"
#include <cstdlib>


//Constractor
CheckIfLegal::CheckIfLegal(){

}

//Destractor
CheckIfLegal::~CheckIfLegal(){

}


bool CheckIfLegal::checkPawnMoveLegal(std::string pawnStart, std::string pawnEnd)
{
	int startRank=(int)pawnStart.at(1)-'0';
	int endRank=(int)pawnEnd.at(1)-'0';
	bool legalMove=true;


	if(startRank==2){
		if (endRank-startRank > 2) {
			return false;
		}
	}
	else {
		if (endRank-startRank > 1) {
			return false;
		}
	}
	if(endRank-startRank<=0){
		return false;
	}


	return legalMove;
}
