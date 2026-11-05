#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "class_fare_config.hpp"
#include "fare_config.hpp"
#include "train_catalogue.hpp"

namespace rrs {

inline constexpr std::size_t kGeneratedFixtureTrainCount = 2000;
inline constexpr std::uint32_t kFixtureGeneratorVersion = 1;
inline constexpr char kFixtureTimestamp[] = "2026-10-30T10:00:00Z";
inline constexpr char kFixtureDate[] = "2026-10-30";
inline constexpr char kFixtureManifestFile[] = "fixture-manifest-v1.txt";

class SplitMix64 final {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}
    std::uint64_t next() noexcept;
    std::uint64_t bounded(std::uint64_t exclusive_upper_bound);

private:
    std::uint64_t state_;
};

struct GeneratedClassFare {
    std::string class_code;
    std::uint32_t capacity{0};
    std::int64_t fare_paise{0};
    std::string effective_from;
};

struct GeneratedTrain {
    TrainRecord train;
    std::vector<GeneratedClassFare> classes;
};

struct FixtureManifest {
    std::uint64_t seed{0};
    std::uint32_t generator_version{kFixtureGeneratorVersion};
    std::string timestamp{kFixtureTimestamp};
    std::string effective_from{kFixtureDate};
    std::size_t train_count{0};
    std::size_t class_count{0};
    std::size_t fare_count{0};
    std::uint64_t fingerprint{0};
};

struct GeneratedFixturePlan {
    FixtureManifest manifest;
    std::vector<GeneratedTrain> trains;
};

struct FixtureCheckReport {
    std::vector<std::string> violations;
    bool ok() const noexcept { return violations.empty(); }
};

FixtureCheckReport validate_fixture_plan(const GeneratedFixturePlan& plan);
std::string format_fixture_manifest(const FixtureManifest& manifest);

struct FixtureMaterializationResult;
FixtureMaterializationResult materialize_fixture(const GeneratedFixturePlan& plan,
                                                 const std::filesystem::path& data_directory);

class VerifiedFixture final {
public:
    const std::filesystem::path& data_directory() const noexcept { return data_directory_; }
    const FixtureManifest& manifest() const noexcept { return manifest_; }

private:
    VerifiedFixture(std::filesystem::path path, FixtureManifest manifest)
        : data_directory_(std::move(path)), manifest_(std::move(manifest)) {}
    std::filesystem::path data_directory_;
    FixtureManifest manifest_;
    friend FixtureMaterializationResult materialize_fixture(
        const GeneratedFixturePlan&, const std::filesystem::path&);
};

struct FixtureMaterializationResult {
    std::optional<VerifiedFixture> verified;
    FixtureCheckReport report;
    std::string error;
    bool ok() const noexcept { return verified.has_value() && report.ok() && error.empty(); }
};

GeneratedFixturePlan generate_fixture_plan(std::uint64_t seed);
std::uint64_t fixture_fingerprint(const GeneratedFixturePlan& plan) noexcept;
FixtureCheckReport check_generated_fixture(const GeneratedFixturePlan& plan,
                                           const PersistenceStore& store,
                                           const TrainCatalogueModule& trains,
                                           const ClassFareConfigModule& classes,
                                           const FareConfigModule& fares);
FixtureMaterializationResult materialize_fixture(const GeneratedFixturePlan& plan,
                                                 const std::filesystem::path& data_directory);

}  // namespace rrs
