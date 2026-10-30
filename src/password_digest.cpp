#include "password_digest.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <random>
#include <stdexcept>

namespace rrs {
namespace {

PasswordHash derive_hash(std::string_view password, const PasswordSalt& salt) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("cannot allocate SHA-256 context");
    }

    PasswordHash digest{};
    PasswordHash next_digest{};
    unsigned int digest_size = 0;
    const bool initialized = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
    const bool salt_added = initialized && EVP_DigestUpdate(context, salt.data(), salt.size()) == 1;
    const bool password_added = salt_added &&
                                EVP_DigestUpdate(context, password.data(), password.size()) == 1;
    const bool first_round = password_added &&
                             EVP_DigestFinal_ex(context, digest.data(), &digest_size) == 1 &&
                             digest_size == digest.size();
    if (!first_round) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("SHA-256 password digest failed");
    }

    for (std::size_t iteration = 1; iteration < kPasswordHashIterations; ++iteration) {
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
            EVP_DigestUpdate(context, digest.data(), digest.size()) != 1 ||
            EVP_DigestFinal_ex(context, next_digest.data(), &digest_size) != 1 ||
            digest_size != next_digest.size()) {
            EVP_MD_CTX_free(context);
            OPENSSL_cleanse(digest.data(), digest.size());
            OPENSSL_cleanse(next_digest.data(), next_digest.size());
            throw std::runtime_error("SHA-256 password digest failed");
        }
        digest = next_digest;
    }

    EVP_MD_CTX_free(context);
    OPENSSL_cleanse(next_digest.data(), next_digest.size());
    return digest;
}

PasswordSalt generate_salt() {
    std::random_device random_source;
    std::uniform_int_distribution<unsigned int> byte_distribution(0, 255);

    PasswordSalt salt{};
    for (auto& byte : salt) {
        byte = static_cast<std::uint8_t>(byte_distribution(random_source));
    }
    return salt;
}

}  // namespace

PasswordDigest hash_password(std::string_view password) {
    PasswordDigest result;
    result.salt = generate_salt();
    result.hash = derive_hash(password, result.salt);
    return result;
}

bool verify_password(std::string_view password, const PasswordDigest& stored) {
    auto candidate = derive_hash(password, stored.salt);
    const bool matches = CRYPTO_memcmp(candidate.data(), stored.hash.data(), candidate.size()) == 0;
    OPENSSL_cleanse(candidate.data(), candidate.size());
    return matches;
}

}  // namespace rrs
