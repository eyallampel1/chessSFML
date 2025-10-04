#include "Arrow.h"
#include <cmath>

// Arrow implementation
Arrow::Arrow(sf::RenderWindow* window, const std::string& from, const std::string& to, sf::Color color)
    : window(window), fromSquare(from), toSquare(to), color(color), isVisible(true) {
    createArrow();
}

sf::Vector2f Arrow::squareToPixel(const std::string& square) const {
    // Convert square like "E2" to pixel coordinates (center of square)
    Coordinate coord;
    int x = coord.getX(square);
    int y = coord.getY(square);

    return sf::Vector2f(static_cast<float>(x + 22), static_cast<float>(y + 22)); // Center of square
}

void Arrow::createArrow() {
    sf::Vector2f start = squareToPixel(fromSquare);
    sf::Vector2f end = squareToPixel(toSquare);

    // Calculate arrow direction and length
    sf::Vector2f direction = end - start;
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    float angle = std::atan2(direction.y, direction.x) * 180.0f / 3.14159f;

    // Shaft properties
    float shaftWidth = 8.0f;
    float shaftLength = length - 15.0f; // Leave space for arrow head

    shaft.setSize(sf::Vector2f(shaftLength, shaftWidth));
    shaft.setOrigin(0, shaftWidth / 2);
    shaft.setPosition(start);
    shaft.setRotation(angle);
    shaft.setFillColor(color);

    // Arrow head (triangle)
    head.setPointCount(3);

    float headWidth = 20.0f;
    float headHeight = 15.0f;

    // Triangle points (pointing right)
    head.setPoint(0, sf::Vector2f(headHeight, 0));           // Tip
    head.setPoint(1, sf::Vector2f(0, -headWidth / 2));       // Top corner
    head.setPoint(2, sf::Vector2f(0, headWidth / 2));        // Bottom corner

    head.setOrigin(headHeight / 2, 0);
    head.setPosition(end);
    head.setRotation(angle);
    head.setFillColor(color);
}

void Arrow::setFromSquare(const std::string& from) {
    fromSquare = from;
    createArrow();
}

void Arrow::setToSquare(const std::string& to) {
    toSquare = to;
    createArrow();
}

void Arrow::setColor(const sf::Color& newColor) {
    color = newColor;
    shaft.setFillColor(color);
    head.setFillColor(color);
}

void Arrow::setVisible(bool visible) {
    isVisible = visible;
}

void Arrow::render() {
    if (!isVisible) return;

    window->draw(shaft);
    window->draw(head);
}

// ArrowManager implementation
const sf::Color ArrowManager::LINE1_COLOR = sf::Color(0, 255, 0, 200);      // Green (best)
const sf::Color ArrowManager::LINE2_COLOR = sf::Color(255, 255, 0, 200);    // Yellow (2nd)
const sf::Color ArrowManager::LINE3_COLOR = sf::Color(255, 165, 0, 200);    // Orange (3rd)

ArrowManager::ArrowManager(sf::RenderWindow* window)
    : window(window), isVisible(true) {
}

void ArrowManager::clearArrows() {
    arrows.clear();
}

void ArrowManager::addArrow(const std::string& from, const std::string& to, int lineIndex) {
    sf::Color arrowColor;

    switch (lineIndex) {
        case 0:
            arrowColor = LINE1_COLOR;
            break;
        case 1:
            arrowColor = LINE2_COLOR;
            break;
        case 2:
            arrowColor = LINE3_COLOR;
            break;
        default:
            arrowColor = sf::Color(128, 128, 128, 200); // Gray for others
            break;
    }

    arrows.emplace_back(window, from, to, arrowColor);
}

void ArrowManager::setVisible(bool visible) {
    isVisible = visible;
    for (auto& arrow : arrows) {
        arrow.setVisible(visible);
    }
}

void ArrowManager::toggleVisibility() {
    isVisible = !isVisible;
    setVisible(isVisible);
}

void ArrowManager::render() {
    if (!isVisible) return;

    for (auto& arrow : arrows) {
        arrow.render();
    }
}
