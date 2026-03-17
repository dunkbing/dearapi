#include "importer.hpp"
#include <algorithm>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <wx/filename.h>

namespace {

    std::string urlEncode(const std::string& s) {
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += static_cast<char>(c);
            else
                out += std::format("%{:02X}", c);
        }
        return out;
    }

    std::string stemName(const std::string& path) {
        return wxFileName(path).GetName().ToStdString();
    }

    Importer::Result importSwagger(DBStore& db, const std::string& path,
                                   const nlohmann::json& doc) {
        bool isV2 = doc.contains("swagger");
        bool isV3 = doc.contains("openapi");
        if ((!isV2 && !isV3) || !doc.contains("paths"))
            return {0, {}, "Unrecognized format. Expected Swagger 2.0 or OpenAPI 3.x with paths."};

        // base URL
        std::string baseUrl;
        if (isV2) {
            std::string host = doc.value("host", "");
            std::string basePath = doc.value("basePath", "");
            std::string scheme = "https";
            if (doc.contains("schemes") && !doc["schemes"].empty())
                scheme = doc["schemes"][0].get<std::string>();
            baseUrl = host.empty() ? basePath : (scheme + "://" + host + basePath);
        } else {
            if (doc.contains("servers") && !doc["servers"].empty())
                baseUrl = doc["servers"][0].value("url", "");
        }

        std::string collName;
        if (doc.contains("info") && doc["info"].is_object())
            collName = doc["info"].value("title", "");
        if (collName.empty())
            collName = stemName(path);

        int64_t rootCollId = db.createCollection(collName);

        if (!baseUrl.empty())
            db.setCollectionVariables(rootCollId, {{"basePath", baseUrl, "base url"}});

        std::map<std::string, int64_t> tagFolders;
        auto tagFolder = [&](const std::string& tag) -> int64_t {
            auto [it, inserted] = tagFolders.emplace(tag, 0);
            if (inserted)
                it->second = db.createFolder(tag, rootCollId, 0);
            return it->second;
        };

        static const std::string kMethods[] = {"get",    "post", "put",    "patch",
                                               "delete", "head", "options"};
        int count = 0;
        db.beginTransaction();

        for (auto& [pathStr, pathItem] : doc["paths"].items()) {
            std::vector<nlohmann::json> pathParams;
            if (pathItem.contains("parameters") && pathItem["parameters"].is_array())
                pathParams = pathItem["parameters"].get<std::vector<nlohmann::json>>();

            for (auto& m : kMethods) {
                if (!pathItem.contains(m))
                    continue;
                auto& op = pathItem[m];

                std::vector<nlohmann::json> params = pathParams;
                if (op.contains("parameters") && op["parameters"].is_array())
                    for (auto& p : op["parameters"])
                        params.push_back(p);

                HttpRequest req;
                req.method = m;
                std::transform(req.method.begin(), req.method.end(), req.method.begin(), ::toupper);
                req.url = (baseUrl.empty() ? "" : "{{basePath}}") + pathStr;

                std::string qs;
                for (auto& p : params) {
                    std::string in = p.value("in", "");
                    std::string pname = p.value("name", "");
                    if (pname.empty())
                        continue;
                    if (in == "query")
                        qs += (qs.empty() ? "?" : "&") + pname + "=";
                    else if (in == "header")
                        req.headers[pname] = "";
                }
                req.url += qs;

                if (isV2) {
                    bool hasBody = false, hasForm = false;
                    for (auto& p : params) {
                        std::string in = p.value("in", "");
                        if (in == "body")
                            hasBody = true;
                        else if (in == "formData")
                            hasForm = true;
                    }
                    if (hasForm)
                        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
                    else if (hasBody)
                        req.headers["Content-Type"] = "application/json";
                } else if (op.contains("requestBody")) {
                    auto& rb = op["requestBody"];
                    if (rb.contains("content") && rb["content"].is_object()) {
                        auto& ct = rb["content"];
                        if (ct.contains("application/json"))
                            req.headers["Content-Type"] = "application/json";
                        else if (ct.contains("multipart/form-data"))
                            req.headers["Content-Type"] = "multipart/form-data";
                        else if (ct.contains("application/x-www-form-urlencoded"))
                            req.headers["Content-Type"] = "application/x-www-form-urlencoded";
                    }
                }

                std::string name;
                if (op.contains("operationId") && op["operationId"].is_string())
                    name = op["operationId"].get<std::string>();
                else if (op.contains("summary") && op["summary"].is_string())
                    name = op["summary"].get<std::string>();
                else
                    name = req.method + " " + pathStr;

                int64_t folderId = 0;
                if (op.contains("tags") && op["tags"].is_array() && !op["tags"].empty())
                    folderId = tagFolder(op["tags"][0].get<std::string>());

                db.saveRequest(name, rootCollId, folderId, req);
                ++count;
            }
        }
        db.commit();
        return {count, collName, {}};
    }

    Importer::Result importPostman(DBStore& db, const std::string& path,
                                   const nlohmann::json& doc) {
        std::string collName;
        if (doc.contains("info") && doc["info"].is_object())
            collName = doc["info"].value("name", "");
        if (collName.empty())
            collName = stemName(path);

        int64_t rootCollId = db.createCollection(collName);

        if (doc.contains("variable") && doc["variable"].is_array()) {
            std::vector<CollectionVariable> vars;
            for (auto& v : doc["variable"]) {
                std::string key = v.value("key", "");
                if (!key.empty())
                    vars.push_back({key, v.value("value", ""), v.value("description", "")});
            }
            if (!vars.empty())
                db.setCollectionVariables(rootCollId, vars);
        }

        int count = 0;
        db.beginTransaction();

        std::function<void(const nlohmann::json&, int64_t)> processItems;
        processItems = [&](const nlohmann::json& items, int64_t parentFolderId) {
            if (!items.is_array())
                return;
            for (auto& item : items) {
                if (!item.contains("name"))
                    continue;
                std::string name = item["name"].get<std::string>();

                if (item.contains("item")) {
                    int64_t folderId = db.createFolder(name, rootCollId, parentFolderId);
                    processItems(item["item"], folderId);
                } else if (item.contains("request")) {
                    auto& reqObj = item["request"];
                    HttpRequest req;

                    req.method = reqObj.value("method", "GET");
                    std::transform(req.method.begin(), req.method.end(), req.method.begin(),
                                   ::toupper);

                    if (reqObj.contains("url")) {
                        auto& u = reqObj["url"];
                        req.url = u.is_string() ? u.get<std::string>() : u.value("raw", "");
                    }

                    if (reqObj.contains("header") && reqObj["header"].is_array()) {
                        for (auto& h : reqObj["header"]) {
                            std::string key = h.value("key", "");
                            if (!key.empty() && !h.value("disabled", false))
                                req.headers[key] = h.value("value", "");
                        }
                    }

                    if (reqObj.contains("body") && reqObj["body"].is_object()) {
                        auto& body = reqObj["body"];
                        std::string mode = body.value("mode", "");
                        if (mode == "raw") {
                            req.body = body.value("raw", "");
                            if (!req.headers.count("Content-Type") && body.contains("options")) {
                                auto& opts = body["options"];
                                std::string lang;
                                if (opts.contains("raw") && opts["raw"].is_object())
                                    lang = opts["raw"].value("language", "");
                                if (lang == "json")
                                    req.headers["Content-Type"] = "application/json";
                                else if (lang == "xml")
                                    req.headers["Content-Type"] = "application/xml";
                            }
                        } else if (mode == "urlencoded" && body.contains("urlencoded") &&
                                   body["urlencoded"].is_array()) {
                            std::string encoded;
                            for (auto& p : body["urlencoded"]) {
                                if (p.value("disabled", false))
                                    continue;
                                if (!encoded.empty())
                                    encoded += "&";
                                encoded += urlEncode(p.value("key", "")) + "=" +
                                           urlEncode(p.value("value", ""));
                            }
                            req.body = encoded;
                            if (!req.headers.count("Content-Type"))
                                req.headers["Content-Type"] = "application/x-www-form-urlencoded";
                        } else if (mode == "graphql" && body.contains("graphql")) {
                            nlohmann::json gql;
                            gql["query"] = body["graphql"].value("query", "");
                            if (body["graphql"].contains("variables"))
                                gql["variables"] = body["graphql"]["variables"];
                            req.body = gql.dump(2);
                            if (!req.headers.count("Content-Type"))
                                req.headers["Content-Type"] = "application/json";
                        }
                    }

                    db.saveRequest(name, rootCollId, parentFolderId, req);
                    ++count;
                }
            }
        };

        if (doc.contains("item"))
            processItems(doc["item"], 0);
        db.commit();
        return {count, collName, {}};
    }

} // namespace

namespace Importer {

    Result importFile(DBStore& db, const std::string& path) {
        nlohmann::json doc;
        try {
            std::ifstream f(path);
            f >> doc;
        } catch (...) {
            return {0, {}, "Failed to parse JSON."};
        }

        bool isPostman =
            doc.contains("item") && doc.contains("info") &&
            doc["info"].value("schema", "").find("getpostman.com") != std::string::npos;
        return isPostman ? importPostman(db, path, doc) : importSwagger(db, path, doc);
    }

} // namespace Importer
