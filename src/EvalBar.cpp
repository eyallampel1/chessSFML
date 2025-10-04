#include "EvalBar.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

EvalBar::EvalBar(sf::RenderWindow* window, sf::Font* font, int x, int y, int width, int height)
    : window(window), font(font), evaluation(0.0f), isVisible(true),
      barWidth(width), barHeight(height), posX(x), posY(y) {

    // Background (dark gray)
    background.setSize(sf::Vector2f(static_cast<float>(barWidth), static_cast<float>(barHeight)));
    background.setPosition(static_cast<float>(posX), static_cast<float>(posY));
    background.setFillColor(sf::Color(50, 50, 50));

    // White advantage bar (starts from center, grows upward)
    whiteBar.setSize(sf::Vector2f(static_cast<float>(barWidth), 0.0f));
    whiteBar.setPosition(static_cast<float>(posX), static_cast<float>(posY + barHeight / 2));
    whiteBar.setFillColor(sf::Color::White);

    // Black advantage bar (starts from center, grows downward)
    blackBar.setSize(sf::Vector2f(static_cast<float>(barWidth), 0.0f));
    blackBar.setPosition(static_cast<float>(posX), static_cast<float>(posY + barHeight / 2));
    blackBar.setFillColor(sf::Color::Black);

    // Evaluation text
    if (font) {
        evalText.setFont(*font);
        evalText.setCharacterSize(14);
        evalText.setFillColor(sf::Color::White);
    }
}

float EvalBar::clampEval(float eval) const {
    // Clamp between -1000 and +1000 centipawns (10 pawns advantage)
    return std::max(-1000.0f, std::min(1000.0f, eval));
}

float EvalBar::evalToBarHeight(float eval) const {
    // Convert evaluation to bar height
    // -1000 cp = full black bar (bottom half)
    // 0 cp = center
    // +1000 cp = full white bar (top half)
    float clampedEval = clampEval(eval);
    float ratio = clampedEval / 1000.0f; // -1.0 to +1.0
    return (barHeight / 2.0f) * ratio;
}

void EvalBar::setEvaluation(float centipawns) {
    evaluation = centipawns;

    float barFill = evalToBarHeight(centipawns);

    if (barFill >= 0) {
        // White advantage
        whiteBar.setSize(sf::Vector2f(static_cast<float>(barWidth), barFill));
        whiteBar.setPosition(static_cast<float>(posX), static_cast<float>(posY + barHeight / 2) - barFill);
        blackBar.setSize(sf::Vector2f(static_cast<float>(barWidth), 0.0f));
    } else {
        // Black advantage
        blackBar.setSize(sf::Vector2f(static_cast<float>(barWidth), -barFill));
        blackBar.setPosition(static_cast<float>(posX), static_cast<float>(posY + barHeight / 2));
        whiteBar.setSize(sf::Vector2f(static_cast<float>(barWidth), 0.0f));
    }

    // Update text
    if (font) {
        std::ostringstream oss;
        float pawns = centipawns / 100.0f;

        if (std::abs(centipawns) >= 10000) {
            // Mate score
            oss << "M";
        } else {
            oss << std::fixed << std::setprecision(1);
            if (pawns >= 0) {
                oss << "+" << pawns;
            } else {
                oss << pawns;
            }
        }

        evalText.setString(oss.str());

        // Center text in the bar
        sf::FloatRect textBounds = evalText.getLocalBounds();
        evalText.setPosition(
            static_cast<float>(posX) + (static_cast<float>(barWidth) - textBounds.width) / 2.0f,
            static_cast<float>(posY + barHeight - 25)
        );
    }
}

void EvalBar::setVisible(bool visible) {
    isVisible = visible;
}

void EvalBar::toggleVisibility() {
    isVisible = !isVisible;
}

void EvalBar::render() {
    if (!isVisible) return;

    window->draw(background);
    window->draw(blackBar);
    window->draw(whiteBar);

    if (font) {
        window->draw(evalText);
    }
}
