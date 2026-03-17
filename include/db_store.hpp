#pragma once
#include "http_client.hpp"
#include <map>
#include <sqlite3.h>
#include <string>
#include <vector>

struct CollectionInfo {
    int64_t id;
    std::string name;
};

struct CollectionVariable {
    std::string key, value, description;
};

struct FolderInfo {
    int64_t id;
    std::string name;
    int64_t collectionId;
    int64_t parentId;
};

struct SavedRequest {
    int64_t id;
    std::string name;
    int64_t collectionId;
    int64_t folderId;
    HttpRequest request;
};

class DBStore {
public:
    ~DBStore();
    bool open(const std::string& path);

    // collections
    int64_t createCollection(const std::string& name);
    bool renameCollection(int64_t id, const std::string& name);
    bool deleteCollection(int64_t id); // cascades folders, requests, variables
    std::vector<CollectionInfo> getCollections() const;
    std::vector<CollectionVariable> getCollectionVariables(int64_t collectionId) const;
    void setCollectionVariables(int64_t collectionId, const std::vector<CollectionVariable>& vars);

    // folders
    int64_t createFolder(const std::string& name, int64_t collectionId, int64_t parentId = 0);
    bool renameFolder(int64_t id, const std::string& name);
    bool deleteFolder(int64_t id); // cascades to children
    bool moveFolder(int64_t id, int64_t newParentId);

    // requests
    int64_t saveRequest(const std::string& name, int64_t collectionId, int64_t folderId,
                        const HttpRequest& req);
    bool renameRequest(int64_t id, const std::string& name);
    bool updateRequest(int64_t id, const HttpRequest& req);
    bool deleteRequest(int64_t id);
    bool moveRequest(int64_t id, int64_t newFolderId);

    // queries
    std::vector<FolderInfo> getFolders(int64_t collectionId, int64_t parentId = 0) const;
    std::vector<SavedRequest> getRequests(int64_t collectionId, int64_t folderId = 0) const;
    SavedRequest getRequest(int64_t id) const;

private:
    sqlite3* m_db{};
    void initSchema();
    void exec(const std::string& sql) const;
    bool tableExists(const std::string& name) const;
    static std::string serializeHeaders(const std::map<std::string, std::string>& h);
    static std::map<std::string, std::string> parseHeaders(const std::string& s);
};
