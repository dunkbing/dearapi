#include "curl_parser.hpp"

#include <gtest/gtest.h>

// ── helpers ───────────────────────────────────────────────────────────────────

static std::string header(const HttpRequest& req, const std::string& key) {
    auto it = req.headers.find(key);
    return it != req.headers.end() ? it->second : "";
}

static bool hasHeader(const HttpRequest& req, const std::string& key) {
    return req.headers.count(key) > 0;
}

// ── basic GET ─────────────────────────────────────────────────────────────────

TEST(CurlParserTest, SimpleGet) {
    auto req = ParseCurl("curl https://example.com");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com");
    EXPECT_TRUE(req.body.empty());
}

TEST(CurlParserTest, GetWithTrailingSlash) {
    auto req = ParseCurl("curl https://example.com/api/v1/users/");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com/api/v1/users/");
}

TEST(CurlParserTest, GetWithQueryParams) {
    auto req = ParseCurl("curl 'https://example.com/search?q=hello&page=2'");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com/search?q=hello&page=2");
}

TEST(CurlParserTest, GetWithExplicitMethod) {
    auto req = ParseCurl("curl -X GET https://example.com");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com");
}

TEST(CurlParserTest, GetWithLongRequestFlag) {
    auto req = ParseCurl("curl --request GET --url https://example.com");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com");
}

TEST(CurlParserTest, GetUrlBeforeFlags) {
    auto req = ParseCurl("curl https://example.com -H 'Accept: application/json'");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com");
    EXPECT_EQ(header(req, "Accept"), "application/json");
}

// ── POST ──────────────────────────────────────────────────────────────────────

TEST(CurlParserTest, PostWithJsonBody) {
    auto req = ParseCurl(R"(curl -X POST https://api.example.com/users -H "Content-Type: application/json" -d '{"name":"Alice","age":30}')");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.url, "https://api.example.com/users");
    EXPECT_EQ(header(req, "Content-Type"), "application/json");
    EXPECT_EQ(req.body, R"({"name":"Alice","age":30})");
}

TEST(CurlParserTest, PostBodyImpliesPost) {
    auto req = ParseCurl(R"(curl https://example.com/submit -d "key=value")");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.body, "key=value");
}

TEST(CurlParserTest, PostWithDataRaw) {
    auto req = ParseCurl(R"(curl -X POST https://example.com --data-raw '{"x":1}')");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.body, R"({"x":1})");
}

TEST(CurlParserTest, PostWithDataBinary) {
    auto req = ParseCurl(R"(curl -X POST https://example.com --data-binary '{"x":1}')");
    EXPECT_EQ(req.body, R"({"x":1})");
}

TEST(CurlParserTest, PostMultipleDataArgs) {
    auto req = ParseCurl("curl -X POST https://example.com -d 'a=1' -d 'b=2' -d 'c=3'");
    EXPECT_EQ(req.body, "a=1&b=2&c=3");
}

// ── other methods ─────────────────────────────────────────────────────────────

TEST(CurlParserTest, PutRequest) {
    auto req = ParseCurl(R"(curl -X PUT https://example.com/items/1 -H "Content-Type: application/json" -d '{"name":"Bob"}')");
    EXPECT_EQ(req.method, "PUT");
    EXPECT_EQ(req.url, "https://example.com/items/1");
    EXPECT_EQ(req.body, R"({"name":"Bob"})");
}

TEST(CurlParserTest, PatchRequest) {
    auto req = ParseCurl(R"(curl -X PATCH https://example.com/items/1 -d '{"status":"active"}')");
    EXPECT_EQ(req.method, "PATCH");
}

TEST(CurlParserTest, DeleteRequest) {
    auto req = ParseCurl("curl -X DELETE https://example.com/items/42");
    EXPECT_EQ(req.method, "DELETE");
    EXPECT_EQ(req.url, "https://example.com/items/42");
    EXPECT_TRUE(req.body.empty());
}

TEST(CurlParserTest, HeadRequestShortFlag) {
    auto req = ParseCurl("curl -I https://example.com");
    EXPECT_EQ(req.method, "HEAD");
    EXPECT_EQ(req.url, "https://example.com");
}

TEST(CurlParserTest, HeadRequestLongFlag) {
    auto req = ParseCurl("curl --head https://example.com");
    EXPECT_EQ(req.method, "HEAD");
}

// ── headers ───────────────────────────────────────────────────────────────────

TEST(CurlParserTest, SingleHeader) {
    auto req = ParseCurl(R"(curl -H "Authorization: Bearer abc123" https://example.com)");
    EXPECT_EQ(header(req, "Authorization"), "Bearer abc123");
}

TEST(CurlParserTest, MultipleHeaders) {
    auto req = ParseCurl(R"(curl https://api.example.com -H "Accept: application/json" -H "X-API-Key: secret" -H "X-Request-ID: 123")");
    EXPECT_EQ(header(req, "Accept"), "application/json");
    EXPECT_EQ(header(req, "X-API-Key"), "secret");
    EXPECT_EQ(header(req, "X-Request-ID"), "123");
}

TEST(CurlParserTest, LongHeaderFlag) {
    auto req = ParseCurl(R"(curl --header "Content-Type: text/plain" https://example.com)");
    EXPECT_EQ(header(req, "Content-Type"), "text/plain");
}

TEST(CurlParserTest, HeaderWithColonInValue) {
    auto req = ParseCurl(R"(curl -H "Authorization: Basic dXNlcjpwYXNz" https://example.com)");
    EXPECT_EQ(header(req, "Authorization"), "Basic dXNlcjpwYXNz");
}

TEST(CurlParserTest, HeaderValueNotTrimmedBeyondFirstSpace) {
    // value after colon may contain spaces and colons
    auto req = ParseCurl(R"(curl -H "X-Custom: foo: bar baz" https://example.com)");
    EXPECT_EQ(header(req, "X-Custom"), "foo: bar baz");
}

// ── basic auth ────────────────────────────────────────────────────────────────

TEST(CurlParserTest, BasicAuthShortFlag) {
    auto req = ParseCurl("curl -u user:pass https://example.com");
    EXPECT_EQ(header(req, "Authorization"), "Basic dXNlcjpwYXNz");
}

TEST(CurlParserTest, BasicAuthLongFlag) {
    auto req = ParseCurl("curl --user admin:secret https://example.com");
    EXPECT_EQ(header(req, "Authorization"), "Basic YWRtaW46c2VjcmV0");
}

TEST(CurlParserTest, BasicAuthUsernameOnly) {
    auto req = ParseCurl("curl -u alice https://example.com");
    // no colon — still encodes as-is
    EXPECT_TRUE(hasHeader(req, "Authorization"));
    EXPECT_EQ(header(req, "Authorization").substr(0, 6), "Basic ");
}

// ── convenience flags ─────────────────────────────────────────────────────────

TEST(CurlParserTest, UserAgentShortFlag) {
    auto req = ParseCurl(R"(curl -A "MyClient/1.0" https://example.com)");
    EXPECT_EQ(header(req, "User-Agent"), "MyClient/1.0");
}

TEST(CurlParserTest, UserAgentLongFlag) {
    auto req = ParseCurl(R"(curl --user-agent "curl/7.88" https://example.com)");
    EXPECT_EQ(header(req, "User-Agent"), "curl/7.88");
}

TEST(CurlParserTest, CookieFlag) {
    auto req = ParseCurl(R"(curl -b "session=abc123; token=xyz" https://example.com)");
    EXPECT_EQ(header(req, "Cookie"), "session=abc123; token=xyz");
}

TEST(CurlParserTest, RefererFlag) {
    auto req = ParseCurl(R"(curl -e "https://referrer.example.com" https://example.com)");
    EXPECT_EQ(header(req, "Referer"), "https://referrer.example.com");
}

TEST(CurlParserTest, RefererLongFlag) {
    auto req = ParseCurl(R"(curl --referer "https://referrer.example.com" https://example.com)");
    EXPECT_EQ(header(req, "Referer"), "https://referrer.example.com");
}

// ── --json flag ───────────────────────────────────────────────────────────────

TEST(CurlParserTest, JsonFlag) {
    auto req = ParseCurl(R"(curl --json '{"key":"value"}' https://api.example.com)");
    EXPECT_EQ(req.body, R"({"key":"value"})");
    EXPECT_EQ(header(req, "Content-Type"), "application/json");
    EXPECT_EQ(header(req, "Accept"), "application/json");
    EXPECT_EQ(req.method, "POST");
}

TEST(CurlParserTest, JsonFlagDoesNotOverrideExistingContentType) {
    auto req = ParseCurl(R"(curl --json '{"k":"v"}' -H "Content-Type: application/vnd.api+json" https://example.com)");
    EXPECT_EQ(header(req, "Content-Type"), "application/vnd.api+json");
}

// ── -G / --get with body ──────────────────────────────────────────────────────

TEST(CurlParserTest, GetFlagAppendsDAsQueryString) {
    auto req = ParseCurl("curl -G https://example.com/search -d 'q=hello world' -d 'page=1'");
    EXPECT_EQ(req.method, "GET");
    EXPECT_TRUE(req.body.empty());
    EXPECT_NE(req.url.find("q=hello world"), std::string::npos);
    EXPECT_NE(req.url.find("page=1"), std::string::npos);
}

TEST(CurlParserTest, GetFlagAppendToExistingQueryString) {
    auto req = ParseCurl("curl -G 'https://example.com/search?existing=1' -d 'q=test'");
    EXPECT_NE(req.url.find("existing=1"), std::string::npos);
    EXPECT_NE(req.url.find("q=test"), std::string::npos);
}

// ── quoting ───────────────────────────────────────────────────────────────────

TEST(CurlParserTest, SingleQuotedBody) {
    auto req = ParseCurl("curl -X POST https://example.com -d '{\"key\":\"value\"}'");
    EXPECT_EQ(req.body, R"({"key":"value"})");
}

TEST(CurlParserTest, DoubleQuotedBody) {
    auto req = ParseCurl(R"(curl -X POST https://example.com -d "{\"key\":\"value\"}")");
    EXPECT_EQ(req.body, R"({"key":"value"})");
}

TEST(CurlParserTest, AnsiCQuotedBody) {
    auto req = ParseCurl("curl -X POST https://example.com -d $'{\"key\":\"val\\nue\"}'");
    EXPECT_EQ(req.body, "{\"key\":\"val\nue\"}");
}

TEST(CurlParserTest, UnquotedUrl) {
    auto req = ParseCurl("curl http://localhost:8080/api");
    EXPECT_EQ(req.url, "http://localhost:8080/api");
}

TEST(CurlParserTest, MixedQuoting) {
    auto req = ParseCurl("curl -H 'X-Foo: bar' -H \"X-Baz: qux\" https://example.com");
    EXPECT_EQ(header(req, "X-Foo"), "bar");
    EXPECT_EQ(header(req, "X-Baz"), "qux");
}

// ── line continuation ─────────────────────────────────────────────────────────

TEST(CurlParserTest, LineContinuationUnix) {
    auto req = ParseCurl(
        "curl -X POST \\\n"
        "  https://example.com \\\n"
        "  -H 'Content-Type: application/json' \\\n"
        "  -d '{\"a\":1}'");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.url, "https://example.com");
    EXPECT_EQ(header(req, "Content-Type"), "application/json");
    EXPECT_EQ(req.body, R"({"a":1})");
}

TEST(CurlParserTest, LineContinuationWindows) {
    auto req = ParseCurl("curl -X GET \\\r\nhttps://example.com");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://example.com");
}

// ── ignored flags ─────────────────────────────────────────────────────────────

TEST(CurlParserTest, IgnoredVerboseAndSilent) {
    auto req = ParseCurl("curl -v -s https://example.com");
    EXPECT_EQ(req.url, "https://example.com");
    EXPECT_EQ(req.method, "GET");
}

TEST(CurlParserTest, IgnoredInsecureFlag) {
    auto req = ParseCurl("curl -k https://self-signed.example.com");
    EXPECT_EQ(req.url, "https://self-signed.example.com");
}

TEST(CurlParserTest, IgnoredCompressedAndLocation) {
    auto req = ParseCurl("curl -L --compressed https://example.com");
    EXPECT_EQ(req.url, "https://example.com");
}

TEST(CurlParserTest, IgnoredOutputFlag) {
    auto req = ParseCurl("curl -o /dev/null https://example.com");
    EXPECT_EQ(req.url, "https://example.com");
}

// ── --url flag ────────────────────────────────────────────────────────────────

TEST(CurlParserTest, ExplicitUrlFlag) {
    auto req = ParseCurl("curl --url https://example.com/path");
    EXPECT_EQ(req.url, "https://example.com/path");
}

TEST(CurlParserTest, ExplicitUrlFlagWithOtherFlags) {
    auto req = ParseCurl(R"(curl --url https://example.com -H "Accept: */*")");
    EXPECT_EQ(req.url, "https://example.com");
    EXPECT_EQ(header(req, "Accept"), "*/*");
}

// ── prompt prefix ─────────────────────────────────────────────────────────────

TEST(CurlParserTest, DollarPromptPrefix) {
    auto req = ParseCurl("$ curl https://example.com");
    EXPECT_EQ(req.url, "https://example.com");
    EXPECT_EQ(req.method, "GET");
}

// ── real-world examples ───────────────────────────────────────────────────────

TEST(CurlParserTest, RealWorldGithubApi) {
    auto req = ParseCurl(
        R"(curl -L -s -H "Accept: application/vnd.github+json" -H "Authorization: Bearer ghp_token123" -H "X-GitHub-Api-Version: 2022-11-28" https://api.github.com/repos/owner/repo)");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.url, "https://api.github.com/repos/owner/repo");
    EXPECT_EQ(header(req, "Accept"), "application/vnd.github+json");
    EXPECT_EQ(header(req, "Authorization"), "Bearer ghp_token123");
    EXPECT_EQ(header(req, "X-GitHub-Api-Version"), "2022-11-28");
}

TEST(CurlParserTest, RealWorldPostWithAuth) {
    auto req = ParseCurl(
        "curl -X POST https://api.example.com/v2/messages \\\n"
        "  -u 'api:keyvalue123' \\\n"
        "  -H 'Content-Type: application/json' \\\n"
        "  -d '{\"to\":\"+1555\",\"body\":\"Hello\"}'");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.url, "https://api.example.com/v2/messages");
    EXPECT_TRUE(hasHeader(req, "Authorization"));
    EXPECT_EQ(header(req, "Content-Type"), "application/json");
    EXPECT_EQ(req.body, R"({"to":"+1555","body":"Hello"})");
}

TEST(CurlParserTest, RealWorldStripeApi) {
    auto req = ParseCurl(
        R"(curl https://api.stripe.com/v1/charges -u sk_test_abc123: -d amount=2000 -d currency=usd -d "source=tok_visa")");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.url, "https://api.stripe.com/v1/charges");
    EXPECT_NE(req.body.find("amount=2000"), std::string::npos);
    EXPECT_NE(req.body.find("currency=usd"), std::string::npos);
    EXPECT_NE(req.body.find("source=tok_visa"), std::string::npos);
}

// ── invalid / edge cases ──────────────────────────────────────────────────────

TEST(CurlParserTest, EmptyInput) {
    auto req = ParseCurl("");
    EXPECT_TRUE(req.url.empty());
}

TEST(CurlParserTest, NotACurlCommand) {
    auto req = ParseCurl("wget https://example.com");
    EXPECT_TRUE(req.url.empty());
}

TEST(CurlParserTest, CurlWithNoUrl) {
    auto req = ParseCurl("curl -H 'Accept: json'");
    EXPECT_TRUE(req.url.empty());
}

TEST(CurlParserTest, CurlCommandCaseInsensitive) {
    auto req = ParseCurl("CURL https://example.com");
    EXPECT_EQ(req.url, "https://example.com");
}

TEST(CurlParserTest, LeadingWhitespace) {
    auto req = ParseCurl("   curl https://example.com");
    EXPECT_EQ(req.url, "https://example.com");
}
