#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "train_catalogue.hpp"

namespace rrs {

enum class SearchStatus {
    ResultsFound,
    NoTrainServesRoute,
    NoTrainRunsOnDate,
    NoClassHasFreeSeats,
    AllDeparturesHavePassed
};

enum class AvailabilityStatus { HasBookableClass, NoBookableClass, Unknown };

struct SearchQuery {
    std::string source_station;
    std::string destination_station;
    std::string journey_date;  // ISO 8601 YYYY-MM-DD
    std::string current_date;  // injected decision date, same format
    std::string current_time;  // injected decision time, HH:MM
};

struct ClassAvailabilityInput {
    std::string class_code;
    std::size_t capacity{0};
    std::size_t allocated_seats{0};
    // No configured fare means the class is not open for booking.
    std::optional<std::int64_t> fare_paise;
};

struct TrainClassAvailability {
    std::uint32_t train_number{0};
    std::string class_code;
    std::optional<std::int64_t> fare_paise;
    std::size_t free_seats{0};
};

struct SearchResult {
    std::vector<TrainRecord> trains;
    SearchStatus status{SearchStatus::ResultsFound};
    // Per-train rows are kept separately so TrainRecord remains the M6 schedule record.
    std::vector<TrainClassAvailability> class_availability;
};

// M8 integration seam. Unknown means M8 is not available; it is never treated as zero seats.
using AvailabilityEvaluator =
    std::function<AvailabilityStatus(const TrainRecord&, std::string_view)>;

// Supplies every configured class for a train/date, including classes without a fare.
// Allocated seats includes both booked and administratively blocked seats.
using ClassAvailabilityProvider =
    std::function<std::vector<ClassAvailabilityInput>(const TrainRecord&, std::string_view)>;

class SearchModule final {
public:
    explicit SearchModule(const TrainCatalogueModule& catalogue) : catalogue_(catalogue) {}

    SearchResult search(const SearchQuery& query,
                        const AvailabilityEvaluator& availability = {},
                        const ClassAvailabilityProvider& class_availability = {}) const;

private:
    const TrainCatalogueModule& catalogue_;
};

// Renders train details and one fare/free-seat row per configured class. A missing fare is
// rendered as "not open" while the class row remains visible.
void render_search_results(std::ostream& out, const SearchResult& result);

}  // namespace rrs
