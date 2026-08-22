#ifndef COURSE_TRAINER_H
#define COURSE_TRAINER_H

// "Hakobyan full 1.d4 course" mode.
//
// Pick one of the course chapters, walk the author's variation tree with his
// annotations on screen, and drill its lines on a spaced-repetition schedule.
// The trainer owns its own buttons and panels; the Game only has to forward
// events to it and let it draw.

#include "Board.h"
#include "Button.h"
#include "CoursePgn.h"
#include "UserDB.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/System/String.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <functional>
#include <string>
#include <vector>

class CourseTrainer {
public:
    // Course mode draws the board larger than the normal game does, at a
    // scale the Game picks to fit the screen, so the panels are laid out from
    // the resulting size rather than from constants.
    struct Layout {
        float boardSize  = 352.0f;   // scaled board edge, in window pixels
        float evalBarW   = 40.0f;
        float panelX     = 392.0f;   // boardSize + evalBarW
        float panelW     = 708.0f;
        float windowW    = 1100.0f;
        float windowH    = 700.0f;
    };

    // Board scales the Game chooses between; the largest that fits wins.
    static constexpr float BOARD_UNIT = 352.0f;

    enum class Screen { Chapters, Study };
    enum class Mode { Learn, Drill };

    CourseTrainer(sf::RenderWindow* window, sf::Font* font, Board* board, UserDB* db);
    ~CourseTrainer();

    bool isActive() const { return active; }
    bool isPicking() const { return active && screen == Screen::Chapters; }

    // Opens the trainer. `askForCourseFolder` is only called the first time,
    // or when the remembered folder has gone away; it should return the path
    // of any PGN inside the course folder (or the folder itself).
    bool enter(const std::function<std::string()>& askForCourseFolder);
    void leave();

    // Forces the folder prompt, for the "Course Folder" button.
    bool chooseFolder(const std::function<std::string()>& askForCourseFolder);

    void setStatusSink(std::function<void(const std::string&)> sink) { status = std::move(sink); }

    // Each returns true when the trainer consumed the event and the caller
    // should not pass it on to the board.
    bool handleMousePress(const sf::Vector2i& mouse);
    bool handleMouseWheel(const sf::Vector2i&, float delta);
    bool handleKey(sf::Keyboard::Key key);
    void updateHover(const sf::Vector2i& mouse);

    // Called once the board has accepted a move the user made by hand; the
    // trainer either keeps it or takes it back.
    void onUserMove();

    // True while user moves on the board should be ignored entirely.
    bool boardLocked() const;

    // One of the engine's top lines, already formatted by the Game (which owns
    // the engine) so the trainer needs nothing from StockfishEngine.h.
    struct EngineReadout {
        std::string eval;   // "+0.34", "M3"
        std::string pv;     // "E2E4 E7E5 G1F3"
    };

    // Two independent switches, as an analysis board has them: Evaluation
    // shows the bar, Lines shows the ranked variations and the board arrows.
    // Both start off - in Drill they would hand over the answer - and the
    // engine runs while either is on.
    bool engineEnabled() const { return evalOn || linesOn; }
    bool evalVisible() const { return evalOn; }
    bool linesVisible() const { return linesOn; }
    void toggleEval();
    void toggleLines();
    void setEngineLines(std::vector<EngineReadout> lines, int depth) {
        engineLines = std::move(lines);
        engineDepth = depth;
    }
    void clearEngineLines() { engineLines.clear(); engineDepth = 0; }

    void update(float dtMs);

    // Board decorations, drawn in unscaled 0..352 board coordinates under the
    // Game's board view so they scale with the pieces.
    void renderBoardOverlay();
    // Everything else, in window coordinates.
    void render();

    void setLayout(const Layout& layout);
    const Layout& layout() const { return ui; }

private:
    Layout ui;

    // Panel geometry, all derived from `ui`. The tree panel sits under the
    // board and is exactly as wide as the board plus its eval bar.
    float panelX() const { return ui.panelX; }
    float panelW() const { return ui.panelW; }
    float treeY()  const { return ui.boardSize; }
    float treeW()  const { return ui.panelX; }

    sf::RenderWindow* window;
    sf::Font* font;
    Board* board;
    UserDB* db;
    std::function<void(const std::string&)> status;

    bool active = false;
    Screen screen = Screen::Chapters;
    Mode mode = Mode::Learn;

    // --- course ---
    std::string courseDir;
    std::vector<std::string> chapterFiles;               // UTF-8 paths
    std::vector<std::string> chapterTitles;              // UTF-8, display form
    std::vector<UserDB::ChapterStats> chapterStats;
    std::vector<std::size_t> chapterLineCounts;
    int chapterScroll = 0;

    int chapterIndex = -1;
    course::Chapter chapter;
    std::vector<course::Line> lines;
    bool heroIsWhite = true;
    std::string loadError;

    // --- learn ---
    std::vector<int> path;        // node ids walked from the root
    int variationCursor = 0;      // which child of the current node is armed
    int learnLineIndex = 0;

    // --- drill ---
    int cardIndex = -1;
    int lastCardIndex = -1;
    std::size_t drillPos = 0;     // moves of the card already on the board
    int mistakes = 0;
    bool hintUsed = false;
    bool lineComplete = false;
    float replyDelayMs = 0.0f;
    std::string revealedMove;     // set while a hint is on screen

    std::string feedback;
    sf::Color feedbackColor = sf::Color(200, 200, 200);

    // --- engine ---
    bool evalOn = false;
    bool linesOn = false;
    std::vector<EngineReadout> engineLines;
    int engineDepth = 0;

    // Rows of the "options from here" list, recorded while drawing so a click
    // can pick a variation without the layout being computed twice.
    std::vector<std::pair<sf::FloatRect, int>> optionRows;
    sf::Vector2i hoverMouse;

    // --- buttons ---
    std::vector<Button*> buttons;
    Button* chaptersButton = nullptr;
    Button* modeButton = nullptr;
    Button* exitButton = nullptr;
    Button* prevButton = nullptr;   // "Show Answer" in drill
    Button* nextButton = nullptr;   // "Restart Line" in drill
    Button* lineButton = nullptr;   // "Next Line" / "Skip Line"
    Button* evalButton = nullptr;
    Button* linesButton = nullptr;
    Button* positionButton = nullptr;
    // The chapter list fills the window, so it gets its own footer controls
    // rather than sharing the study panel's.
    std::vector<Button*> pickerButtons;
    std::function<std::string()> folderPrompt;

    // Wrapping a 1500-character annotation every frame would be wasteful, so
    // the result is kept until the text or the width changes.
    struct WrapCache {
        std::string source;
        float width = 0.0f;
        unsigned int charSize = 0;
        std::vector<sf::String> lines;
    };
    mutable WrapCache annotationCache;

    void buildButtons();
    void positionButtons();
    void syncButtonLabels();

    bool openChapter(int index);
    void refreshChapterStats();

    // board <-> tree
    // Which game of the chapter is on the board, and where inside it we are.
    // A collection holds many games, each starting from its own position.
    int currentRoot = 0;
    int currentNode() const { return path.empty() ? currentRoot : path.back(); }
    void setUpGame(int root);   // puts the game's starting position on the board

    // A collection is a list of positions to study, so it needs navigating by
    // position as well as by line.
    int currentGameIndex() const;
    void gotoGame(int index);
    const course::MoveNode& node(int id) const { return chapter.nodes[static_cast<std::size_t>(id)]; }

    // learn
    void stepForward();
    void stepBack();
    void cycleVariation(int delta);
    void playVariation(int childIndex);
    void gotoLine(int index);

    // drill
    int pickNextCard() const;
    void startCard(int index);
    void autoPlayOpponent();
    void finishLine();
    void showAnswer();

    void setFeedback(const std::string& text, const sf::Color& color);
    void say(const std::string& message);

    // rendering
    void renderPanelBackground();
    void renderChapterPicker();
    void renderStudyPanel();
    void renderMoveTree();
    void renderMarkings();
    // Returns the y just below the block, or `y` unchanged when off/empty.
    float renderEngineLines(float x, float y);
    void renderAnnotation(float x, float y, float width, float maxY);

    const std::vector<sf::String>& wrap(const std::string& utf8, float width,
                                        unsigned int charSize) const;
    void drawText(const sf::String& s, float x, float y, unsigned int size,
                  const sf::Color& color, bool bold = false);
    float textWidth(const sf::String& s, unsigned int size) const;
};

#endif /* COURSE_TRAINER_H */
