#include "curl_parser.hpp"

#include <algorithm>
#include <openssl/evp.h>
#include <string>
#include <vector>

// ── base64 ────────────────────────────────────────────────────────────────────

static std::string base64Encode(const std::string& in) {
    // output is at most ceil(n/3)*4 bytes + null terminator
    std::string out(((in.size() + 2) / 3) * 4 + 1, '\0');
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                              reinterpret_cast<const unsigned char*>(in.data()),
                              static_cast<int>(in.size()));
    out.resize(len);
    return out;
}

// ── tokenizer ─────────────────────────────────────────────────────────────────

// Splits a shell-like command line into tokens, handling:
//   - single-quoted strings  'value'  (no escape processing inside)
//   - double-quoted strings  "value"  (\" \\ \n \t inside)
//   - ANSI-C strings         $'value' (\n \t \\ etc.)
//   - backslash line continuation  \<newline>
//   - bare backslash escapes outside quotes
static std::vector<std::string> tokenize(const std::string& cmd) {
    std::vector<std::string> tokens;
    std::string cur;
    size_t i = 0;
    const size_t n = cmd.size();

    while (i < n) {
        char c = cmd[i];

        // whitespace between tokens
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            i++;
            continue;
        }

        // line continuation
        if (c == '\\' && i + 1 < n && (cmd[i + 1] == '\n' || cmd[i + 1] == '\r')) {
            i += 2;
            if (i < n && cmd[i] == '\n')
                i++; // CRLF
            continue;
        }

        // ANSI-C quoting  $'...'
        if (c == '$' && i + 1 < n && cmd[i + 1] == '\'') {
            i += 2;
            while (i < n && cmd[i] != '\'') {
                if (cmd[i] == '\\' && i + 1 < n) {
                    i++;
                    switch (cmd[i]) {
                    case 'n':
                        cur += '\n';
                        break;
                    case 't':
                        cur += '\t';
                        break;
                    case 'r':
                        cur += '\r';
                        break;
                    case '\\':
                        cur += '\\';
                        break;
                    case '\'':
                        cur += '\'';
                        break;
                    case '"':
                        cur += '"';
                        break;
                    default:
                        cur += '\\';
                        cur += cmd[i];
                        break;
                    }
                    i++;
                } else {
                    cur += cmd[i++];
                }
            }
            if (i < n)
                i++; // closing '
            continue;
        }

        // single-quoted string  '...'
        if (c == '\'') {
            i++;
            while (i < n && cmd[i] != '\'')
                cur += cmd[i++];
            if (i < n)
                i++;
            continue;
        }

        // double-quoted string  "..."
        if (c == '"') {
            i++;
            while (i < n && cmd[i] != '"') {
                if (cmd[i] == '\\' && i + 1 < n) {
                    i++;
                    switch (cmd[i]) {
                    case 'n':
                        cur += '\n';
                        break;
                    case 't':
                        cur += '\t';
                        break;
                    case '"':
                        cur += '"';
                        break;
                    case '\\':
                        cur += '\\';
                        break;
                    default:
                        cur += '\\';
                        cur += cmd[i];
                        break;
                    }
                    i++;
                } else {
                    cur += cmd[i++];
                }
            }
            if (i < n)
                i++;
            continue;
        }

        // bare backslash escape
        if (c == '\\' && i + 1 < n) {
            i++;
            cur += cmd[i++];
            continue;
        }

        cur += c;
        i++;
    }

    if (!cur.empty())
        tokens.push_back(cur);
    return tokens;
}

// ── parser ────────────────────────────────────────────────────────────────────

HttpRequest ParseCurl(const std::string& cmd) {
    // strip leading whitespace and optional "$ " prompt
    std::string trimmed = cmd;
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    if (trimmed[start] == '$')
        start = trimmed.find_first_not_of(" \t", start + 1);
    if (start == std::string::npos)
        return {};
    trimmed = trimmed.substr(start);

    auto tokens = tokenize(trimmed);
    if (tokens.empty())
        return {};

    // accept "curl" as first token (case-insensitive)
    {
        std::string first = tokens[0];
        std::transform(first.begin(), first.end(), first.begin(), ::tolower);
        if (first != "curl")
            return {};
    }

    HttpRequest req;
    std::string method;
    std::vector<std::string> bodyParts;
    bool forceGet = false;

    // helper: peek next token
    auto nextToken = [&](size_t& idx) -> std::string {
        if (idx + 1 < tokens.size())
            return tokens[++idx];
        return {};
    };

    for (size_t i = 1; i < tokens.size(); i++) {
        const std::string& tok = tokens[i];

        // ── URL ──────────────────────────────────────────────────────────────
        if (tok == "--url") {
            req.url = nextToken(i);
            continue;
        }

        // ── method ───────────────────────────────────────────────────────────
        if (tok == "-X" || tok == "--request") {
            method = nextToken(i);
            continue;
        }
        if (tok == "-I" || tok == "--head") {
            method = "HEAD";
            continue;
        }
        if (tok == "-G" || tok == "--get") {
            forceGet = true;
            continue;
        }

        // ── headers ──────────────────────────────────────────────────────────
        if (tok == "-H" || tok == "--header") {
            std::string hdr = nextToken(i);
            auto colon = hdr.find(':');
            if (colon != std::string::npos) {
                std::string key = hdr.substr(0, colon);
                std::string val = hdr.substr(colon + 1);
                size_t vs = val.find_first_not_of(' ');
                if (vs != std::string::npos)
                    val = val.substr(vs);
                req.headers[key] = val;
            }
            continue;
        }

        // ── body ─────────────────────────────────────────────────────────────
        if (tok == "-d" || tok == "--data" || tok == "--data-raw" || tok == "--data-binary" ||
            tok == "--data-ascii" || tok == "--data-urlencode") {
            bodyParts.push_back(nextToken(i));
            continue;
        }
        if (tok == "--json") {
            std::string data = nextToken(i);
            bodyParts.push_back(data);
            if (req.headers.find("Content-Type") == req.headers.end())
                req.headers["Content-Type"] = "application/json";
            if (req.headers.find("Accept") == req.headers.end())
                req.headers["Accept"] = "application/json";
            continue;
        }

        // ── auth / convenience headers ────────────────────────────────────────
        if (tok == "-u" || tok == "--user") {
            req.headers["Authorization"] = "Basic " + base64Encode(nextToken(i));
            continue;
        }
        if (tok == "-A" || tok == "--user-agent") {
            req.headers["User-Agent"] = nextToken(i);
            continue;
        }
        if (tok == "-b" || tok == "--cookie") {
            req.headers["Cookie"] = nextToken(i);
            continue;
        }
        if (tok == "-e" || tok == "--referer" || tok == "--referrer") {
            req.headers["Referer"] = nextToken(i);
            continue;
        }

        // ── flags with a value we ignore ─────────────────────────────────────
        if (tok == "-o" || tok == "--output" || tok == "-m" || tok == "--max-time" ||
            tok == "--connect-timeout" || tok == "--limit-rate" || tok == "--proxy" ||
            tok == "-x" || tok == "--cacert" || tok == "--cert" || tok == "--key" ||
            tok == "--resolve" || tok == "--dns-servers" || tok == "-w" || tok == "--write-out" ||
            tok == "--interface" || tok == "--local-port" || tok == "-T" ||
            tok == "--upload-file" || tok == "--range" || tok == "-r") {
            i++; // skip value
            continue;
        }

        // ── boolean flags we ignore ───────────────────────────────────────────
        if (tok == "-L" || tok == "--location" || tok == "-v" || tok == "--verbose" ||
            tok == "-s" || tok == "--silent" || tok == "-S" || tok == "--show-error" ||
            tok == "-k" || tok == "--insecure" || tok == "--compressed" || tok == "-i" ||
            tok == "--include" || tok == "-f" || tok == "--fail" || tok == "--no-keepalive" ||
            tok == "--http1.0" || tok == "--http1.1" || tok == "--http2" || tok == "--http3" ||
            tok == "-4" || tok == "--ipv4" || tok == "-6" || tok == "--ipv6" ||
            tok == "--tr-encoding" || tok == "--no-buffer" || tok == "-N") {
            continue;
        }

        // ── positional URL ────────────────────────────────────────────────────
        if (!tok.empty() && tok[0] != '-' && req.url.empty()) {
            req.url = tok;
            continue;
        }

        // unrecognized — skip (best-effort)
    }

    if (req.url.empty())
        return {};

    // join multiple -d parts
    std::string body;
    for (size_t j = 0; j < bodyParts.size(); j++) {
        if (j > 0)
            body += "&";
        body += bodyParts[j];
    }

    // -G / --get: append body as query string instead
    if (forceGet && !body.empty()) {
        req.url += (req.url.find('?') == std::string::npos ? "?" : "&") + body;
        body.clear();
    }

    req.body = body;

    // determine method
    if (!method.empty())
        req.method = method;
    else if (!req.body.empty())
        req.method = "POST";
    else
        req.method = "GET";

    return req;
}
