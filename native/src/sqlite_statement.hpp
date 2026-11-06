#pragma once
#include <sqlite3.h>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace atlas::detail {
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK)
            throw std::runtime_error("database query failed; schema or JSON support unavailable");
    }
    ~Statement() { sqlite3_finalize(value_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(std::string_view value) {
        if (value.size() > 128 || sqlite3_bind_text(value_, 1, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::invalid_argument("invalid session ID");
    }
    void bindJson(std::string_view value) {
        if (value.size() > 32768 || sqlite3_bind_text(value_, 1, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::invalid_argument("JSON exceeds capacity");
    }
    void bindLimit(std::size_t value) {
        if (sqlite3_bind_int(value_, 1, static_cast<int>(value)) != SQLITE_OK) throw std::runtime_error("query bind failed");
    }
    bool next() {
        const int result = sqlite3_step(value_);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        throw std::runtime_error("database read failed: " + std::string(sqlite3_errstr(result)));
    }
    bool null(int column) const { return sqlite3_column_type(value_, column) == SQLITE_NULL; }
    std::string text(int column) const {
        if (sqlite3_column_type(value_, column) != SQLITE_TEXT)
            throw std::runtime_error("text field missing or invalid");
        const auto* value = sqlite3_column_text(value_, column);
        if (!value) throw std::runtime_error("required text field missing");
        const auto size = sqlite3_column_bytes(value_, column);
        if (size > 32768) throw std::runtime_error("text field exceeds native capacity");
        return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
    }
    std::optional<std::string> optionalText(int column) const {
        return null(column) ? std::nullopt : std::optional<std::string>{text(column)};
    }
    std::int64_t integer(int column) const {
        if (sqlite3_column_type(value_, column) != SQLITE_INTEGER) throw std::runtime_error("integer field missing or invalid");
        return sqlite3_column_int64(value_, column);
    }
    double number(int column) const { return sqlite3_column_double(value_, column); }
private:
    sqlite3_stmt* value_{};
};

} // namespace atlas::detail
