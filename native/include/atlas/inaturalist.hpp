#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace atlas {
// Worker-only bounded public sightings adapter. No network or durable state.
// Caller schedules <=1 request/second, handles HTTP failures/backoff and caps pages.
struct InaturalistPage {
    std::vector<std::string> normalizedRecords;
    std::vector<std::string> explicitlyRevokedIds;
    std::map<std::string, std::string> originalTimestampEvidence;
    std::size_t invalidRecords{}, totalResults{}, page{}, perPage{}, returnedRecords{};
};
std::string inaturalistRequestUrl(std::string_view startUtc,
                                std::string_view endUtc, unsigned page = 1);
std::string inaturalistRevalidationUrl(const std::vector<std::string>& providerIds);
InaturalistPage parseInaturalistResponse(std::string_view json,
                                        std::string_view nowUtc);
}
