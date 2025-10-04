#ifndef ARROW_H
#define ARROW_H

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <string>
#include <vector>
#include "Coordinate.h"

class Arrow {
private:
    sf::RenderWindow* window;
    std::string fromSquare;
    std::string toSquare;
    sf::Color color;
    bool isVisible;

    sf::RectangleShape shaft;
    sf::ConvexShape head;

    void createArrow();
    sf::Vector2f squareToPixel(const std::string& square) const;

public:
    Arrow(sf::RenderWindow* window, const std::string& from, const std::string& to, sf::Color color);

    void setFromSquare(const std::string& from);
    void setToSquare(const std::string& to);
    void setColor(const sf::Color& color);
    void setVisible(bool visible);
    bool getVisible() const { return isVisible; }

    void render();
};

class ArrowManager {
private:
    sf::RenderWindow* window;
    std::vector<Arrow> arrows;
    bool isVisible;

    // Color scheme for top 3 moves
    static const sf::Color LINE1_COLOR; // Best move
    static const sf::Color LINE2_COLOR; // Second best
    static const sf::Color LINE3_COLOR; // Third best

public:
    ArrowManager(sf::RenderWindow* window);

    void clearArrows();
    void addArrow(const std::string& from, const std::string& to, int lineIndex);
    void setVisible(bool visible);
    void toggleVisibility();
    bool getVisible() const { return isVisible; }

    void render();
};

#endif /* ARROW_H */
