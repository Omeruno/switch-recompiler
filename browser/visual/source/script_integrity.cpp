#include "script_integrity.h"
#include "ws_codec.h"
#include <algorithm>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/version.h>
#include <sstream>
#include <vector>
namespace visual {
bool script_integrity_matches(const std::string &metadata, const uint8_t *bytes, size_t size) {
    if (metadata.size() > 8192)
        return false;
    unsigned strongest = 0;
    std::vector<std::string> candidates;
    std::istringstream stream(metadata);
    std::string token;
    while (stream >> token) {
        unsigned algorithm = token.rfind("sha512-", 0) == 0   ? 512
                             : token.rfind("sha384-", 0) == 0 ? 384
                             : token.rfind("sha256-", 0) == 0 ? 256
                                                              : 0;
        if (!algorithm || algorithm < strongest)
            continue;
        if (algorithm > strongest) {
            candidates.clear();
            strongest = algorithm;
        }
        auto value = token.substr(token.find('-') + 1);
        value.resize(value.find('?') == std::string::npos ? value.size() : value.find('?'));
        std::replace(value.begin(), value.end(), '-', '+');
        std::replace(value.begin(), value.end(), '_', '/');
        while (!value.empty() && value.back() == '=')
            value.pop_back();
        candidates.push_back(value);
    }
    if (!strongest)
        return true; // Unknown algorithms do not add integrity metadata.
    uint8_t digest[64]{}, empty = 0;
    const auto *input = size ? bytes : &empty;
    int result;
#if MBEDTLS_VERSION_MAJOR >= 3
    result = strongest == 256 ? mbedtls_sha256(input, size, digest, 0)
                              : mbedtls_sha512(input, size, digest, strongest == 384);
#else
    result = strongest == 256 ? mbedtls_sha256_ret(input, size, digest, 0)
                              : mbedtls_sha512_ret(input, size, digest, strongest == 384);
#endif
    if (result)
        return false;
    auto encoded = ws::base64(digest, strongest / 8);
    while (!encoded.empty() && encoded.back() == '=')
        encoded.pop_back();
    return std::find(candidates.begin(), candidates.end(), encoded) != candidates.end();
}
} // namespace visual
