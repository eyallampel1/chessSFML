// UserDB with optional SQLite backend (USE_SQLITE). Falls back to text file otherwise.

#include "UserDB.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <ctime>

#include <sqlite3.h>
static sqlite3* g_db = nullptr;

static inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

UserDB::UserDB(const std::string& path) : dbPath(path) {}

bool UserDB::load() {
    reviewQueue.clear();
    if (!initSQLite()) return false;
    readSettingsSQLite();
    readReviewQueueSQLite();
    return true;
}

bool UserDB::save() const {
    writeSettingsSQLite();
    return true;
}

void UserDB::pushReview(const std::string& id) {
    insertReviewSQLite(id);
    auto it = std::find(reviewQueue.begin(), reviewQueue.end(), id);
    if (it == reviewQueue.end()) reviewQueue.push_back(id);
}

bool UserDB::popReview(std::string& id) {
    if (popReviewSQLite(id)) {
        if (!id.empty()) {
            auto it = std::find(reviewQueue.begin(), reviewQueue.end(), id);
            if (it != reviewQueue.end()) reviewQueue.erase(it);
        }
        return true;
    }
    return false;
}

void UserDB::removeFromReview(const std::string& id) {
    deleteReviewSQLite(id);
    auto it = std::remove(reviewQueue.begin(), reviewQueue.end(), id);
    if (it != reviewQueue.end()) reviewQueue.erase(it, reviewQueue.end());
}

void UserDB::adjustRating(int delta) {
    rating += delta; if (rating < 400) rating = 400;
    writeSettingsSQLite();
}

bool UserDB::initSQLite() {
    if (g_db) return true;
    if (sqlite3_open(dbPath.c_str(), &g_db) != SQLITE_OK) {
        g_db = nullptr; return false;
    }
    const char* ddl =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT);"
        "CREATE TABLE IF NOT EXISTS review_queue (id TEXT PRIMARY KEY, added_ts INTEGER DEFAULT (strftime('%s','now')));"
        "CREATE TABLE IF NOT EXISTS rating_history (ts INTEGER, delta INTEGER, rating INTEGER);"
        "CREATE TABLE IF NOT EXISTS course_cards ("
        "  id TEXT PRIMARY KEY,"
        "  chapter TEXT NOT NULL,"
        "  line TEXT NOT NULL,"
        "  ease REAL NOT NULL DEFAULT 2.5,"
        "  interval_days INTEGER NOT NULL DEFAULT 0,"
        "  repetitions INTEGER NOT NULL DEFAULT 0,"
        "  lapses INTEGER NOT NULL DEFAULT 0,"
        "  due_ts INTEGER NOT NULL DEFAULT 0,"
        "  last_ts INTEGER NOT NULL DEFAULT 0);"
        "CREATE INDEX IF NOT EXISTS idx_course_cards_chapter ON course_cards(chapter, due_ts);";
    char* err = nullptr;
    if (sqlite3_exec(g_db, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        sqlite3_close(g_db); g_db = nullptr; return false;
    }
    return true;
}

void UserDB::closeSQLite() const {
    if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
}

static std::string selectSetting(const char* key) {
    if (!g_db) return std::string();
    std::string val;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT value FROM settings WHERE key=?1", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* txt = sqlite3_column_text(stmt, 0);
            if (txt) val = reinterpret_cast<const char*>(txt);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return val;
}

static void upsertSetting(const char* key, const std::string& value) {
    if (!g_db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "INSERT INTO settings(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

void UserDB::readSettingsSQLite() {
    std::string r = selectSetting("rating");
    if (!r.empty()) try { rating = std::stoi(r); } catch (...) {}
    csvPath = selectSetting("csv");
}

void UserDB::writeSettingsSQLite() const {
    upsertSetting("rating", std::to_string(rating));
    upsertSetting("csv", csvPath);
}

void UserDB::readReviewQueueSQLite() {
    reviewQueue.clear();
    if (!g_db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT id FROM review_queue ORDER BY added_ts ASC", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* txt = sqlite3_column_text(stmt, 0);
            if (txt) reviewQueue.emplace_back(reinterpret_cast<const char*>(txt));
        }
    }
    if (stmt) sqlite3_finalize(stmt);
}

void UserDB::clearReviewQueueSQLite() const {
    if (!g_db) return;
    sqlite3_exec(g_db, "DELETE FROM review_queue", nullptr, nullptr, nullptr);
}

void UserDB::insertReviewSQLite(const std::string& id) const {
    if (!g_db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "INSERT OR IGNORE INTO review_queue(id) VALUES(?1)", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

bool UserDB::popReviewSQLite(std::string& id) {
    if (!g_db) return false;
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, "SELECT id FROM review_queue ORDER BY added_ts ASC LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* txt = sqlite3_column_text(stmt, 0);
            if (txt) { id = reinterpret_cast<const char*>(txt); ok = true; }
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    if (ok) deleteReviewSQLite(id);
    return ok;
}

void UserDB::deleteReviewSQLite(const std::string& id) const {
    if (!g_db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "DELETE FROM review_queue WHERE id=?1", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

// --- Free-form settings -----------------------------------------------------

std::string UserDB::getSetting(const std::string& key) const {
    return selectSetting(key.c_str());
}

void UserDB::setSetting(const std::string& key, const std::string& value) {
    upsertSetting(key.c_str(), value);
}

// --- Opening-course spaced repetition ---------------------------------------

void UserDB::ensureCourseSchema() const {
    if (!g_db) return;
    static const char* ddl =
        "CREATE TABLE IF NOT EXISTS course_cards ("
        "  id TEXT PRIMARY KEY,"
        "  chapter TEXT NOT NULL,"
        "  line TEXT NOT NULL,"
        "  ease REAL NOT NULL DEFAULT 2.5,"
        "  interval_days INTEGER NOT NULL DEFAULT 0,"
        "  repetitions INTEGER NOT NULL DEFAULT 0,"
        "  lapses INTEGER NOT NULL DEFAULT 0,"
        "  due_ts INTEGER NOT NULL DEFAULT 0,"
        "  last_ts INTEGER NOT NULL DEFAULT 0);"
        "CREATE INDEX IF NOT EXISTS idx_course_cards_chapter ON course_cards(chapter, due_ts);";
    sqlite3_exec(g_db, ddl, nullptr, nullptr, nullptr);
}

bool UserDB::getCourseCard(const std::string& id, CourseCard& out) const {
    if (!g_db) return false;
    ensureCourseSchema();
    sqlite3_stmt* stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(g_db,
            "SELECT id,chapter,line,ease,interval_days,repetitions,lapses,due_ts,last_ts"
            " FROM course_cards WHERE id=?1", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = [&](int col) {
                const unsigned char* t = sqlite3_column_text(stmt, col);
                return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
            };
            out.id = text(0);
            out.chapter = text(1);
            out.line = text(2);
            out.ease = sqlite3_column_double(stmt, 3);
            out.intervalDays = sqlite3_column_int(stmt, 4);
            out.repetitions = sqlite3_column_int(stmt, 5);
            out.lapses = sqlite3_column_int(stmt, 6);
            out.dueTs = sqlite3_column_int64(stmt, 7);
            out.lastTs = sqlite3_column_int64(stmt, 8);
            found = true;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return found;
}

void UserDB::gradeCourseCard(const std::string& id, const std::string& chapter,
                             const std::string& line, int quality) {
    if (!g_db) return;
    ensureCourseSchema();
    if (quality < 0) quality = 0;
    if (quality > 5) quality = 5;

    CourseCard card;
    if (!getCourseCard(id, card)) {
        card.id = id;
    }
    card.chapter = chapter;
    card.line = line;

    const long long now = static_cast<long long>(std::time(nullptr));

    if (quality < 3) {
        // Failed: back to the start of the ladder and due again this session.
        card.repetitions = 0;
        card.intervalDays = 0;
        card.lapses += 1;
        card.dueTs = now + 10 * 60;   // ten minutes, so it comes round again today
    } else {
        card.repetitions += 1;
        if (card.repetitions == 1)      card.intervalDays = 1;
        else if (card.repetitions == 2) card.intervalDays = 6;
        else {
            double next = card.intervalDays * card.ease;
            card.intervalDays = static_cast<int>(next + 0.5);
            if (card.intervalDays < 1) card.intervalDays = 1;
        }
        card.dueTs = now + static_cast<long long>(card.intervalDays) * 86400LL;
    }

    // SM-2 ease update; the floor keeps a repeatedly-missed line from being
    // scheduled into oblivion.
    const double q = static_cast<double>(quality);
    card.ease += 0.1 - (5.0 - q) * (0.08 + (5.0 - q) * 0.02);
    if (card.ease < 1.3) card.ease = 1.3;
    if (card.ease > 3.0) card.ease = 3.0;
    card.lastTs = now;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO course_cards(id,chapter,line,ease,interval_days,repetitions,lapses,due_ts,last_ts)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)"
            " ON CONFLICT(id) DO UPDATE SET chapter=excluded.chapter, line=excluded.line,"
            " ease=excluded.ease, interval_days=excluded.interval_days,"
            " repetitions=excluded.repetitions, lapses=excluded.lapses,"
            " due_ts=excluded.due_ts, last_ts=excluded.last_ts", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, card.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, card.chapter.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, card.line.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, card.ease);
        sqlite3_bind_int(stmt, 5, card.intervalDays);
        sqlite3_bind_int(stmt, 6, card.repetitions);
        sqlite3_bind_int(stmt, 7, card.lapses);
        sqlite3_bind_int64(stmt, 8, card.dueTs);
        sqlite3_bind_int64(stmt, 9, card.lastTs);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

UserDB::ChapterStats UserDB::getChapterStats(const std::string& chapter, long long nowTs) const {
    ChapterStats stats;
    if (!g_db) return stats;
    ensureCourseSchema();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db,
            "SELECT COUNT(*),"
            "       SUM(CASE WHEN due_ts<=?2 THEN 1 ELSE 0 END),"
            "       SUM(CASE WHEN repetitions>0 THEN 1 ELSE 0 END)"
            " FROM course_cards WHERE chapter=?1", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, chapter.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, nowTs);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.total = sqlite3_column_int(stmt, 0);
            stats.due = sqlite3_column_int(stmt, 1);
            stats.learned = sqlite3_column_int(stmt, 2);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return stats;
}
