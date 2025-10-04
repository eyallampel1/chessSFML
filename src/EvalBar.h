#ifndef EVALBAR_H
#define EVALBAR_H

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Font.hpp>

class EvalBar {
private:
    sf::RenderWindow* window;
    sf::RectangleShape background;
    sf::RectangleShape whiteBar;
    sf::RectangleShape blackBar;
    sf::Text evalText;
    sf::Font* font;

    float evaluation; // In centipawns (-1000 to 1000, clamped to -1000/+1000 for display)
    bool isVisible;

    // Mate display state
    bool mateMode = false;      // true when showing a mate score
    int matePliesStored = 0;    // mate distance in plies
    bool mateWhiteWinning = false; // who is winning in mate

    int barWidth;
    int barHeight;
    int posX;
    int posY;

    float clampEval(float eval) const;
    float evalToBarHeight(float eval) const;

public:
    EvalBar(sf::RenderWindow* window, sf::Font* font, int x, int y, int width, int height);

    void setEvaluation(float centipawns);
    void setMateEvaluation(int matePlies, bool whiteWinning);
    void setVisible(bool visible);
    void toggleVisibility();
    bool getVisible() const { return isVisible; }

    void render();
};

#endif /* EVALBAR_H */
