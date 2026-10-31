# M9 Search Design

## SRS requirements

RRS-F-010 requires an exact source, destination, and journey date search. SRS v2.0 Section 4.3.1 specifies a `std::vector<TrainRecord>`, a `std::multimap` keyed by station-code pair, an `equal_range` route probe, the Monday-zero seven-bit running mask, removal of departures already in the past, and ascending departure-time ordering. The SRS names three empty-result explanations: no train serves the route; trains serve it but none runs on the requested day; no class has free seats on this date.

## Implementation decisions

Each `search()` call reads `TrainCatalogueModule::list_active_trains()`, builds a vector snapshot and route multimap locally, and discards both after the query. This makes M6 additions, updates, and removals visible on the next search without cache invalidation. The catalogue remains the only owner of the 128-byte train record format.

Both query station codes are validated using the M6 rule: one to eight uppercase ASCII letters or digits. Invalid codes throw `std::invalid_argument`; route matching remains exact and case-sensitive. Source and destination must differ.

Journey dates are strictly validated as Gregorian `YYYY-MM-DD`, including leap years. Weekday calculation uses a proleptic Gregorian day number and the known Thursday anchor 1970-01-01; Monday is bit 0. This avoids locale, daylight-saving, and platform `mktime` variation. The caller supplies current date and time explicitly in the same civil time basis used for the journey station. This keeps tests deterministic and leaves timezone acquisition to the eventual UI boundary.

When the journey date is before the supplied current date, all departures are returned as the structured `AllDeparturesHavePassed` state. On the current date, departures strictly earlier than the supplied current time are filtered; a departure exactly at the supplied time remains eligible. The SRS says already-passed departures are discarded but does not define the empty-result wording when every candidate is past. M9 therefore does not label this state with one of the three specified explanations or invent a message.

`SearchResult::status` distinguishes `ResultsFound`, `NoTrainServesRoute`, `NoTrainRunsOnDate`, `NoClassHasFreeSeats`, and `AllDeparturesHavePassed`. The first three non-success statuses correspond to the SRS empty-result explanations; the past-departure state remains separate. `NoClassHasFreeSeats` may accompany schedule rows in `trains`, because the SRS requires those trains to remain listable with no-bookable-class information.

Departures sort first by ascending `HH:MM`; equal departure times sort by ascending train number. This secondary key is an M9 deterministic-ordering decision.

Availability can use an injected evaluator returning `HasBookableClass`, `NoBookableClass`, or `Unknown`. The third SRS explanation is returned only when every remaining train is confirmed to lack a bookable class. Unknown availability is not treated as zero seats. A second provider can supply each configured class with its capacity, allocated seat count, and optional fare in paise. Search computes free seats as `capacity - allocated_seats`, rejects allocations above capacity, and retains classes without a fare with the `not open` display label. `render_search_results()` prints the per-train class code, fare in rupees, and free-seat count. When the per-class provider is not supplied, the legacy evaluator behavior remains available. M7's fare module is still a stub in this checkout, so production wiring awaits the fare module implementation.

## Performance verification

The `[performance]` Catch2 case constructs 2,000 valid M6-compatible 128-byte records before timing. Construction time is reported separately. It then measures 100 calls to `SearchModule::search()` with a fixed query and injected current date/time, with no terminal I/O or assertion reporting in the timed interval. It reports nearest-rank p95 (sorted sample 95) and maximum latency. Compare the reported p95 with the Test Plan limit of 200 ms; this is a diagnostic benchmark rather than a machine-dependent pass/fail threshold. Record the actual build configuration and measured values with verification results.

The baseline Windows/MSYS2 g++ 16.2.0 Release build using Ninja and `-Wall -Wextra -Werror -Wno-error=unused-function` measured p95 at 385,423/335,131 us and maximum search latency at 424,911/360,399 us. The bottleneck was M6 `read_all()` calling M1 `read_record()` separately for every train, reopening the file each time. M1 now exposes `PersistenceStore::read_records()` to open once and read records in index order; M6 still decodes, validates, and checks uniqueness, and M9 still requests fresh data for every search.

After that change, a later Release/Ninja/g++ 16.2.0 diagnostic benchmark measured dataset construction at 2,740 us, p95 at 5,390 us, and maximum search latency at 43,455 us. The 200 ms p95 target passed in this run. A process-level Windows Working Set sample at 100 ms intervals from the preceding benchmark run peaked at 9,428,992 bytes (about 9.00 MiB), below 64 MiB during that run. This is only a sampled Working Set value, not a measurement of total committed memory or a definitive peak RSS measurement. The Test Plan's peak RSS requirement is therefore not formally verified by this sample. The benchmark process used an isolated temporary root under the workspace because this environment denies rename operations under its `%TEMP%` AppContainer path.

The portable Catch2 benchmark does not measure process memory. A target-platform peak RSS measurement remains necessary to verify the 64 MiB memory requirement. The 2,000-record dataset demonstrates correctness at the catalogue limit but is not itself a memory measurement.
