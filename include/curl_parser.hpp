#pragma once
#include "http_client.hpp"
#include <string>

// Parses a curl command string into an HttpRequest.
// Returns an HttpRequest with empty url if the input is not a valid curl command.
HttpRequest ParseCurl(const std::string& cmd);
