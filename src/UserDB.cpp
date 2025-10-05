// UserDB with optional SQLite backend (USE_SQLITE). Falls back to text file otherwise.

#include "UserDB.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

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
        "CREATE TABLE IF NOT EXISTS rating_history (ts INTEGER, delta INTEGER, rating INTEGER);";
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
