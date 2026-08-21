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

bool loadChapter(const std::string& path, Chapter& out, std::string& error) {
    // Paths are carried around as UTF-8 (chapter names such as "Grünfeld" have
    // no representation in the ANSI code page), so widen before opening.
    std::ifstream input(std::filesystem::u8path(path));
    if (!input.is_open()) {
        error = "Unable to open " + path;
        return false;
    }

    std::string tagEvent, tagAnnotator, tagWhite;
    std::string movetext;
    bool inMovetext = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);

        if (!inMovetext) {
            if (t.empty()) continue;
            if (t.front() == '[') {
                const std::size_t space = t.find(' ');
                const std::size_t q1 = t.find('"');
                const std::size_t q2 = (q1 == std::string::npos) ? q1 : t.find('"', q1 + 1);
                if (space != std::string::npos && q2 != std::string::npos) {
                    const std::string key = t.substr(1, space - 1);
                    const std::string value = t.substr(q1 + 1, q2 - q1 - 1);
                    if (key == "Event") tagEvent = value;
                    else if (key == "Annotator") tagAnnotator = value;
                    else if (key == "White") tagWhite = value;
                }
                continue;
            }
        }
        inMovetext = true;
        movetext += line;
        movetext.push_back('\n');
    }

    out = Chapter{};
    out.filePath = path;
    out.annotator = tagAnnotator;
    out.title = !tagEvent.empty() ? tagEvent
              : !tagWhite.empty() ? tagWhite
              : std::filesystem::u8path(path).stem().u8string();

    analysis::HeadlessPosition startPosition;

    // Root sentinel: no move, just the starting position.
    MoveNode root;
    root.fen = startPosition.getFEN();
    out.nodes.push_back(root);

    // Position reached after each node, kept only while parsing: entering a
    // variation has to rewind to the branch point's parent, and replaying the
    // whole path each time would be quadratic.
    std::vector<analysis::HeadlessPosition> positionAfter;
    positionAfter.push_back(startPosition);

    analysis::HeadlessPosition position = startPosition;
    std::vector<ParseFrame> stack;
    int parentNode = 0;
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
        const std::string token = movetext.substr(i, j - i);
        i = j;

        if (token.empty()) { ++i; continue; }
        if (isResultToken(token) || isMoveNumberToken(token)) continue;
        if (token == "--" || token == "Z0") continue;   // null move, unsupported

        analysis::Move move;
        if (!position.decodeSan(token, move)) {
            error = out.title + ": could not play '" + token + "' at ply " +
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

    out.introComment = out.nodes[0].comment;
    out.loaded = out.nodes.size() > 1;
    if (!out.loaded) {
        error = out.title + ": no moves found";
        return false;
    }
    return true;
}

std::vector<Line> enumerateLines(const Chapter& chapter) {
    std::vector<Line> lines;
    if (chapter.nodes.size() < 2) return lines;

    // Iterative DFS: children pushed in reverse so the main line comes first.
    struct Step { int node; bool entering; };
    std::vector<Step> work{{0, true}};
    std::vector<int> path;

    while (!work.empty()) {
        const Step step = work.back();
        work.pop_back();

        if (!step.entering) {
            path.pop_back();
            continue;
        }

        const MoveNode& node = chapter.nodes[static_cast<std::size_t>(step.node)];
        if (step.node != 0) path.push_back(step.node);

        if (node.children.empty()) {
            if (!path.empty()) {
                Line line;
                line.nodeIds = path;
                line.id = makeLineId(chapter, path);
                lines.push_back(std::move(line));
            }
            if (step.node != 0) path.pop_back();
            continue;
        }

        if (step.node != 0) work.push_back(Step{step.node, false});
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            work.push_back(Step{*it, true});
        }
    }
    return lines;
}

std::string makeLineId(const Chapter& chapter, const std::vector<int>& nodeIds) {
    // FNV-1a over the chapter title plus the moves. Node indices would not
    // survive a re-parse; the move list does, and it changes if the author
    // edits the line - which is exactly when the card should reset.
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
