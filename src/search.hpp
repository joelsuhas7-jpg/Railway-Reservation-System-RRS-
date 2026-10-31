#pragma once

#include <functional>
#include <iosfwd>
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

struct SearchResult {
    std::vector<TrainRecord> trains;
    SearchStatus status{SearchStatus::ResultsFound};
};

// M8 integration seam. Unknown means M8 is not available; it is never treated as zero seats.
using AvailabilityEvaluator =
    std::function<AvailabilityStatus(const TrainRecord&, std::string_view)>;

class SearchModule final {
public:
    explicit SearchModule(const TrainCatalogueModule& catalogue) : catalogue_(catalogue) {}

    SearchResult search(const SearchQuery& query,
                        const AvailabilityEvaluator& availability = {}) const;

    // Render the six train fields used by TC-SEARCH-02 without truncating decoded text.
    static void render_results(std::ostream& out, const std::vector<TrainRecord>& trains);

private:
    const TrainCatalogueModule& catalogue_;
};

}  // namespace rrs
