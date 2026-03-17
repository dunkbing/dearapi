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

bool DBStore::tableExists(const std::string& name) const {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "SELECT name FROM sqlite_master WHERE type='table' AND name=?", -1, &s,
                       nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(s) == SQLITE_ROW;
    sqlite3_finalize(s);
    return found;
}

void DBStore::initSchema() {
    exec("PRAGMA journal_mode=WAL");

    if (!tableExists("collections")) {
        // create collections table
        exec(R"(
            CREATE TABLE IF NOT EXISTS collections (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT    NOT NULL
            )
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS collection_variables (
                id            INTEGER PRIMARY KEY AUTOINCREMENT,
                collection_id INTEGER NOT NULL,
                key           TEXT    NOT NULL,
                value         TEXT    NOT NULL DEFAULT '',
                description   TEXT    NOT NULL DEFAULT ''
            )
        )");

        if (tableExists("folders")) {
            // migrate: create default collection and add collection_id column
            exec("INSERT INTO collections VALUES (null, 'Default Collection')");
            int64_t defaultId = sqlite3_last_insert_rowid(m_db);

            // alter existing tables to add collection_id
            {
                sqlite3_stmt* s{};
                std::string sql =
                    "ALTER TABLE folders ADD COLUMN collection_id INTEGER NOT NULL DEFAULT " +
                    std::to_string(defaultId);
                exec(sql);
            }
            {
                std::string sql = "ALTER TABLE saved_requests ADD COLUMN collection_id INTEGER NOT "
                                  "NULL DEFAULT " +
                                  std::to_string(defaultId);
                exec(sql);
            }
        } else {
            // fresh install: create tables with new schema
            exec(R"(
                CREATE TABLE IF NOT EXISTS folders (
                    id            INTEGER PRIMARY KEY AUTOINCREMENT,
                    name          TEXT    NOT NULL,
                    collection_id INTEGER NOT NULL,
                    parent_id     INTEGER NOT NULL DEFAULT 0
                )
            )");
            exec(R"(
                CREATE TABLE IF NOT EXISTS saved_requests (
                    id            INTEGER PRIMARY KEY AUTOINCREMENT,
                    name          TEXT    NOT NULL,
                    collection_id INTEGER NOT NULL,
                    folder_id     INTEGER NOT NULL DEFAULT 0,
                    method        TEXT    NOT NULL DEFAULT 'GET',
                    url           TEXT    NOT NULL DEFAULT '',
                    headers       TEXT    NOT NULL DEFAULT '',
                    body          TEXT    NOT NULL DEFAULT ''
                )
            )");
        }
    }
}

void DBStore::beginTransaction() {
    exec("BEGIN");
}

void DBStore::commit() {
    exec("COMMIT");
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

// ── collections
// ──────────────────────────────────────────────────────────────

int64_t DBStore::createCollection(const std::string& name) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "INSERT INTO collections (name) VALUES (?)", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

bool DBStore::renameCollection(int64_t id, const std::string& name) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "UPDATE collections SET name=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

bool DBStore::deleteCollection(int64_t id) {
    // cascade: delete all folders in this collection (using direct query for children)
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "SELECT id FROM folders WHERE collection_id=?", -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        std::vector<int64_t> folderIds;
        while (sqlite3_step(s) == SQLITE_ROW)
            folderIds.push_back(sqlite3_column_int64(s, 0));
        sqlite3_finalize(s);
        for (int64_t fid : folderIds)
            deleteFolder(fid);
    }
    // delete direct requests (folder_id=0) in this collection
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "DELETE FROM saved_requests WHERE collection_id=?", -1, &s,
                           nullptr);
        sqlite3_bind_int64(s, 1, id);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
    // delete variables
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "DELETE FROM collection_variables WHERE collection_id=?", -1, &s,
                           nullptr);
        sqlite3_bind_int64(s, 1, id);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
    // delete collection row
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "DELETE FROM collections WHERE id=?", -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
    return true;
}

std::vector<CollectionInfo> DBStore::getCollections() const {
    std::vector<CollectionInfo> result;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "SELECT id,name FROM collections ORDER BY name", -1, &s, nullptr);
    while (sqlite3_step(s) == SQLITE_ROW) {
        result.push_back(
            {sqlite3_column_int64(s, 0), reinterpret_cast<const char*>(sqlite3_column_text(s, 1))});
    }
    sqlite3_finalize(s);
    return result;
}

std::vector<CollectionVariable> DBStore::getCollectionVariables(int64_t collectionId) const {
    std::vector<CollectionVariable> result;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(
        m_db,
        "SELECT key,value,description FROM collection_variables WHERE collection_id=? ORDER BY id",
        -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, collectionId);
    while (sqlite3_step(s) == SQLITE_ROW) {
        CollectionVariable v;
        v.key = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        v.value = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        v.description = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
        result.push_back(std::move(v));
    }
    sqlite3_finalize(s);
    return result;
}

void DBStore::setCollectionVariables(int64_t collectionId,
                                     const std::vector<CollectionVariable>& vars) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "DELETE FROM collection_variables WHERE collection_id=?", -1, &s,
                       nullptr);
    sqlite3_bind_int64(s, 1, collectionId);
    sqlite3_step(s);
    sqlite3_finalize(s);

    for (auto& v : vars) {
        sqlite3_prepare_v2(m_db,
                           "INSERT INTO collection_variables (collection_id,key,value,description) "
                           "VALUES (?,?,?,?)",
                           -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, collectionId);
        sqlite3_bind_text(s, 2, v.key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, v.value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 4, v.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
}

// ── folders
// ───────────────────────────────────────────────────────────────────

int64_t DBStore::createFolder(const std::string& name, int64_t collectionId, int64_t parentId) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db, "INSERT INTO folders (name, collection_id, parent_id) VALUES (?,?,?)",
                       -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, collectionId);
    sqlite3_bind_int64(s, 3, parentId);
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
    // cascade children using direct SQL (avoids needing collectionId)
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "SELECT id FROM folders WHERE parent_id=?", -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        std::vector<int64_t> children;
        while (sqlite3_step(s) == SQLITE_ROW)
            children.push_back(sqlite3_column_int64(s, 0));
        sqlite3_finalize(s);
        for (int64_t child : children)
            deleteFolder(child);
    }
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "DELETE FROM saved_requests WHERE folder_id=?", -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
    {
        sqlite3_stmt* s{};
        sqlite3_prepare_v2(m_db, "DELETE FROM folders WHERE id=?", -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
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

int64_t DBStore::saveRequest(const std::string& name, int64_t collectionId, int64_t folderId,
                             const HttpRequest& req) {
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(
        m_db,
        "INSERT INTO saved_requests (name,collection_id,folder_id,method,url,headers,body)"
        " VALUES (?,?,?,?,?,?,?)",
        -1, &s, nullptr);
    auto hdrs = serializeHeaders(req.headers);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, collectionId);
    sqlite3_bind_int64(s, 3, folderId);
    sqlite3_bind_text(s, 4, req.method.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 5, req.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 6, hdrs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 7, req.body.c_str(), -1, SQLITE_TRANSIENT);
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

std::vector<FolderRecord> DBStore::getFolders(int64_t collectionId, int64_t parentId) const {
    std::vector<FolderRecord> result;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db,
                       "SELECT id,name,collection_id,parent_id FROM folders"
                       " WHERE collection_id=? AND parent_id=? ORDER BY name",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, collectionId);
    sqlite3_bind_int64(s, 2, parentId);
    while (sqlite3_step(s) == SQLITE_ROW) {
        result.push_back({sqlite3_column_int64(s, 0),
                          reinterpret_cast<const char*>(sqlite3_column_text(s, 1)),
                          sqlite3_column_int64(s, 2), sqlite3_column_int64(s, 3)});
    }
    sqlite3_finalize(s);
    return result;
}

std::vector<SavedRequest> DBStore::getRequests(int64_t collectionId, int64_t folderId) const {
    std::vector<SavedRequest> result;
    sqlite3_stmt* s{};
    sqlite3_prepare_v2(m_db,
                       "SELECT id,name,collection_id,folder_id,method,url,headers,body"
                       " FROM saved_requests WHERE collection_id=? AND folder_id=? ORDER BY name",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, collectionId);
    sqlite3_bind_int64(s, 2, folderId);
    while (sqlite3_step(s) == SQLITE_ROW) {
        SavedRequest r;
        r.id = sqlite3_column_int64(s, 0);
        r.name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        r.collectionId = sqlite3_column_int64(s, 2);
        r.folderId = sqlite3_column_int64(s, 3);
        r.request.method = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
        r.request.url = reinterpret_cast<const char*>(sqlite3_column_text(s, 5));
        auto h = sqlite3_column_text(s, 6);
        r.request.headers = parseHeaders(h ? reinterpret_cast<const char*>(h) : "");
        auto b = sqlite3_column_text(s, 7);
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
                       "SELECT id,name,collection_id,folder_id,method,url,headers,body"
                       " FROM saved_requests WHERE id=?",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    if (sqlite3_step(s) == SQLITE_ROW) {
        r.id = sqlite3_column_int64(s, 0);
        r.name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        r.collectionId = sqlite3_column_int64(s, 2);
        r.folderId = sqlite3_column_int64(s, 3);
        r.request.method = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
        r.request.url = reinterpret_cast<const char*>(sqlite3_column_text(s, 5));
        auto h = sqlite3_column_text(s, 6);
        r.request.headers = parseHeaders(h ? reinterpret_cast<const char*>(h) : "");
        auto b = sqlite3_column_text(s, 7);
        r.request.body = b ? reinterpret_cast<const char*>(b) : "";
    }
    sqlite3_finalize(s);
    return r;
}
