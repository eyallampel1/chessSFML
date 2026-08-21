#ifndef BUTTON_H
#define BUTTON_H

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Window/Mouse.hpp>
#include <functional>
#include <string>

class Button {
private:
    sf::RenderWindow* window;
    sf::RectangleShape background;
    sf::RectangleShape border;
    sf::Text text;
    sf::Font* font;

    std::string label;
    std::function<void()> onClick;

    bool isHovered;
    bool isPressed;
    bool enabled;

    sf::Color normalColor;
    sf::Color disabledColor;
    sf::Color hoverColor;
    sf::Color pressColor;
    sf::Color textColor;

public:
    Button(sf::RenderWindow* window, sf::Font* font, const std::string& label,
           float x, float y, float width, float height);

    void setOnClick(std::function<void()> callback);
    void update(const sf::Vector2i& mousePos);
    void handleClick(const sf::Vector2i& mousePos);
    void render();

    bool contains(const sf::Vector2i& point) const;

    void setEnabled(bool value);
    bool isEnabled() const { return enabled; }

    // Update button label at runtime and re-center the text
    void setLabel(const std::string& newLabel);

    // Move and resize at runtime; course mode lays its panel out from the
    // board size, which depends on the screen.
    void setBounds(float x, float y, float width, float height);
};

#endif /* BUTTON_H */
