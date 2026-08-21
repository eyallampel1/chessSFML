#include "CourseTrainer.h"

#include "AppPaths.h"
#include "Coordinate.h"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

const sf::Color kPanelBg(24, 26, 30);
const sf::Color kPanelBg2(32, 35, 40);
const sf::Color kText(214, 216, 220);
const sf::Color kTextDim(150, 153, 158);
const sf::Color kAccent(120, 180, 250);
const sf::Color kGood(110, 200, 120);
const sf::Color kBad(230, 110, 100);
const sf::Color kWarn(235, 190, 90);

sf::String toDisplay(const std::string& utf8) {
    return sf::String::fromUtf8(utf8.begin(), utf8.end());
}

// The board draws A1 bottom-left and takes uppercase file letters.
std::string toBoardSquare(const std::string& uciSquare) {
    if (uciSquare.size() < 2) return {};
    std::string s;
    s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(uciSquare[0]))));
    s.push_back(uciSquare[1]);
    return s;
}

sf::Color markingColor(char code, sf::Uint8 alpha) {
    switch (std::toupper(static_cast<unsigned char>(code))) {
        case 'R': return sf::Color(220, 70, 60, alpha);
        case 'Y': return sf::Color(235, 200, 60, alpha);
        case 'B': return sf::Color(80, 140, 235, alpha);
        default:  return sf::Color(90, 200, 100, alpha);
    }
}

// Chapter-list geometry, shared by the renderer and the hit test.
constexpr float kPickerTop = 84.0f;
constexpr float kPickerRowH = 30.0f;
constexpr int   kPickerRows = 18;

std::string humanInterval(long long seconds) {
    if (seconds <= 0) return "now";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m";
    if (seconds < 86400) return std::to_string(seconds / 3600) + "h";
    return std::to_string(seconds / 86400) + "d";
}

} // namespace

CourseTrainer::CourseTrainer(sf::RenderWindow* window, sf::Font* font, Board* board, UserDB* db)
    : window(window), font(font), board(board), db(db) {
    buildButtons();
}

void CourseTrainer::setLayout(const Layout& next) {
    ui = next;
    positionButtons();
    annotationCache.source.clear();   // the wrap width has changed
}

CourseTrainer::~CourseTrainer() {
    for (auto* b : buttons) delete b;
    for (auto* b : pickerButtons) delete b;
}

void CourseTrainer::say(const std::string& message) {
    // The Game's status line is hidden in course mode, so notices go on the
    // trainer's own feedback line as well.
    if (status) status(message);
    setFeedback(message, kTextDim);
}

// --- entering / leaving -----------------------------------------------------

bool CourseTrainer::enter(const std::function<std::string()>& askForCourseFolder) {
    folderPrompt = askForCourseFolder;

    if (courseDir.empty() && db) courseDir = db->getSetting("course_dir");

    if (courseDir.empty()) {
        // A packaged copy ships its chapters in .\course next to the exe, so
        // there is nothing to ask about on a fresh install.
        const std::filesystem::path bundled =
            std::filesystem::path(apppaths::executableDir()) / "course";
        const std::string asUtf8 = bundled.u8string();
        if (!course::findChapterFiles(asUtf8).empty()) {
            courseDir = asUtf8;
            if (db) { db->setSetting("course_dir", courseDir); db->save(); }
        }
    }

    bool haveChapters = false;
    if (!courseDir.empty()) {
        chapterFiles = course::findChapterFiles(courseDir);
        haveChapters = !chapterFiles.empty();
    }
    if (!haveChapters) {
        if (!chooseFolder(askForCourseFolder)) return false;
    } else {
        refreshChapterStats();
    }

    active = true;
    screen = Screen::Chapters;
    chapterScroll = 0;
    say("Course loaded: " + std::to_string(chapterFiles.size()) + " chapters");
    return true;
}

bool CourseTrainer::chooseFolder(const std::function<std::string()>& askForCourseFolder) {
    if (!askForCourseFolder) return false;
    const std::string picked = askForCourseFolder();
    if (picked.empty()) {
        say("Course folder not selected");
        return false;
    }

    // The prompt hands back a PGN inside the folder (a plain folder picker is
    // a different Win32 API and this keeps the dialog familiar).
    std::error_code ec;
    std::filesystem::path p = std::filesystem::u8path(picked);
    if (std::filesystem::is_regular_file(p, ec)) p = p.parent_path();

    const std::string dir = p.u8string();
    const auto files = course::findChapterFiles(dir);
    if (files.empty()) {
        say("No .pgn chapters in that folder");
        return false;
    }

    courseDir = dir;
    chapterFiles = files;
    chapterIndex = -1;
    chapter = course::Chapter{};
    lines.clear();
    path.clear();
    if (db) {
        db->setSetting("course_dir", courseDir);
        db->save();
    }
    refreshChapterStats();
    screen = Screen::Chapters;
    chapterScroll = 0;
    return true;
}

void CourseTrainer::leave() {
    active = false;
    path.clear();
    cardIndex = -1;
    lineComplete = false;
    feedback.clear();
    // Hand the board back in a state the normal game can use, rather than
    // halfway down a repertoire line.
    board->reset();
}

void CourseTrainer::refreshChapterStats() {
    // Titles come from each chapter's Event tag, but reading 21 files just to
    // label a menu is wasteful - the file name carries the same text.
    chapterTitles.clear();
    chapterStats.clear();
    chapterLineCounts.assign(chapterFiles.size(), 0);

    const long long now = static_cast<long long>(std::time(nullptr));
    for (const auto& f : chapterFiles) {
        const std::string stem = std::filesystem::u8path(f).stem().u8string();
        chapterTitles.push_back(stem);
        chapterStats.push_back(db ? db->getChapterStats(stem, now) : UserDB::ChapterStats{});
    }
}

bool CourseTrainer::openChapter(int index) {
    if (index < 0 || index >= static_cast<int>(chapterFiles.size())) return false;

    course::Chapter loaded;
    std::string error;
    if (!course::loadChapter(chapterFiles[static_cast<std::size_t>(index)], loaded, error)) {
        loadError = error;
        say("Chapter failed to load");
        return false;
    }

    loadError.clear();
    chapter = std::move(loaded);
    // The stats key has to match what refreshChapterStats() used, and the
    // Event tag and the file stem are not always spelled the same.
    chapter.title = chapterTitles[static_cast<std::size_t>(index)];
    lines = course::enumerateLines(chapter);
    chapterIndex = index;
    chapterLineCounts[static_cast<std::size_t>(index)] = lines.size();

    heroIsWhite = true;
    if (!chapter.nodes[0].children.empty()) {
        heroIsWhite = node(chapter.nodes[0].children[0]).byWhite;
    }

    screen = Screen::Study;
    path.clear();
    variationCursor = 0;
    learnLineIndex = 0;
    cardIndex = -1;
    lastCardIndex = -1;
    lineComplete = false;
    feedback.clear();
    annotationCache.source.clear();
    rebuildBoard(0);

    if (mode == Mode::Drill) {
        startCard(pickNextCard());
    } else {
        setFeedback("Right/Left walk the line, Up/Down switch variation", kTextDim);
    }
    syncButtonLabels();
    if (db) db->setSetting("course_chapter", chapter.title);
    return true;
}

// --- board synchronisation --------------------------------------------------

void CourseTrainer::rebuildBoard(std::size_t moveCount) {
    board->reset();
    const std::vector<int>& ids = (mode == Mode::Drill && cardIndex >= 0)
                                ? lines[static_cast<std::size_t>(cardIndex)].nodeIds
                                : path;
    for (std::size_t i = 0; i < moveCount && i < ids.size(); ++i) {
        board->setNextProgrammaticAnimation(1.0f, 0.0f);
        board->applyUCIMove(node(ids[i]).uci);
    }
}

// --- learn mode -------------------------------------------------------------

void CourseTrainer::stepForward() {
    const auto& kids = node(currentNode()).children;
    if (kids.empty()) {
        setFeedback("End of the line", kWarn);
        return;
    }
    if (variationCursor < 0 || variationCursor >= static_cast<int>(kids.size())) variationCursor = 0;

    const int next = kids[static_cast<std::size_t>(variationCursor)];
    board->setNextProgrammaticAnimation(220.0f, 0.0f);
    if (!board->applyUCIMove(node(next).uci)) {
        setFeedback("Could not play " + node(next).san + " on the board", kBad);
        return;
    }
    path.push_back(next);
    variationCursor = 0;
    annotationCache.source.clear();
    feedback.clear();
}

void CourseTrainer::stepBack() {
    if (path.empty()) {
        setFeedback("Already at the start", kTextDim);
        return;
    }
    const int leaving = path.back();
    path.pop_back();
    board->undoLastMove();

    // Put the cursor back on the branch we just came out of, so pressing
    // Right again retraces the same move.
    const auto& kids = node(currentNode()).children;
    const auto it = std::find(kids.begin(), kids.end(), leaving);
    variationCursor = (it == kids.end()) ? 0 : static_cast<int>(std::distance(kids.begin(), it));
    annotationCache.source.clear();
    feedback.clear();
}

void CourseTrainer::cycleVariation(int delta) {
    const auto& kids = node(currentNode()).children;
    if (kids.size() < 2) {
        setFeedback("Only one move here", kTextDim);
        return;
    }
    const int n = static_cast<int>(kids.size());
    variationCursor = ((variationCursor + delta) % n + n) % n;
    setFeedback("Armed: " + node(kids[static_cast<std::size_t>(variationCursor)]).san, kAccent);
}

void CourseTrainer::playVariation(int childIndex) {
    const auto& kids = node(currentNode()).children;
    if (childIndex < 0 || childIndex >= static_cast<int>(kids.size())) return;
    variationCursor = childIndex;
    stepForward();
}

void CourseTrainer::gotoLine(int index) {
    if (lines.empty()) return;
    const int n = static_cast<int>(lines.size());
    learnLineIndex = ((index % n) + n) % n;
    path = lines[static_cast<std::size_t>(learnLineIndex)].nodeIds;
    variationCursor = 0;
    annotationCache.source.clear();

    board->reset();
    for (int id : path) {
        board->setNextProgrammaticAnimation(1.0f, 0.0f);
        board->applyUCIMove(node(id).uci);
    }
    setFeedback("Line " + std::to_string(learnLineIndex + 1) + " of " + std::to_string(lines.size()), kAccent);
}

// --- drill mode -------------------------------------------------------------

int CourseTrainer::pickNextCard() const {
    if (lines.empty() || !db) return -1;

    const long long now = static_cast<long long>(std::time(nullptr));
    int bestDue = -1;
    long long bestDueTs = 0;
    int firstNew = -1;
    int soonest = -1;
    long long soonestTs = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (static_cast<int>(i) == lastCardIndex && lines.size() > 1) continue;

        UserDB::CourseCard card;
        if (!db->getCourseCard(lines[i].id, card)) {
            if (firstNew < 0) firstNew = static_cast<int>(i);
            continue;
        }
        if (card.dueTs <= now && (bestDue < 0 || card.dueTs < bestDueTs)) {
            bestDue = static_cast<int>(i);
            bestDueTs = card.dueTs;
        }
        if (soonest < 0 || card.dueTs < soonestTs) {
            soonest = static_cast<int>(i);
            soonestTs = card.dueTs;
        }
    }

    if (bestDue >= 0) return bestDue;   // reviews first
    if (firstNew >= 0) return firstNew; // then something new
    return soonest;                     // nothing due: rehearse the nearest
}

void CourseTrainer::startCard(int index) {
    cardIndex = index;
    drillPos = 0;
    mistakes = 0;
    hintUsed = false;
    lineComplete = false;
    revealedMove.clear();
    replyDelayMs = 0.0f;
    annotationCache.source.clear();

    if (cardIndex < 0) {
        setFeedback("No lines in this chapter", kWarn);
        return;
    }
    lastCardIndex = cardIndex;

    board->reset();
    autoPlayOpponent();

    const std::size_t total = lines[static_cast<std::size_t>(cardIndex)].nodeIds.size();
    setFeedback("Play the course move  (" + std::to_string(total) + " plies in this line)", kText);
}

void CourseTrainer::autoPlayOpponent() {
    if (cardIndex < 0) return;
    const auto& ids = lines[static_cast<std::size_t>(cardIndex)].nodeIds;

    while (drillPos < ids.size()) {
        const course::MoveNode& n = node(ids[drillPos]);
        if (n.byWhite == heroIsWhite) break;   // hero's turn, stop and wait

        board->setNextProgrammaticAnimation(400.0f, 0.0f);
        if (!board->applyUCIMove(n.uci)) {
            setFeedback("Could not replay " + n.san, kBad);
            lineComplete = true;
            return;
        }
        ++drillPos;
        annotationCache.source.clear();
    }

    if (drillPos >= ids.size()) finishLine();
}

void CourseTrainer::onUserMove() {
    if (!active || screen != Screen::Study || mode != Mode::Drill) return;
    if (cardIndex < 0 || lineComplete) return;

    const auto& ids = lines[static_cast<std::size_t>(cardIndex)].nodeIds;
    if (drillPos >= ids.size()) return;

    const course::MoveNode& expected = node(ids[drillPos]);
    const std::string played = board->getLastMoveUCI();

    // A UCI without a promotion piece and one promoting to a queen are the
    // same move as far as the course is concerned.
    auto sameMove = [](const std::string& a, const std::string& b) {
        if (a == b) return true;
        if (a.size() < 4 || b.size() < 4) return false;
        if (a.compare(0, 4, b, 0, 4) != 0) return false;
        const char pa = a.size() >= 5 ? a[4] : 'q';
        const char pb = b.size() >= 5 ? b[4] : 'q';
        return pa == pb;
    };

    if (sameMove(played, expected.uci)) {
        ++drillPos;
        revealedMove.clear();
        annotationCache.source.clear();
        setFeedback(expected.san + "  \xe2\x80\x94  correct", kGood);
        replyDelayMs = 550.0f;   // let the user read before the reply lands
        return;
    }

    // Did they play one of the author's other suggestions from this position?
    const int parent = (drillPos == 0) ? 0 : ids[drillPos - 1];
    for (int sibling : node(parent).children) {
        if (sibling == ids[drillPos]) continue;
        if (sameMove(played, node(sibling).uci)) {
            board->undoLastMove();
            setFeedback(node(sibling).san + " is also in the course, but this line trains a "
                        "different move", kWarn);
            return;
        }
    }

    board->undoLastMove();
    ++mistakes;
    if (mistakes >= 2) {
        showAnswer();
    } else {
        setFeedback("Not the course move \xe2\x80\x94 try again", kBad);
    }
}

void CourseTrainer::showAnswer() {
    if (cardIndex < 0 || lineComplete) return;
    const auto& ids = lines[static_cast<std::size_t>(cardIndex)].nodeIds;
    if (drillPos >= ids.size()) return;

    hintUsed = true;
    const course::MoveNode& expected = node(ids[drillPos]);
    revealedMove = expected.uci;
    setFeedback("The move is " + expected.san + " \xe2\x80\x94 play it to continue", kWarn);
}

void CourseTrainer::finishLine() {
    if (lineComplete || cardIndex < 0) return;
    lineComplete = true;

    // Chessable-style grading: a clean run is a 5, one slip still passes,
    // needing the answer or slipping twice sends the line back to the start.
    int quality = 5;
    if (hintUsed) quality = 2;
    else if (mistakes >= 2) quality = 1;
    else if (mistakes == 1) quality = 3;

    const course::Line& line = lines[static_cast<std::size_t>(cardIndex)];
    std::ostringstream moves;
    for (std::size_t i = 0; i < line.nodeIds.size(); ++i) {
        if (i) moves << ' ';
        moves << node(line.nodeIds[i]).uci;
    }
    if (db) {
        db->gradeCourseCard(line.id, chapter.title, moves.str(), quality);
        db->save();
    }
    refreshChapterStats();

    if (quality >= 4)      setFeedback("Line complete \xe2\x80\x94 clean run", kGood);
    else if (quality == 3) setFeedback("Line complete \xe2\x80\x94 one slip, back sooner", kWarn);
    else                   setFeedback("Line complete \xe2\x80\x94 scheduled again shortly", kBad);
}

// --- input ------------------------------------------------------------------

bool CourseTrainer::boardLocked() const {
    if (!active) return false;
    if (screen != Screen::Study) return true;
    if (mode == Mode::Learn) return true;      // browsing, not playing
    return lineComplete || cardIndex < 0 || replyDelayMs > 0.0f;
}

bool CourseTrainer::handleMousePress(const sf::Vector2i& mouse) {
    if (!active) return false;

    if (screen == Screen::Chapters) {
        for (auto* b : pickerButtons) {
            if (b->contains(mouse)) { b->handleClick(mouse); return true; }
        }
        // Rows have to match renderChapterPicker().
        for (int row = 0; row < kPickerRows; ++row) {
            const int index = chapterScroll + row;
            if (index >= static_cast<int>(chapterFiles.size())) break;
            const sf::FloatRect rect(60.0f, kPickerTop + row * kPickerRowH,
                                     ui.windowW - 120.0f, kPickerRowH - 2.0f);
            if (rect.contains(static_cast<float>(mouse.x), static_cast<float>(mouse.y))) {
                openChapter(index);
                return true;
            }
        }
        return true;   // the picker is modal
    }

    for (auto* b : buttons) {
        if (b->contains(mouse)) { b->handleClick(mouse); return true; }
    }

    // Clicking a row of the "options from here" list plays that variation.
    // Up/Down still work, but at a branch point the list is where the eye
    // already is.
    if (mode == Mode::Learn) {
        for (const auto& row : optionRows) {
            if (row.first.contains(static_cast<float>(mouse.x), static_cast<float>(mouse.y))) {
                playVariation(row.second);
                return true;
            }
        }
    }

    // Anything else in the course panel or the tree panel belongs to us.
    if (mouse.x >= static_cast<int>(panelX())) return true;
    if (mouse.y >= static_cast<int>(treeY()) && mouse.x < static_cast<int>(treeW())) return true;
    return false;
}

bool CourseTrainer::handleMouseWheel(const sf::Vector2i&, float delta) {
    if (!active) return false;
    if (screen == Screen::Chapters) {
        chapterScroll -= static_cast<int>(delta);
        const int maxScroll = std::max(0, static_cast<int>(chapterFiles.size()) - kPickerRows);
        chapterScroll = std::max(0, std::min(chapterScroll, maxScroll));
        return true;
    }
    return false;
}

bool CourseTrainer::handleKey(sf::Keyboard::Key key) {
    if (!active) return false;

    if (key == sf::Keyboard::Escape) {
        if (screen == Screen::Study) { screen = Screen::Chapters; refreshChapterStats(); }
        else leave();
        return true;
    }

    if (screen == Screen::Chapters) {
        if (key == sf::Keyboard::Up)   { chapterScroll = std::max(0, chapterScroll - 1); return true; }
        if (key == sf::Keyboard::Down) {
            const int maxScroll = std::max(0, static_cast<int>(chapterFiles.size()) - kPickerRows);
            chapterScroll = std::min(maxScroll, chapterScroll + 1);
            return true;
        }
        return true;
    }

    // Available in both modes.
    if (key == sf::Keyboard::E) { toggleEval();  return true; }
    if (key == sf::Keyboard::L) { toggleLines(); return true; }

    if (mode == Mode::Learn) {
        switch (key) {
            case sf::Keyboard::Right: stepForward();  return true;
            case sf::Keyboard::Left:  stepBack();     return true;
            case sf::Keyboard::Up:    cycleVariation(-1); return true;
            case sf::Keyboard::Down:  cycleVariation(1);  return true;
            case sf::Keyboard::Space: gotoLine(learnLineIndex + 1); return true;
            default: break;
        }
        return false;
    }

    switch (key) {
        case sf::Keyboard::H:     showAnswer(); return true;
        case sf::Keyboard::R:     startCard(cardIndex); return true;
        case sf::Keyboard::Space:
            if (lineComplete) startCard(pickNextCard());
            return true;
        default: break;
    }
    return false;
}

void CourseTrainer::updateHover(const sf::Vector2i& mouse) {
    if (!active) return;
    hoverMouse = mouse;
    if (screen == Screen::Chapters) {
        for (auto* b : pickerButtons) b->update(mouse);
        return;
    }
    for (auto* b : buttons) b->update(mouse);
}

void CourseTrainer::update(float dtMs) {
    if (!active || screen != Screen::Study || mode != Mode::Drill) return;
    if (replyDelayMs > 0.0f) {
        replyDelayMs -= dtMs;
        if (replyDelayMs <= 0.0f) {
            replyDelayMs = 0.0f;
            autoPlayOpponent();
            if (!lineComplete) setFeedback("Your move", kText);
        }
    }
}

// --- buttons ----------------------------------------------------------------

void CourseTrainer::buildButtons() {
    const float x0 = panelX() + 12.0f;
    const float w = (panelW() - 24.0f - 2 * 8.0f) / 3.0f;
    const float h = 30.0f;
    const float row1 = 68.0f;
    const float row2 = row1 + h + 8.0f;
    auto colX = [&](int c) { return x0 + c * (w + 8.0f); };

    chaptersButton = new Button(window, font, "Chapters", colX(0), row1, w, h);
    chaptersButton->setOnClick([this]() {
        screen = Screen::Chapters;
        refreshChapterStats();
        syncButtonLabels();
    });
    buttons.push_back(chaptersButton);

    modeButton = new Button(window, font, "Mode: Learn", colX(1), row1, w, h);
    modeButton->setOnClick([this]() {
        if (chapterIndex < 0) { say("Open a chapter first"); return; }
        mode = (mode == Mode::Learn) ? Mode::Drill : Mode::Learn;
        if (mode == Mode::Drill) {
            startCard(pickNextCard());
        } else {
            cardIndex = -1;
            lineComplete = false;
            gotoLine(learnLineIndex);
        }
        syncButtonLabels();
    });
    buttons.push_back(modeButton);

    exitButton = new Button(window, font, "Exit Course", colX(2), row1, w, h);
    exitButton->setOnClick([this]() { leave(); });
    buttons.push_back(exitButton);

    prevButton = new Button(window, font, "< Prev", colX(0), row2, w, h);
    prevButton->setOnClick([this]() {
        if (mode == Mode::Learn) stepBack();
        else showAnswer();
    });
    buttons.push_back(prevButton);

    nextButton = new Button(window, font, "Next >", colX(1), row2, w, h);
    nextButton->setOnClick([this]() {
        if (mode == Mode::Learn) stepForward();
        else startCard(cardIndex);
    });
    buttons.push_back(nextButton);

    lineButton = new Button(window, font, "Next Line", colX(2), row2, w, h);
    lineButton->setOnClick([this]() {
        if (mode == Mode::Learn) gotoLine(learnLineIndex + 1);
        else startCard(pickNextCard());
    });
    buttons.push_back(lineButton);

    const float row3 = row2 + h + 8.0f;
    evalButton = new Button(window, font, "Evaluation: Off", colX(0), row3, w, h);
    evalButton->setOnClick([this]() { toggleEval(); });
    buttons.push_back(evalButton);

    linesButton = new Button(window, font, "Lines: Off", colX(1), row3, w, h);
    linesButton->setOnClick([this]() { toggleLines(); });
    buttons.push_back(linesButton);

    // Chapter-picker footer.
    const float footerY = ui.windowH - 44.0f;
    auto* folderButton = new Button(window, font, "Change Course Folder", 60.0f, footerY, 200.0f, 30.0f);
    folderButton->setOnClick([this]() { chooseFolder(folderPrompt); });
    pickerButtons.push_back(folderButton);

    auto* pickerExit = new Button(window, font, "Exit Course", 272.0f, footerY, 150.0f, 30.0f);
    pickerExit->setOnClick([this]() { leave(); });
    pickerButtons.push_back(pickerExit);

    positionButtons();
}

// The panel is laid out from the board size, which the Game picks to fit the
// screen, so the buttons have to be placed again whenever that changes.
void CourseTrainer::positionButtons() {
    const float x0 = panelX() + 12.0f;
    const float w = (panelW() - 24.0f - 2 * 8.0f) / 3.0f;
    const float h = 30.0f;
    const float row1 = 68.0f;
    const float row2 = row1 + h + 8.0f;
    const float row3 = row2 + h + 8.0f;
    auto colX = [&](int c) { return x0 + c * (w + 8.0f); };

    Button* const grid[3][3] = {
        { chaptersButton, modeButton,  exitButton  },
        { prevButton,     nextButton,  lineButton  },
        { evalButton,     linesButton, nullptr     },
    };
    const float rowY[3] = { row1, row2, row3 };

    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (grid[r][c]) grid[r][c]->setBounds(colX(c), rowY[r], w, h);
        }
    }

    const float footerY = ui.windowH - 44.0f;
    if (pickerButtons.size() >= 2) {
        pickerButtons[0]->setBounds(60.0f, footerY, 200.0f, 30.0f);
        pickerButtons[1]->setBounds(272.0f, footerY, 150.0f, 30.0f);
    }
}

void CourseTrainer::toggleEval() {
    evalOn = !evalOn;
    evalButton->setLabel(evalOn ? "Evaluation: On" : "Evaluation: Off");
    if (!engineEnabled()) clearEngineLines();
    if (evalOn && mode == Mode::Drill) {
        setFeedback("Evaluation on \xe2\x80\x94 the bar hints at the answer", kWarn);
    }
}

void CourseTrainer::toggleLines() {
    linesOn = !linesOn;
    linesButton->setLabel(linesOn ? "Lines: On" : "Lines: Off");
    if (!linesOn) clearEngineLines();
    if (linesOn && mode == Mode::Drill) {
        setFeedback("Lines on \xe2\x80\x94 they reveal the answer", kWarn);
    }
}

void CourseTrainer::syncButtonLabels() {
    const bool learn = (mode == Mode::Learn);
    modeButton->setLabel(learn ? "Mode: Learn" : "Mode: Drill");
    prevButton->setLabel(learn ? "< Prev" : "Show Answer");
    nextButton->setLabel(learn ? "Next >" : "Restart Line");
    lineButton->setLabel(learn ? "Next Line" : "Skip Line");
}

void CourseTrainer::setFeedback(const std::string& text, const sf::Color& color) {
    feedback = text;
    feedbackColor = color;
}

// --- text helpers -----------------------------------------------------------

float CourseTrainer::textWidth(const sf::String& s, unsigned int size) const {
    sf::Text t;
    t.setFont(*font);
    t.setCharacterSize(size);
    t.setString(s);
    return t.getLocalBounds().width;
}

void CourseTrainer::drawText(const sf::String& s, float x, float y, unsigned int size,
                             const sf::Color& color, bool bold) {
    sf::Text t;
    t.setFont(*font);
    t.setCharacterSize(size);
    t.setFillColor(color);
    if (bold) t.setStyle(sf::Text::Bold);
    t.setString(s);
    t.setPosition(std::floor(x), std::floor(y));
    window->draw(t);
}

const std::vector<sf::String>& CourseTrainer::wrap(const std::string& utf8, float width,
                                                   unsigned int charSize) const {
    if (annotationCache.source == utf8 && annotationCache.width == width &&
        annotationCache.charSize == charSize) {
        return annotationCache.lines;
    }

    annotationCache.source = utf8;
    annotationCache.width = width;
    annotationCache.charSize = charSize;
    annotationCache.lines.clear();

    sf::Text probe;
    probe.setFont(*font);
    probe.setCharacterSize(charSize);

    const sf::String text = toDisplay(utf8);
    sf::String current;
    sf::String word;

    auto flushWord = [&]() {
        if (word.isEmpty()) return;
        sf::String candidate = current;
        if (!candidate.isEmpty()) candidate += " ";
        candidate += word;
        probe.setString(candidate);
        if (probe.getLocalBounds().width > width && !current.isEmpty()) {
            annotationCache.lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
        word.clear();
    };

    for (std::size_t i = 0; i < text.getSize(); ++i) {
        const sf::Uint32 c = text[i];
        if (c == '\n') {
            flushWord();
            annotationCache.lines.push_back(current);
            current.clear();
        } else if (c == ' ' || c == '\t') {
            flushWord();
        } else {
            word += c;
        }
    }
    flushWord();
    if (!current.isEmpty()) annotationCache.lines.push_back(current);
    return annotationCache.lines;
}

// --- rendering --------------------------------------------------------------

void CourseTrainer::renderBoardOverlay() {
    // Runs under the Game's board view, so everything here is in the board's
    // own 0..352 coordinates and scales with it.
    if (!active || screen != Screen::Study) return;
    renderMarkings();
}

void CourseTrainer::render() {
    if (!active) return;

    renderPanelBackground();
    if (screen == Screen::Chapters) {
        renderChapterPicker();
        return;
    }
    renderMoveTree();
    renderStudyPanel();
}

void CourseTrainer::renderPanelBackground() {
    sf::RectangleShape panel(sf::Vector2f(panelW(), static_cast<float>(window->getSize().y)));
    panel.setPosition(panelX(), 0.0f);
    panel.setFillColor(kPanelBg);
    window->draw(panel);

    if (screen == Screen::Study) {
        sf::RectangleShape tree(sf::Vector2f(treeW(), static_cast<float>(window->getSize().y) - treeY()));
        tree.setPosition(0.0f, treeY());
        tree.setFillColor(kPanelBg2);
        window->draw(tree);
    }
}

void CourseTrainer::renderChapterPicker() {
    sf::RectangleShape overlay(sf::Vector2f(static_cast<float>(window->getSize().x),
                                            static_cast<float>(window->getSize().y)));
    overlay.setFillColor(sf::Color(14, 15, 18, 245));
    window->draw(overlay);

    drawText("Hakobyan Full 1.d4 Course", 60.0f, 22.0f, 22, kText, true);
    drawText(toDisplay(courseDir), 60.0f, 52.0f, 12, kTextDim);

    const sf::Vector2i mouse = sf::Mouse::getPosition(*window);

    for (int row = 0; row < kPickerRows; ++row) {
        const int index = chapterScroll + row;
        if (index >= static_cast<int>(chapterFiles.size())) break;

        const sf::FloatRect rect(60.0f, kPickerTop + row * kPickerRowH,
                                 ui.windowW - 120.0f, kPickerRowH - 2.0f);
        const bool hovered = rect.contains(static_cast<float>(mouse.x), static_cast<float>(mouse.y));

        sf::RectangleShape bg(sf::Vector2f(rect.width, rect.height));
        bg.setPosition(rect.left, rect.top);
        bg.setFillColor(hovered ? sf::Color(52, 58, 68) : sf::Color(30, 33, 38));
        if (index == chapterIndex) bg.setFillColor(sf::Color(46, 68, 96));
        window->draw(bg);

        drawText(toDisplay(course::prettyChapterName(chapterTitles[static_cast<std::size_t>(index)])),
                 rect.left + 12.0f, rect.top + 6.0f, 15, kText);

        const UserDB::ChapterStats& s = chapterStats[static_cast<std::size_t>(index)];
        std::ostringstream right;
        if (s.total == 0) {
            right << "not started";
        } else {
            right << s.learned << " learned";
            if (s.due > 0) right << "   |   " << s.due << " due";
        }
        const sf::String rs = right.str();
        drawText(rs, rect.left + rect.width - textWidth(rs, 13) - 12.0f, rect.top + 8.0f, 13,
                 s.due > 0 ? kWarn : kTextDim);
    }

    drawText("Click a chapter to open it.  Mouse wheel scrolls.  Esc leaves course mode.",
             440.0f, ui.windowH - 36.0f, 13, kTextDim);

    if (!loadError.empty()) {
        drawText(toDisplay(loadError), 60.0f, ui.windowH - 62.0f, 13, kBad);
    } else if (!feedback.empty()) {
        drawText(toDisplay(feedback), 60.0f, ui.windowH - 62.0f, 13, feedbackColor);
    }

    for (auto* b : pickerButtons) b->render();
}

void CourseTrainer::renderStudyPanel() {
    const float x = panelX() + 12.0f;
    const float w = panelW() - 24.0f;

    drawText(toDisplay(course::prettyChapterName(chapter.title)), x, 12.0f, 17, kText, true);

    {
        std::ostringstream sub;
        sub << (mode == Mode::Learn ? "Learn" : "Drill") << "  |  " << lines.size() << " lines";
        if (mode == Mode::Drill && cardIndex >= 0) {
            sub << "  |  line " << (cardIndex + 1) << "  |  ply "
                << drillPos << "/" << lines[static_cast<std::size_t>(cardIndex)].nodeIds.size();
        }
        drawText(sub.str(), x, 38.0f, 13, kTextDim);
    }

    for (auto* b : buttons) b->render();

    // Three rows of buttons end at 174; leave a little air under them.
    float y = 188.0f;
    y = renderEngineLines(x, y);

    if (!feedback.empty()) {
        drawText(toDisplay(feedback), x, y, 15, feedbackColor, true);
    }
    y += 28.0f;

    // The move that was just played, then whatever the author wrote about it.
    const int cur = (mode == Mode::Drill && cardIndex >= 0)
                  ? (drillPos > 0 ? lines[static_cast<std::size_t>(cardIndex)].nodeIds[drillPos - 1] : 0)
                  : currentNode();

    if (cur != 0) {
        drawText(toDisplay(course::formatMoveNumber(node(cur), true)), x, y, 20, kAccent, true);
        y += 30.0f;
    }

    const float statsY = static_cast<float>(window->getSize().y) - 58.0f;
    renderAnnotation(x, y, w, statsY - 12.0f);

    // Scheduling footer.
    sf::RectangleShape rule(sf::Vector2f(w, 1.0f));
    rule.setPosition(x, statsY - 10.0f);
    rule.setFillColor(sf::Color(60, 64, 70));
    window->draw(rule);

    std::ostringstream stats;
    if (chapterIndex >= 0 && chapterIndex < static_cast<int>(chapterStats.size())) {
        const UserDB::ChapterStats& s = chapterStats[static_cast<std::size_t>(chapterIndex)];
        stats << "Chapter: " << s.learned << " learned / " << lines.size() << " lines";
        if (s.due > 0) stats << "   |   " << s.due << " due now";
    }
    drawText(stats.str(), x, statsY, 13, kTextDim);
    drawText("E evaluation   L lines", x + w - textWidth("E evaluation   L lines", 12) , statsY, 12, kTextDim);

    if (mode == Mode::Drill && cardIndex >= 0 && db) {
        UserDB::CourseCard card;
        std::ostringstream sched;
        if (db->getCourseCard(lines[static_cast<std::size_t>(cardIndex)].id, card)) {
            const long long now = static_cast<long long>(std::time(nullptr));
            sched << "This line: seen " << card.repetitions << "x, ease "
                  << std::fixed << std::setprecision(2) << card.ease
                  << ", next in " << humanInterval(card.dueTs - now);
            if (card.lapses > 0) sched << ", " << card.lapses << " lapses";
        } else {
            sched << "This line: new";
        }
        drawText(sched.str(), x, statsY + 20.0f, 13, kTextDim);
    } else {
        drawText("Right/Left step, Up/Down pick a variation, Space next line, Esc chapters",
                 x, statsY + 20.0f, 12, kTextDim);
    }
}

float CourseTrainer::renderEngineLines(float x, float y) {
    if (!linesOn) return y;

    std::ostringstream header;
    header << "Engine";
    if (engineDepth > 0) header << "   depth " << engineDepth;
    drawText(header.str(), x, y, 13, kAccent, true);
    y += 20.0f;

    if (engineLines.empty()) {
        drawText("thinking...", x, y, 13, kTextDim);
        return y + 24.0f;
    }

    // Same green / yellow / orange ranking the arrows on the board use, so a
    // row and an arrow can be matched by colour.
    static const sf::Color rank[3] = {
        sf::Color(0, 200, 0), sf::Color(200, 200, 0), sf::Color(255, 165, 0)
    };

    for (std::size_t i = 0; i < engineLines.size() && i < 3; ++i) {
        sf::CircleShape dot(6.0f);
        dot.setPosition(x, y + 4.0f);
        dot.setFillColor(rank[i]);
        window->draw(dot);

        drawText(toDisplay(engineLines[i].eval), x + 20.0f, y, 14, kText);
        drawText(toDisplay(engineLines[i].pv), x + 74.0f, y + 1.0f, 13, kTextDim);
        y += 22.0f;
    }
    return y + 6.0f;
}

void CourseTrainer::renderAnnotation(float x, float y, float width, float maxY) {
    int cur;
    if (mode == Mode::Drill && cardIndex >= 0) {
        cur = drillPos > 0 ? lines[static_cast<std::size_t>(cardIndex)].nodeIds[drillPos - 1] : 0;
    } else {
        cur = currentNode();
    }

    std::string text = (cur == 0) ? chapter.introComment : node(cur).comment;
    if (text.empty() && cur == 0) {
        text = (mode == Mode::Learn)
             ? "Press Right (or Next) to start walking the chapter."
             : "Play the first move of the line on the board.";
    }
    if (text.empty()) text = "\xe2\x80\x94";

    const unsigned int size = 14;
    const float lineHeight = size + 6.0f;
    for (const sf::String& line : wrap(text, width, size)) {
        if (y + lineHeight > maxY) {
            drawText("...", x, y, size, kTextDim);
            break;
        }
        if (!line.isEmpty()) drawText(line, x, y, size, kText);
        y += lineHeight;
    }
}

void CourseTrainer::renderMoveTree() {
    const float x = 10.0f;
    float y = treeY() + 8.0f;
    const float width = treeW() - 20.0f;
    const float bottom = static_cast<float>(window->getSize().y) - 8.0f;

    optionRows.clear();   // stale rects must not survive a mode or line change
    drawText(mode == Mode::Learn ? "Current line" : "Line being drilled", x, y, 13, kAccent, true);
    y += 20.0f;

    const std::vector<int>& ids = (mode == Mode::Drill && cardIndex >= 0)
                                ? lines[static_cast<std::size_t>(cardIndex)].nodeIds
                                : path;
    const std::size_t played = (mode == Mode::Drill && cardIndex >= 0) ? drillPos : ids.size();

    // Moves flow across the panel and wrap, like a game score.
    float cx = x;
    const unsigned int size = 13;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const course::MoveNode& n = node(ids[i]);
        // In drill the moves not yet reached stay hidden - that is the point.
        const bool hidden = (mode == Mode::Drill && i >= played);
        const sf::String token = hidden
            ? sf::String(n.byWhite ? std::to_string(n.moveNumber) + ". ?" : "?")
            : toDisplay(course::formatMoveNumber(n, i == 0));

        const float tw = textWidth(token, size) + 8.0f;
        if (cx + tw > x + width) { cx = x; y += size + 5.0f; }
        if (y + size > bottom) { drawText("...", cx, y, size, kTextDim); break; }

        const bool isCurrent = (i + 1 == played);
        if (isCurrent) {
            sf::RectangleShape hl(sf::Vector2f(tw - 4.0f, size + 5.0f));
            hl.setPosition(cx - 2.0f, y - 2.0f);
            hl.setFillColor(sf::Color(52, 76, 108));
            window->draw(hl);
        }
        drawText(token, cx, y, size, hidden ? sf::Color(90, 94, 100) : (isCurrent ? kText : kTextDim));
        cx += tw;
    }

    y += size + 16.0f;
    if (y > bottom - 30.0f) return;

    // The branches available from where we stand: the "tree of options".
    if (mode == Mode::Learn) {
        const auto& kids = node(currentNode()).children;
        drawText(kids.empty() ? "No continuations (end of line)"
                              : "Options from here (click one, or Up/Down + Right)",
                 x, y, 13, kAccent, true);
        y += 20.0f;

        for (std::size_t i = 0; i < kids.size(); ++i) {
            if (y + 18.0f > bottom) { drawText("...", x, y, 12, kTextDim); break; }
            const course::MoveNode& n = node(kids[i]);
            const bool armed = (static_cast<int>(i) == variationCursor);

            const sf::FloatRect rect(x - 4.0f, y - 2.0f, width, 18.0f);
            optionRows.emplace_back(rect, static_cast<int>(i));
            const bool hovered = rect.contains(static_cast<float>(hoverMouse.x),
                                               static_cast<float>(hoverMouse.y));

            if (armed || hovered) {
                sf::RectangleShape hl(sf::Vector2f(rect.width, rect.height));
                hl.setPosition(rect.left, rect.top);
                hl.setFillColor(armed ? sf::Color(52, 76, 108) : sf::Color(44, 50, 60));
                window->draw(hl);
            }

            std::ostringstream label;
            label << (i == 0 ? "main  " : "alt   ") << course::formatMoveNumber(n, true);
            if (!n.comment.empty()) label << "   *";
            drawText(toDisplay(label.str()), x, y, 13, (armed || hovered) ? kText : kTextDim);
            y += 18.0f;
        }
    } else if (cardIndex >= 0) {
        std::ostringstream info;
        info << "Mistakes this line: " << mistakes << (hintUsed ? "   (answer shown)" : "");
        drawText(info.str(), x, y, 13, mistakes ? kBad : kTextDim);
        y += 20.0f;
        if (lineComplete) {
            drawText("Space / Skip Line for the next one", x, y, 13, kGood);
        } else {
            drawText(toDisplay(heroIsWhite ? "You are White \xe2\x80\x94 find the course move"
                                           : "You are Black \xe2\x80\x94 find the course move"),
                     x, y, 13, kTextDim);
        }
    }
}

void CourseTrainer::renderMarkings() {
    int cur;
    if (mode == Mode::Drill && cardIndex >= 0) {
        cur = drillPos > 0 ? lines[static_cast<std::size_t>(cardIndex)].nodeIds[drillPos - 1] : 0;
    } else {
        cur = currentNode();
    }

    Coordinate coord;
    auto centre = [&coord](const std::string& square) {
        const std::string s = toBoardSquare(square);
        return sf::Vector2f(static_cast<float>(coord.getX(s) + 22),
                            static_cast<float>(coord.getY(s) + 22));
    };

    // The hint arrow, when the drill gave the move away.
    if (!revealedMove.empty() && revealedMove.size() >= 4) {
        const sf::Vector2f a = centre(revealedMove.substr(0, 2));
        const sf::Vector2f b = centre(revealedMove.substr(2, 2));
        sf::Vector2f d = b - a;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1.0f) {
            sf::RectangleShape shaft(sf::Vector2f(len - 12.0f, 7.0f));
            shaft.setOrigin(0.0f, 3.5f);
            shaft.setPosition(a);
            shaft.setRotation(std::atan2(d.y, d.x) * 180.0f / 3.14159265f);
            shaft.setFillColor(sf::Color(235, 190, 90, 200));
            window->draw(shaft);
        }
    }

    if (cur == 0) return;
    for (const course::Marking& m : node(cur).markings) {
        if (m.to.empty()) {
            sf::CircleShape ring(19.0f);
            ring.setOrigin(19.0f, 19.0f);
            ring.setPosition(centre(m.from));
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(3.0f);
            ring.setOutlineColor(markingColor(m.color, 210));
            window->draw(ring);
        } else {
            const sf::Vector2f a = centre(m.from);
            const sf::Vector2f b = centre(m.to);
            sf::Vector2f d = b - a;
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1.0f) continue;
            sf::RectangleShape shaft(sf::Vector2f(len - 12.0f, 6.0f));
            shaft.setOrigin(0.0f, 3.0f);
            shaft.setPosition(a);
            shaft.setRotation(std::atan2(d.y, d.x) * 180.0f / 3.14159265f);
            shaft.setFillColor(markingColor(m.color, 170));
            window->draw(shaft);

            sf::ConvexShape head(3);
            head.setPoint(0, sf::Vector2f(0.0f, 0.0f));
            head.setPoint(1, sf::Vector2f(-14.0f, -8.0f));
            head.setPoint(2, sf::Vector2f(-14.0f, 8.0f));
            head.setPosition(b);
            head.setRotation(std::atan2(d.y, d.x) * 180.0f / 3.14159265f);
            head.setFillColor(markingColor(m.color, 170));
            window->draw(head);
        }
    }
}
