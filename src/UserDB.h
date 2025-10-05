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
};

#endif // USER_DB_H
