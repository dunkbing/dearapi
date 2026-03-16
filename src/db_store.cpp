#include "db_store.hpp"
#include <sstream>

DBStore::~DBStore() {
    if (m_db)
        sqlite3_close(m_db);
}

bool DBStore::open(const std::string& path) {
    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK)
        return false;
    initSchema();
    return true;
}

void DBStore::initSchema() {
    exec("PRAGMA journal_mode=WAL");
    exec(R"(
        CREATE TABLE IF NOT EXISTS folders (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            name      TEXT    NOT NULL,
            parent_id INTEGER NOT NULL DEFAULT 0
        )
    )");
    exec(R"(
        CREATE TABLE IF NOT EXISTS saved_requests (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            name      TEXT    NOT NULL,
            folder_id INTEGER NOT NULL DEFAULT 0,
            method    TEXT    NOT NULL DEFAULT 'GET',
            url       TEXT    NOT NULL DEFAULT '',
            headers   TEXT    NOT NULL DEFAULT '',
            body      TEXT    NOT NULL DEFAULT ''
        )
    )");
}

void DBStore::exec(const std::string& sql) const {
    char* err{};
    sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);
    if (err)
        sqlite3_free(err);
}

std::string DBStore::serializeHeaders(const std::map<std::string, std::string>& h) {
    std::string out;
    for (auto& [k, v] : h)
        out += k + ": " + v + "\n";
    return out;
}

std::map<std::string, std::string> DBStore::parseHeaders(const std::string& s) {
    std::map<std::string, std::string> out;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        auto pos = line.find(": ");
        if (pos != std::string::npos)
            out[line.substr(0, pos)] = line.substr(pos + 2);
    }
    return out;
}

// ── folders
// ───────────────────────────────────────────────────────────────────

int64_t DBStore::createFolder(const std::string& name, int64_t parentId) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "INSERT INTO folders (name, parent_id) VALUES (?,?)", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, parentId);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

bool DBStore::renameFolder(int64_t id, const std::string& name) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "UPDATE folders SET name=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

bool DBStore::deleteFolder(int64_t id) {
    for (auto& f : getFolders(id))
        deleteFolder(f.id); // cascade
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "DELETE FROM saved_requests WHERE folder_id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    sqlite3_prepare_v2(m_db, "DELETE FROM folders WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

bool DBStore::moveFolder(int64_t id, int64_t newParentId) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "UPDATE folders SET parent_id=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, newParentId);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

// ── requests
// ──────────────────────────────────────────────────────────────────

int64_t DBStore::saveRequest(const std::string& name, int64_t folderId, const HttpRequest& req) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db,
                       "INSERT INTO saved_requests (name,folder_id,method,url,headers,body)"
                       " VALUES (?,?,?,?,?,?)",
                       -1, &s, nullptr);
    auto hdrs = serializeHeaders(req.headers);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, folderId);
    sqlite3_bind_text(s, 3, req.method.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 4, req.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 5, hdrs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 6, req.body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

bool DBStore::renameRequest(int64_t id, const std::string& name) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "UPDATE saved_requests SET name=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

bool DBStore::updateRequest(int64_t id, const HttpRequest& req) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "UPDATE saved_requests SET method=?,url=?,headers=?,body=? WHERE id=?",
                       -1, &s, nullptr);
    auto hdrs = serializeHeaders(req.headers);
    sqlite3_bind_text(s, 1, req.method.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, req.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, hdrs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 4, req.body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 5, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

bool DBStore::deleteRequest(int64_t id) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "DELETE FROM saved_requests WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

bool DBStore::moveRequest(int64_t id, int64_t newFolderId) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "UPDATE saved_requests SET folder_id=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, newFolderId);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

// ── queries
// ───────────────────────────────────────────────────────────────────

std::vector<FolderInfo> DBStore::getFolders(int64_t parentId) const {
    std::vector<FolderInfo> result;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db,
                       "SELECT id,name,parent_id FROM folders WHERE parent_id=? ORDER BY name", -1,
                       &s, nullptr);
    sqlite3_bind_int64(s, 1, parentId);
    while (sqlite3_step(s) == SQLITE_ROW) {
        result.push_back({sqlite3_column_int64(s, 0),
                          reinterpret_cast<const char*>(sqlite3_column_text(s, 1)),
                          sqlite3_column_int64(s, 2)});
    }
    sqlite3_finalize(s);
    return result;
}

std::vector<SavedRequest> DBStore::getRequests(int64_t folderId) const {
    std::vector<SavedRequest> result;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db,
                       "SELECT id,name,folder_id,method,url,headers,body"
                       " FROM saved_requests WHERE folder_id=? ORDER BY name",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, folderId);
    while (sqlite3_step(s) == SQLITE_ROW) {
        SavedRequest r;
        r.id = sqlite3_column_int64(s, 0);
        r.name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        r.folderId = sqlite3_column_int64(s, 2);
        r.request.method = reinterpret_cast<const char*>(sqlite3_column_text(s, 3));
        r.request.url = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
        auto h = sqlite3_column_text(s, 5);
        r.request.headers = parseHeaders(h ? reinterpret_cast<const char*>(h) : "");
        auto b = sqlite3_column_text(s, 6);
        r.request.body = b ? reinterpret_cast<const char*>(b) : "";
        result.push_back(std::move(r));
    }
    sqlite3_finalize(s);
    return result;
}

SavedRequest DBStore::getRequest(int64_t id) const {
    SavedRequest r;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db,
                       "SELECT id,name,folder_id,method,url,headers,body"
                       " FROM saved_requests WHERE id=?",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    if (sqlite3_step(s) == SQLITE_ROW) {
        r.id = sqlite3_column_int64(s, 0);
        r.name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        r.folderId = sqlite3_column_int64(s, 2);
        r.request.method = reinterpret_cast<const char*>(sqlite3_column_text(s, 3));
        r.request.url = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
        auto h = sqlite3_column_text(s, 5);
        r.request.headers = parseHeaders(h ? reinterpret_cast<const char*>(h) : "");
        auto b = sqlite3_column_text(s, 6);
        r.request.body = b ? reinterpret_cast<const char*>(b) : "";
    }
    sqlite3_finalize(s);
    return r;
}
