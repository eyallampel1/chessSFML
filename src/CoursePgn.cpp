#include "CoursePgn.h"

#include "HeadlessPosition.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace course {
namespace {

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool isResultToken(const std::string& t) {
    return t == "*" || t == "1-0" || t == "0-1" || t == "1/2-1/2";
}

// "12." / "12..." / "12" - move numbers carry nothing the tree does not
// already know from whose turn it is.
bool isMoveNumberToken(const std::string& t) {
    std::size_t i = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) ++i;
    if (i == 0) return false;
    while (i < t.size() && t[i] == '.') ++i;
    return i == t.size();
}

// Not every exporter puts a space after the move number: "1.Ka6" and
// "2...Kb8" are as common as "1. Ka6". Returns the move on its own.
std::string stripMoveNumber(const std::string& token) {
    std::size_t i = 0;
    while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) ++i;
    if (i == 0 || i >= token.size() || token[i] != '.') return token;
    while (i < token.size() && token[i] == '.') ++i;
    return (i < token.size()) ? token.substr(i) : token;
}

// Pulls [%csl Rd6] / [%cal Gd1d8,Re4e5] out of a comment, appending what it
// finds to `out` and erasing it from the text.
void extractMarkings(std::string& text, std::vector<Marking>& out) {
    std::size_t pos = 0;
    while ((pos = text.find("[%", pos)) != std::string::npos) {
        const std::size_t close = text.find(']', pos);
        if (close == std::string::npos) break;

        const std::string body = text.substr(pos + 2, close - pos - 2);
        const std::size_t space = body.find(' ');
        if (space != std::string::npos) {
            const std::string cmd = body.substr(0, space);
            if (cmd == "csl" || cmd == "cal") {
                std::istringstream items(body.substr(space + 1));
                std::string item;
                while (std::getline(items, item, ',')) {
                    item = trim(item);
                    if (item.size() < 3) continue;
                    Marking m;
                    m.color = item[0];
                    m.from = item.substr(1, 2);
                    if (item.size() >= 5) m.to = item.substr(3, 2);
                    out.push_back(m);
                }
            }
        }
        text.erase(pos, close - pos + 1);
    }
}

struct ParseFrame {
    int lastNode = -1;   // the move this variation is an alternative to
    int parentNode = 0;  // where nodes created outside the variation hang off
};

} // namespace

std::string nagToSymbol(int nag) {
    switch (nag) {
        case 1:  return "!";
        case 2:  return "?";
        case 3:  return "!!";
        case 4:  return "??";
        case 5:  return "!?";
        case 6:  return "?!";
        case 10:
        case 11: return "=";
        case 13: return "unclear";
        case 14: return "+/=";
        case 15: return "=/+";
        case 16: return "+/-";
        case 17: return "-/+";
        case 18: return "+-";
        case 19: return "-+";
        case 22:
        case 23: return "zugzwang";
        case 36:
        case 37: return "initiative";
        case 40:
        case 41: return "attack";
        case 132:
        case 133: return "counterplay";
        case 146: return "N";
        default: return {};
    }
}

std::string cleanComment(const std::string& raw) {
    std::string text = raw;

    // The author uses "[--]" as a paragraph break.
    for (std::size_t pos = 0; (pos = text.find("[--]", pos)) != std::string::npos; ) {
        text.replace(pos, 4, "\n\n");
        pos += 2;
    }

    // Chessable exports carry their own markup. An inline diagram is a FEN the
    // reader cannot use as text, so it goes; the bracket markers are just
    // parentheses spelled the long way.
    for (std::size_t pos = 0; (pos = text.find("@@StartFEN@@", pos)) != std::string::npos; ) {
        const std::size_t end = text.find("@@EndFEN@@", pos);
        if (end == std::string::npos) { text.erase(pos); break; }
        text.erase(pos, end + 10 - pos);
    }
    auto replaceAll = [&text](const std::string& from, const std::string& to) {
        for (std::size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos; ) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll("@@StartBracket@@", "(");
    replaceAll("@@EndBracket@@", ")");

    // Markdown emphasis around key squares ("the **d6** -pawn").
    for (std::size_t pos = 0; (pos = text.find("**", pos)) != std::string::npos; ) {
        text.erase(pos, 2);
    }

    // Chess.com escapes the dot of move numbers written inside prose.
    for (std::size_t pos = 0; (pos = text.find("\\.", pos)) != std::string::npos; ) {
        text.erase(pos, 1);
        pos += 1;
    }

    // Collapse runs of whitespace but keep the paragraph breaks.
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    int newlines = 0;
    for (char c : text) {
        if (c == '\n') {
            ++newlines;
            pendingSpace = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            pendingSpace = true;
            continue;
        }
        if (newlines > 0) {
            if (!out.empty()) out.append(newlines >= 2 ? "\n\n" : " ");
            newlines = 0;
        } else if (pendingSpace && !out.empty()) {
            out.push_back(' ');
        }
        pendingSpace = false;
        out.push_back(c);
    }
    return trim(out);
}

std::string formatMoveNumber(const MoveNode& node, bool forceNumber) {
    if (node.ply == 0) return {};
    std::ostringstream oss;
    if (node.byWhite) {
        oss << node.moveNumber << ". ";
    } else if (forceNumber) {
        oss << node.moveNumber << "... ";
    }
    oss << node.san;
    for (int nag : node.nags) {
        const std::string sym = nagToSymbol(nag);
        if (sym.empty()) continue;
        oss << (sym.size() <= 2 ? "" : " ") << sym;
    }
    return oss.str();
}

namespace {

// "[Event \"...\"]" and friends. Used to spot where the next game starts, so a
// line beginning with '[' inside prose is not mistaken for a header.
bool looksLikeTagPair(const std::string& t) {
    if (t.size() < 5 || t.front() != '[' || t.back() != ']') return false;
    std::size_t i = 1;
    while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
    if (i == 1 || i >= t.size() || t[i] != ' ') return false;
    return t.find('"', i) != std::string::npos;
}

// Parses one game's movetext into `out`, hanging it off a fresh root sentinel
// that carries the position the game starts from. `positionAfter` stays index
// aligned with `out.nodes`.
bool parseGame(const std::string& movetext, const std::string& startFen,
               const std::string& label, const std::string& displayLabel, Chapter& out,
               std::vector<analysis::HeadlessPosition>& positionAfter,
               std::string& error) {
    analysis::HeadlessPosition startPosition;
    if (!startFen.empty() && !startPosition.setFEN(startFen)) {
        error = label + ": unreadable FEN \"" + startFen + "\"";
        return false;
    }

    // Root sentinel: no move, just the starting position.
    const int rootNode = static_cast<int>(out.nodes.size());
    MoveNode root;
    root.fen = startPosition.getFEN();
    out.nodes.push_back(root);
    positionAfter.push_back(startPosition);

    analysis::HeadlessPosition position = startPosition;
    std::vector<ParseFrame> stack;
    int parentNode = rootNode;
    int lastNode = -1;
    std::string pendingComment;

    const std::size_t n = movetext.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = movetext[i];

        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (c == '{') {
            const std::size_t close = movetext.find('}', i + 1);
            const std::size_t end = (close == std::string::npos) ? n : close;
            std::string body = movetext.substr(i + 1, end - i - 1);
            i = (close == std::string::npos) ? n : close + 1;

            std::vector<Marking> markings;
            extractMarkings(body, markings);
            const std::string cleaned = cleanComment(body);

            if (lastNode >= 0) {
                MoveNode& node = out.nodes[static_cast<std::size_t>(lastNode)];
                if (!cleaned.empty()) {
                    if (!node.comment.empty()) node.comment += "\n\n";
                    node.comment += cleaned;
                }
                node.markings.insert(node.markings.end(), markings.begin(), markings.end());
            } else if (!cleaned.empty()) {
                // Before any move in this branch: hand it to the move that
                // comes next.
                if (!pendingComment.empty()) pendingComment += "\n\n";
                pendingComment += cleaned;
            }
            continue;
        }

        if (c == ';') {   // rest-of-line comment
            const std::size_t nl = movetext.find('\n', i);
            i = (nl == std::string::npos) ? n : nl + 1;
            continue;
        }

        if (c == '(') {
            ++i;
            if (lastNode < 0) continue;   // nothing to branch away from
            stack.push_back(ParseFrame{lastNode, parentNode});

            // A variation replaces the move just played, so rewind to the
            // position that move was made from.
            parentNode = out.nodes[static_cast<std::size_t>(lastNode)].parent;
            position = positionAfter[static_cast<std::size_t>(parentNode)];
            lastNode = -1;
            pendingComment.clear();
            continue;
        }

        if (c == ')') {
            ++i;
            if (stack.empty()) continue;
            const ParseFrame frame = stack.back();
            stack.pop_back();
            lastNode = frame.lastNode;
            parentNode = frame.parentNode;
            position = positionAfter[static_cast<std::size_t>(lastNode)];
            pendingComment.clear();
            continue;
        }

        if (c == '$') {
            std::size_t j = i + 1;
            int value = 0;
            while (j < n && std::isdigit(static_cast<unsigned char>(movetext[j]))) {
                value = value * 10 + (movetext[j] - '0');
                ++j;
            }
            i = j;
            if (lastNode >= 0) out.nodes[static_cast<std::size_t>(lastNode)].nags.push_back(value);
            continue;
        }

        if (c == '<' || c == '>') { ++i; continue; }

        std::size_t j = i;
        while (j < n) {
            const char d = movetext[j];
            if (std::isspace(static_cast<unsigned char>(d))) break;
            if (d == '{' || d == '}' || d == '(' || d == ')' || d == '$' || d == ';') break;
            ++j;
        }
        const std::string rawToken = movetext.substr(i, j - i);
        i = j;

        if (rawToken.empty()) { ++i; continue; }
        if (isResultToken(rawToken) || isMoveNumberToken(rawToken)) continue;

        const std::string token = stripMoveNumber(rawToken);
        if (token.empty()) continue;
        // Null moves ("3.--" as well as bare "--") have no board meaning, and
        // skipping one would put every later move on the wrong turn, so stop
        // reading this game here. Must be tested before the punctuation guard
        // below, which "--" would otherwise fall into.
        if (token == "--" || token == "Z0") break;
        // A stray annotation mark on its own ("+" adrift from its move) is
        // punctuation, not a move.
        if (!std::any_of(token.begin(), token.end(),
                         [](unsigned char c) { return std::isalnum(c) != 0; })) {
            continue;
        }

        analysis::Move move;
        if (!position.decodeSan(token, move)) {
            error = label + ": could not play '" + token + "' at ply " +
                    std::to_string(out.nodes[static_cast<std::size_t>(parentNode)].ply + 1);
            return false;
        }

        MoveNode node;
        node.san = token;
        node.uci = move.toUci();
        node.parent = parentNode;
        node.byWhite = (position.sideToMove() == analysis::PieceColor::White);
        node.moveNumber = position.fullmoveNumber();
        node.ply = out.nodes[static_cast<std::size_t>(parentNode)].ply + 1;
        node.comment = pendingComment;
        pendingComment.clear();

        position.applyMove(move);
        node.fen = position.getFEN();

        const int id = static_cast<int>(out.nodes.size());
        out.nodes.push_back(node);
        positionAfter.push_back(position);
        out.nodes[static_cast<std::size_t>(parentNode)].children.push_back(id);

        parentNode = id;
        lastNode = id;
    }

    // A header with no moves - an endgame manual has a few - is not an error,
    // there is just nothing to study, so drop the empty root again.
    if (out.nodes.size() == static_cast<std::size_t>(rootNode) + 1) {
        out.nodes.pop_back();
        positionAfter.pop_back();
        return true;
    }

    out.gameRoots.push_back(rootNode);
    out.gameLabels.push_back(displayLabel);
    return true;
}

} // namespace

bool loadChapter(const std::string& path, Chapter& out, std::string& error) {
    // Paths are carried around as UTF-8 (chapter names such as "Grünfeld" have
    // no representation in the ANSI code page), so widen before opening.
    std::ifstream input(std::filesystem::u8path(path));
    if (!input.is_open()) {
        error = "Unable to open " + path;
        return false;
    }

    out = Chapter{};
    out.filePath = path;

    // Position reached after each node, kept only while parsing: entering a
    // variation has to rewind to the branch point's parent, and replaying the
    // whole path each time would be quadratic.
    std::vector<analysis::HeadlessPosition> positionAfter;

    std::string tagEvent, tagAnnotator, tagWhite, tagBlack, tagSite, tagFen;
    std::string movetext;
    bool inMovetext = false;
    std::string firstError;
    std::size_t gameCount = 0;
    std::size_t failed = 0;

    auto finishGame = [&]() {
        if (!inMovetext && tagEvent.empty() && tagFen.empty()) return;
        ++gameCount;

        if (out.title.empty()) {
            out.title = !tagEvent.empty() ? tagEvent
                      : !tagWhite.empty() ? tagWhite
                      : std::filesystem::u8path(path).stem().u8string();
        }
        if (out.annotator.empty()) out.annotator = tagAnnotator;

        // A collection numbers its games per chapter, so "Game 16" occurs
        // dozens of times; the Event tag is what tells them apart.
        // Exports disagree about which tag carries the name: one puts a
        // position number in Site, another the variation name in Black and
        // leaves Event as "?". Take the most specific one actually filled in.
        auto useful = [](const std::string& s) { return !s.empty() && s != "?"; };
        std::string display;
        if      (useful(tagSite))  display = tagSite;
        else if (useful(tagBlack)) display = tagBlack;
        else if (useful(tagEvent)) display = tagEvent;
        else                       display = "Game " + std::to_string(gameCount);

        // Errors get the Event as well, since a collection restarts its game
        // numbering in every chapter and "Game 16" alone occurs dozens of times.
        std::string label = display;
        if (useful(tagEvent) && tagEvent != display) label = tagEvent + " " + display;

        std::string err;
        if (!parseGame(movetext, tagFen, label, display, out, positionAfter, err)) {
            // One unreadable game in a large collection should not cost the
            // reader the other sixteen hundred.
            ++failed;
            if (firstError.empty()) firstError = err;
        }

        tagEvent.clear(); tagAnnotator.clear(); tagWhite.clear();
        tagBlack.clear(); tagSite.clear(); tagFen.clear();
        movetext.clear();
        inMovetext = false;
    };

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);

        if (looksLikeTagPair(t)) {
            if (inMovetext) finishGame();   // this header starts the next game

            const std::size_t space = t.find(' ');
            const std::size_t q1 = t.find('"');
            const std::size_t q2 = (q1 == std::string::npos) ? q1 : t.find('"', q1 + 1);
            if (space != std::string::npos && q2 != std::string::npos) {
                const std::string key = t.substr(1, space - 1);
                const std::string value = t.substr(q1 + 1, q2 - q1 - 1);
                if (key == "Event") tagEvent = value;
                else if (key == "Annotator") tagAnnotator = value;
                else if (key == "White") tagWhite = value;
                else if (key == "Site") tagSite = value;
                else if (key == "Black") tagBlack = value;
                else if (key == "FEN") tagFen = value;
            }
            continue;
        }
        if (t.empty() && !inMovetext) continue;

        inMovetext = true;
        movetext += line;
        movetext.push_back('\n');
    }
    finishGame();

    // A collection spans many Events, so the first one is a poor name for the
    // whole file; only a single-game file is fairly described by its own tag.
    if (out.title.empty() || gameCount > 1) {
        out.title = std::filesystem::u8path(path).stem().u8string();
    }

    out.loaded = !out.gameRoots.empty();
    if (!out.loaded) {
        error = out.title + ": no playable games found"
              + (firstError.empty() ? std::string() : " (" + firstError + ")");
        return false;
    }
    if (failed > 0) {
        error = std::to_string(failed) + " of " + std::to_string(gameCount) +
                " games skipped (" + firstError + ")";
    }

    out.introComment = out.nodes[static_cast<std::size_t>(out.gameRoots.front())].comment;
    return true;
}

std::vector<Line> enumerateLines(const Chapter& chapter) {
    std::vector<Line> lines;

    // Every game in the file contributes its own root-to-leaf paths.
    for (int root : chapter.gameRoots) {
        // Iterative DFS: children pushed in reverse so the main line comes first.
        struct Step { int node; bool entering; };
        std::vector<Step> work{{root, true}};
        std::vector<int> path;

        while (!work.empty()) {
            const Step step = work.back();
            work.pop_back();

            if (!step.entering) {
                path.pop_back();
                continue;
            }

            const MoveNode& node = chapter.nodes[static_cast<std::size_t>(step.node)];
            if (step.node != root) path.push_back(step.node);

            if (node.children.empty()) {
                if (!path.empty()) {
                    Line line;
                    line.nodeIds = path;
                    line.root = root;
                    line.id = makeLineId(chapter, root, path);
                    lines.push_back(std::move(line));
                }
                if (step.node != root) path.pop_back();
                continue;
            }

            if (step.node != root) work.push_back(Step{step.node, false});
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
                work.push_back(Step{*it, true});
            }
        }
    }
    return lines;
}

std::string makeLineId(const Chapter& chapter, int root, const std::vector<int>& nodeIds) {
    // FNV-1a over the chapter title, the starting position and the moves. Node
    // indices would not survive a re-parse; this does, and it changes if the
    // author edits the line - which is exactly when the card should reset. The
    // starting FEN matters because an endgame manual plays the same short
    // sequences from many different positions.
    std::uint64_t hash = 1469598103934665603ULL;
    auto feed = [&hash](const std::string& s) {
        for (unsigned char ch : s) {
            hash ^= ch;
            hash *= 1099511628211ULL;
        }
        hash ^= static_cast<unsigned char>('|');
        hash *= 1099511628211ULL;
    };
    feed(chapter.title);
    feed(chapter.nodes[static_cast<std::size_t>(root)].fen);
    for (int id : nodeIds) feed(chapter.nodes[static_cast<std::size_t>(id)].uci);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

std::vector<std::string> findChapterFiles(const std::string& directory) {
    // Returns UTF-8 paths: path::string() would throw on chapter files whose
    // name has no ANSI representation ("13) The Grünfeld Defense").
    std::vector<std::string> files;
    std::error_code ec;
    std::filesystem::directory_iterator it(std::filesystem::u8path(directory), ec);
    if (ec) return files;

    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
        std::string ext = it->path().extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == ".pgn") files.push_back(it->path().u8string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string prettyChapterName(const std::string& title) {
    // "07 - 5) The Old Indian ..." -> "5) The Old Indian ..."
    const std::size_t dash = title.find(" - ");
    if (dash != std::string::npos && dash <= 4) return title.substr(dash + 3);
    return title;
}

} // namespace course
