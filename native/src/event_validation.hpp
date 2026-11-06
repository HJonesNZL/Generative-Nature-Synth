#pragma once
#include <string_view>
struct sqlite3;
namespace atlas::detail {
// Worker-only JSON boundary. Throws on unsupported or malformed frozen event v1.
void validateStoredEvent(sqlite3* db, std::string_view json);
}
