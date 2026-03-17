#pragma once
#include "db_store.hpp"
#include <string>

namespace Importer {

    struct Result {
        int count{0};
        std::string collName;
        std::string error;
        bool ok() const {
            return error.empty();
        }
    };

    // Opens path, parses JSON, detects format (Swagger 2/3 or Postman), runs import.
    // Returns Result with error set on failure.
    Result importFile(DBStore& db, const std::string& path);

} // namespace Importer
