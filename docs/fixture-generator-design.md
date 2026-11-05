# ENB-06 Deterministic Fixture Generator

## Scope and specification status

ENB-06 adds a deterministic fixture plan for exactly 2,000 trains, M6/M7 API-based
materialization, a fixture-scoped consistency checker, and a reproducibility manifest. It does
not change M6/M7 record formats or business rules, M9 search behavior, CMake, or CI. Existing
`load_seed()` and `validate_seed()` remain the known-answer fixture API and retain their prior
behavior.

The Test Plan and Issue text ask for 1–10 classes per train. M7 currently accepts only six codes
(`SL`, `3A`, `2A`, `1A`, `CC`, `2S`) and rejects duplicate `(train_number, class_code)` pairs.
Generated plans therefore contain one to six distinct supported classes per train. No extra
codes or duplicate class records are fabricated. The literal 1–10 condition remains unresolved;
this implementation does not claim that part is satisfied.

## Plan generation

`generate_fixture_plan(seed)` performs no filesystem access and returns owned strings and values.
It always creates 2,000 trains, with train numbers 300000 through 301999. Names are printable
ASCII; station codes are selected from a fixed set of uppercase alphanumeric codes; source and
destination differ; times are valid `HH:MM`; and running masks are nonzero seven-bit values.
Three initial records use the `SBC` to `NDLS` route and daily masks to guarantee a useful search
route for every seed. Remaining routes, schedules, masks, class order/count, capacities, and fares
vary deterministically with the seed. Classes are selected without replacement from the six M7
codes. Capacities are positive `uint32_t`; fares are positive integer paise; no floating-point
currency is used. Each class receives one fare effective on `2026-10-30`.

The logical fixture reference timestamp is fixed at `2026-10-30T10:00:00Z` and its effective
date is `2026-10-30`; both are deterministic fixture inputs and are included in the manifest and
logical fingerprint. Runtime creation time, filesystem timestamps, and machine clock values are
never sampled or included. Fare validation therefore does not depend on the host clock, locale,
or timezone. Schedule values are civil station-local `HH:MM` values; no timezone conversion is
made by fixture generation.

Generator algorithm version 1 uses SplitMix64 with unsigned 64-bit wraparound. For each output,
the state is incremented by `0x9e3779b97f4a7c15`, then mixed with xor-shift/multiply constants
`0xbf58476d1ce4e5b9` and `0x94d049bb133111eb`, followed by xor-shift 31. Bounded values use
rejection sampling with threshold `(-bound) % bound` before modulo reduction. This avoids
implementation-specific standard distributions and modulo bias. A zero bound is an error.

## Materialization and verification boundary

`materialize_fixture(plan, directory)` requires a fresh or empty directory, validates the complete
plan, constructs the existing persistence and module objects, and writes in stable order. Generated
plans contain exactly 2,000 trains; validated projections may contain fewer trains for focused
integration scenarios, and their manifest counts are recomputed from the projected records:

1. all trains through `TrainCatalogueModule::add_train()`;
2. classes in train and generated-class order through `ClassFareConfigModule::add_class()`;
3. fares in the same order through `FareConfigModule::configure_fare()`.

It uses a synthetic administrator session and the fixed timestamp. Module failures stop the run
and return operation/record context. Partial files may remain after failure, but no verified
handle is returned. Only after the consistency report has no violations does the result contain a
`VerifiedFixture`; the deterministic `fixture-manifest-v1.txt` artifact is written after successful
consistency verification, and the handle is returned only if that write succeeds. The artifact has
stable newline-terminated `key=value` fields and contains no absolute path or creation timestamp.
The fingerprint covers the logical plan only, not the artifact's bytes. Tier-3 or performance
consumers should require this handle before reading the fixture. There is no CTest tier ordering
in this repository, so verification is an API boundary, not a new CI execution tier.

The checker reads trains, classes, and fares through their module read APIs and reads record
counts through generic M1 `PersistenceStore::record_count()`. M7 exposes additive bulk read
helpers (`list_all_classes()` and `list_all_fares()`) so the checker can inspect each catalogue
once rather than reopening the complete fare file for every class. M7 mutation behavior and
serialization are unchanged. The checker verifies expected active train records and fields,
unique IDs and class pairs, exact class/capacity and fare/date values, sequential deterministic
fare revisions, positive values, no missing or extra records, no orphan relationships, persisted
record counts, and the plan fingerprint. Verification requires a dedicated, isolated fixture
directory with no concurrent writers: the enumeration APIs do not promise a cross-module snapshot.
The checker is not a general consistency audit or a concurrent production-data snapshot.

## Manifest and fingerprint

`format_fixture_manifest()` emits newline-terminated UTF-8/ASCII `key=value` lines in fixed
order: format, algorithm, seed, timestamp, effective date, three counts, and a 16-digit lowercase
hex fingerprint. The fingerprint is FNV-1a 64-bit (basis `14695981039346656037`, prime
`1099511628211`), a reproducibility fingerprint rather than a security checksum. Its canonical
logical input starts with a length-prefixed domain string, then generator version (4-byte
little-endian), seed (8-byte little-endian), the fixed logical reference timestamp and effective
date (UTF-8 strings with
8-byte little-endian byte lengths), train count (8-byte little-endian), then trains in stored plan
order (generated plans are ascending by train number, and projections preserve that order). Each
train contributes number (4-byte little-endian), its five strings in field order
(name, source, destination, departure, arrival), running mask (one byte), class count (8-byte
little-endian), followed by classes in stored order with code, capacity (4-byte little-endian),
fare paise as an 8-byte little-endian unsigned representation of its positive value, and effective
date. Strings are bytewise UTF-8; generated strings are ASCII. The deterministic logical reference
timestamp/date above are included; runtime creation time, filesystem timestamps and paths, locks,
journals, and host properties are excluded.

## TC-PERF-02 boundary

The single TC-PERF-02 case materializes and verifies the projected 2,000-train/2,000-class/
2,000-fare fixture through M6/M7, persists its manifest, then runs the searches. It prepares 100
deterministic station/date/time queries and derives complete expected train IDs and order from
the logical plan. `steady_clock` surrounds only each `SearchModule::search()` call; fixture
generation/materialization, verification, manifest persistence, query construction, result
extraction, output, and assertions are outside the timed intervals. It reports nearest-rank p95
(sorted index 94) and maximum latency. The 200 ms target is a measured result, not a guaranteed
hosted-runner threshold; it is not asserted without specified reference hardware. Peak RSS
measurement remains separate from fixture generation and must be assessed under TC-SIZE-02.

To keep the search benchmark's M7 setup to the minimum valid record count, its deterministic plan
projection keeps one of each train's generated classes and recomputes train, class, and fare counts
from the projected records. Every train has one valid configured class and one fare in this
projection, while normal generated plans retain their seed-dependent one-to-six class counts. The
separate small integration scenario materializes trains with one, three, and six classes, reads
their configured classes and fares through M6/M7, and tests consistency-checker failures.
Full-range class-count generation is checked separately by unit tests.

## Limitations

- The six-supported-code versus 1–10-class requirement needs specification clarification.
- The manifest fingerprint detects logical fixture differences but is not cryptographically
  secure.
- A failed API materialization can leave partial data in the target directory; the directory
  must be discarded before retry. It is never represented as verified.
- The benchmark's actual p95 depends on the host and storage environment and must be reported with
  build configuration and measured values.
