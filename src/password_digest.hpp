#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rrs {

// M4 AuthenticationModule password storage and verification primitives.
inline constexpr std::size_t kPasswordSaltSize = 16;
inline constexpr std::size_t kPasswordDigestSize = 32;
inline constexpr std::size_t kPasswordHashIterations = 10'000;

using PasswordSalt = std::array<std::uint8_t, kPasswordSaltSize>;
using PasswordHash = std::array<std::uint8_t, kPasswordDigestSize>;

// The salt is generated for this digest. Store the returned salt and hash with
// the account record; never store or log the supplied plaintext password.
struct PasswordDigest {
    PasswordSalt salt{};
    PasswordHash hash{};
};

// Creates a digest using a fresh 16-byte salt from std::random_device.
// Throws std::runtime_error if the random source or SHA-256 operation fails.
PasswordDigest hash_password(std::string_view password);

// Recomputes the digest using the stored salt and compares it in constant time.
bool verify_password(std::string_view password, const PasswordDigest& stored);

}  // namespace rrs
