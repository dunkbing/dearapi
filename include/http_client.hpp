#pragma once
#include <map>
#include <string>

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int statusCode = 0;
    std::string statusMessage;
    std::map<std::string, std::string> headers;
    std::string body;
    double elapsedMs = 0.0;
    std::string error;

    bool success() const {
        return error.empty();
    }
};

HttpResponse sendRequest(const HttpRequest& req);
