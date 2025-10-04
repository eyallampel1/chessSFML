#include "Button.h"

Button::Button(sf::RenderWindow* window, sf::Font* font, const std::string& label,
               float x, float y, float width, float height)
    : window(window), font(font), label(label), isHovered(false), isPressed(false) {

    // Colors
    normalColor = sf::Color(70, 70, 70);
    hoverColor = sf::Color(90, 90, 90);
    pressColor = sf::Color(50, 50, 50);
    textColor = sf::Color::White;

    // Background
    background.setSize(sf::Vector2f(width, height));
    background.setPosition(x, y);
    background.setFillColor(normalColor);

    // Border
    border.setSize(sf::Vector2f(width, height));
    border.setPosition(x, y);
    border.setFillColor(sf::Color::Transparent);
    border.setOutlineColor(sf::Color(150, 150, 150));
    border.setOutlineThickness(2.0f);

    // Text
    if (font) {
        text.setFont(*font);
        text.setString(label);
        text.setCharacterSize(16);
        text.setFillColor(textColor);

        // Center text
        sf::FloatRect textBounds = text.getLocalBounds();
        text.setPosition(
            x + (width - textBounds.width) / 2.0f - textBounds.left,
            y + (height - textBounds.height) / 2.0f - textBounds.top
        );
    }
}

void Button::setOnClick(std::function<void()> callback) {
    onClick = callback;
}

bool Button::contains(const sf::Vector2i& point) const {
    return background.getGlobalBounds().contains(static_cast<float>(point.x), static_cast<float>(point.y));
}

void Button::update(const sf::Vector2i& mousePos) {
    isHovered = contains(mousePos);

    // Reset pressed state if mouse is released
    if (!sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
        isPressed = false;
    }

    if (isPressed) {
        background.setFillColor(pressColor);
    } else if (isHovered) {
        background.setFillColor(hoverColor);
    } else {
        background.setFillColor(normalColor);
    }
}

void Button::handleClick(const sf::Vector2i& mousePos) {
    if (contains(mousePos)) {
        isPressed = true;
        if (onClick) {
            onClick();
        }
    }
}

void Button::render() {
    window->draw(background);
    window->draw(border);
    if (font) {
        window->draw(text);
    }
}
