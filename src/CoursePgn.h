#ifndef COURSE_PGN_H
#define COURSE_PGN_H

// Parsing of annotated opening-course PGNs into a full variation tree.
// Unlike the bulk analyzer's parser (which throws comments and variations
// away), this keeps everything the author wrote: nested variations, text
// annotations, NAGs and the [%csl]/[%cal] board drawings.

#include <cstdint>
#include <string>
#include <vector>

namespace course {

// A drawing the author attached to a position via [%csl] / [%cal].
struct Marking {
    std::string from;   // "d6" (the only square for a circle)
    std::string to;     // "" for a circle, target square for an arrow
    char color = 'G';   // G green, R red, Y yellow, B blue
};

// One half-move in the tree. nodes[0] of a chapter is a root sentinel with
// no move on it; every other node carries the move that reached it.
struct MoveNode {
    std::string san;        // "Bxc3+"
    std::string uci;        // "b4c3"
    std::string comment;    // author's prose for the position after the move
    std::string fen;        // FEN after the move
    std::vector<int> nags;  // 1 => !, 5 => !?, 16 => +/- ...
    std::vector<Marking> markings;

    int parent = -1;
    std::vector<int> children; // children[0] is the main continuation
    int ply = 0;               // 1-based; root is 0
    int moveNumber = 1;        // full move number this half-move belongs to
    bool byWhite = true;
};

struct Chapter {
    std::string title;      // from the Event tag, else the file name
    std::string filePath;
    std::string annotator;
    std::string introComment;   // text before the very first move

    // A course file may hold one game (an opening chapter) or many (an endgame
    // manual, where each game is a position to study). Every game contributes
    // a root sentinel to `nodes` carrying its starting FEN, so games that begin
    // from different positions can share one flat node array.
    std::vector<MoveNode> nodes;
    std::vector<int> gameRoots;
    std::vector<std::string> gameLabels;   // "Game 12", from the Site/Black tag

    bool loaded = false;
};

// A single root-to-leaf path through one game's tree: one thing to memorise.
struct Line {
    std::string id;             // stable across re-parses of the same PGN
    std::vector<int> nodeIds;   // tree nodes, root excluded
    int root = 0;               // the game this line belongs to
};

// Reads one chapter PGN. Only the first game in the file is used; course
// exports put one chapter per file.
bool loadChapter(const std::string& path, Chapter& out, std::string& error);

// Every root-to-leaf path, main line first, then variations in the order the
// author wrote them.
std::vector<Line> enumerateLines(const Chapter& chapter);

// Stable id for a line: chapter title, the position it starts from, and the
// moves that make it up. An endgame manual repeats short move sequences from
// many different positions, so the starting FEN has to be part of the identity.
std::string makeLineId(const Chapter& chapter, int root, const std::vector<int>& nodeIds);

// Turns the author's raw annotation into something renderable: markdown and
// the [%csl]/[%cal] drawing commands come out, "[--]" becomes a blank line.
std::string cleanComment(const std::string& raw);

// "!", "!?", "+/-" ... empty for NAGs we don't render.
std::string nagToSymbol(int nag);

// Move in "12. Bxc3+" / "12... Bxc3+" form, for move-list display.
std::string formatMoveNumber(const MoveNode& node, bool forceNumber = false);

// Lists *.pgn in a directory, sorted by name (the course files are numbered).
std::vector<std::string> findChapterFiles(const std::string& directory);

// Chapter title without the leading sort prefix ("07 - 5) The Old Indian" ->
// "5) The Old Indian").
std::string prettyChapterName(const std::string& title);

} // namespace course

#endif // COURSE_PGN_H
