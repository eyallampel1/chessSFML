#ifndef CHECKIFLEGAL_H
#define CHECKIFLEGAL_H
#include <string>
#include <vector>
class CheckIfLegal
{
private:
	

public:
	CheckIfLegal();
	bool checkPawnMoveLegal(std::string pawnStart,
			std::string pawnEnd);
	std::vector<std::string>returnLegalMoves(std::string pawnClicked);
	virtual ~CheckIfLegal();
};

#endif /* CHECKIFLEGAL_H */
