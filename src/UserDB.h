#ifndef USER_DB_H
#define USER_DB_H

#include <string>
#include <vector>
#include <unordered_set>

// Very small local persistence using a simple key=value text file.
// Format (lines):
// rating=1500
// csv=C:\\path\\to\\puzzles.csv
// review=id1,id2,id3
class UserDB {
public:
    explicit UserDB(const std::string& path);

    bool load();
    bool save() const;

    int getRating() const { return rating; }
    void setRating(int r) { rating = r; }
    void adjustRating(int delta);

    const std::string& getLastCsvPath() const { return csvPath; }
    void setLastCsvPath(const std::string& p) { csvPath = p; }
    bool isSQLiteEnabled() const { return true; }

    // Free-form settings, for things that don't deserve their own accessor
    // (the opening course directory, the chapter last studied, ...).
    std::string getSetting(const std::string& key) const;
    void setSetting(const std::string& key, const std::string& value);

    // --- Opening-course spaced repetition (SM-2) ---
    // One card is one root-to-leaf line of a course chapter.
    struct CourseCard {
        std::string id;
        std::string chapter;
        std::string line;           // space-separated UCI moves
        double ease = 2.5;
        int intervalDays = 0;
        int repetitions = 0;
        int lapses = 0;
        long long dueTs = 0;        // unix seconds; 0 => never studied
        long long lastTs = 0;
    };

    bool getCourseCard(const std::string& id, CourseCard& out) const;
    // quality is the SM-2 grade 0..5; anything below 3 counts as a lapse.
    void gradeCourseCard(const std::string& id, const std::string& chapter,
                         const std::string& line, int quality);

    struct ChapterStats {
        int total = 0;      // cards this chapter has a record for
        int due = 0;        // of those, how many are due now
        int learned = 0;    // seen at least once and not lapsed back to zero
    };
    ChapterStats getChapterStats(const std::string& chapter, long long nowTs) const;

    // Review queue API
    const std::vector<std::string>& getReviewQueue() const { return reviewQueue; }
    void pushReview(const std::string& id);
    bool popReview(std::string& id);
    void removeFromReview(const std::string& id);

private:
    std::string dbPath;
    int rating = 1500;
    std::string csvPath;
    std::vector<std::string> reviewQueue;

    // SQLite internals
    void closeSQLite() const;
    bool initSQLite();
    void readSettingsSQLite();
    void writeSettingsSQLite() const;
    void readReviewQueueSQLite();
    void clearReviewQueueSQLite() const;
    void insertReviewSQLite(const std::string& id) const;
    bool popReviewSQLite(std::string& id);
    void deleteReviewSQLite(const std::string& id) const;
    void ensureCourseSchema() const;
};

#endif // USER_DB_H
