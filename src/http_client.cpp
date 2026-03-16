#include "http_client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <chrono>
#include <regex>
#include <stdexcept>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

struct ParsedUrl {
    std::string scheme;
    std::string host;
    int port;
    std::string target;
};

static ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;
    std::regex re(R"(^(https?)://([^/:?#]+)(?::(\d+))?((?:/[^?#]*)?(?:\?[^#]*)?)$)");
    std::smatch m;
    if (std::regex_match(url, m, re)) {
        result.scheme = m[1].str();
        result.host = m[2].str();
        result.port = m[3].matched ? std::stoi(m[3].str()) : (result.scheme == "https" ? 443 : 80);
        result.target = m[4].str().empty() ? "/" : m[4].str();
    } else {
        throw std::runtime_error("Invalid URL: " + url);
    }
    return result;
}

static http::verb toVerb(const std::string& method) {
    if (method == "GET")
        return http::verb::get;
    if (method == "POST")
        return http::verb::post;
    if (method == "PUT")
        return http::verb::put;
    if (method == "DELETE")
        return http::verb::delete_;
    if (method == "PATCH")
        return http::verb::patch;
    if (method == "HEAD")
        return http::verb::head;
    if (method == "OPTIONS")
        return http::verb::options;
    return http::verb::get;
}

HttpResponse sendRequest(const HttpRequest& req) {
    HttpResponse response;
    auto start = std::chrono::steady_clock::now();

    try {
        auto parsed = parseUrl(req.url);
        net::io_context ioc;

        http::request<http::string_body> beast_req{toVerb(req.method), parsed.target, 11};
        beast_req.set(http::field::host, parsed.host);
        beast_req.set(http::field::user_agent, "DearAPI/0.1");

        for (auto& [k, v] : req.headers)
            beast_req.set(k, v);

        if (!req.body.empty()) {
            beast_req.body() = req.body;
            beast_req.prepare_payload();
        }

        auto populate = [&](auto& beast_res) {
            response.statusCode = beast_res.result_int();
            response.statusMessage = std::string(beast_res.reason());
            response.body = beast_res.body();
            for (auto& field : beast_res)
                response.headers[std::string(field.name_string())] = std::string(field.value());
        };

        if (parsed.scheme == "https") {
            ssl::context ctx{ssl::context::tlsv12_client};
            ctx.set_default_verify_paths();
            beast::ssl_stream<beast::tcp_stream> stream{ioc, ctx};

            if (!SSL_set_tlsext_host_name(stream.native_handle(), parsed.host.c_str()))
                throw std::runtime_error("Failed to set SNI hostname");

            tcp::resolver resolver{ioc};
            auto results = resolver.resolve(parsed.host, std::to_string(parsed.port));
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);

            http::write(stream, beast_req);
            beast::flat_buffer buf;
            http::response<http::string_body> beast_res;
            http::read(stream, buf, beast_res);
            populate(beast_res);

            beast::error_code ec;
            stream.shutdown(ec);
        } else {
            beast::tcp_stream stream{ioc};
            tcp::resolver resolver{ioc};
            auto results = resolver.resolve(parsed.host, std::to_string(parsed.port));
            stream.connect(results);

            http::write(stream, beast_req);
            beast::flat_buffer buf;
            http::response<http::string_body> beast_res;
            http::read(stream, buf, beast_res);
            populate(beast_res);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        }
    } catch (std::exception& e) {
        response.error = e.what();
    }

    auto end = std::chrono::steady_clock::now();
    response.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    return response;
}
