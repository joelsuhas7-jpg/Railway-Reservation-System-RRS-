#include <catch2/catch_test_macros.hpp>

#include "password_digest.hpp"

namespace {

// SHA-256(salt || password), then SHA-256 of the previous digest, 10,000 rounds in all,
// for an all-zero salt and "Pass1Secure". Computed independently with Python hashlib.
constexpr rrs::PasswordHash kKnownHash = {
    0xff, 0xc8, 0x65, 0xf4, 0x44, 0x51, 0x6e, 0x6c, 0xfe, 0x02, 0xed, 0x61, 0x3f, 0x58, 0x43, 0x00,
    0x94, 0x63, 0xbc, 0xd3, 0x08, 0xc1, 0x1a, 0x72, 0x14, 0x81, 0x90, 0xf1, 0x1a, 0x53, 0x26, 0x5b};

}  // namespace

TEST_CASE("TC-SEC-04: digest uses a 16-byte salt and 10,000 rounds of SHA-256",
          "[unit][M4]") {
    CHECK(rrs::kPasswordSaltSize == 16);
    CHECK(rrs::kPasswordDigestSize == 32);
    CHECK(rrs::kPasswordHashIterations == 10'000);

    const rrs::PasswordDigest known{rrs::PasswordSalt{}, kKnownHash};
    CHECK(rrs::verify_password("Pass1Secure", known));
    CHECK_FALSE(rrs::verify_password("Pass1Securf", known));
}

TEST_CASE("TC-SEC-04: the correct password verifies and a wrong one does not",
          "[unit][M4]") {
    const auto digest = rrs::hash_password("Pass1Secure");
    CHECK(rrs::verify_password("Pass1Secure", digest));
    CHECK_FALSE(rrs::verify_password("pass1secure", digest));
    CHECK_FALSE(rrs::verify_password("", digest));

    auto tampered = digest;
    tampered.salt[0] ^= 0x01U;
    CHECK_FALSE(rrs::verify_password("Pass1Secure", tampered));
}

TEST_CASE("TC-SEC-04: every digest gets a distinct salt", "[unit][M4]") {
    const auto first = rrs::hash_password("Pass1Secure");
    const auto second = rrs::hash_password("Pass1Secure");
    CHECK(first.salt != second.salt);
    CHECK(first.hash != second.hash);
    CHECK(rrs::verify_password("Pass1Secure", second));
}
