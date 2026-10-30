# M6 Train Catalogue Design

## Scope and traceability

This note separates SRS requirements from implementation decisions for Issue #20, M6
TrainCatalogueModule. Implementation choices here do not amend the SRS, SAD, or Test Plan.

## SRS requirements

RRS-F-006 requires administrator add, update, and remove operations for train number, name,
source, destination, departure and arrival times, and a seven-bit running-days mask. The SRS sets
the train catalogue limit at 2,000 records and describes train records as 128 bytes in the M9
search mechanics. RRS-SR-002 restricts master-data changes to administrators. RRS-SR-003 requires
validation before persistence. RRS-F-023 requires append-only audit entries for master-data
changes.

## Implementation decisions

## Approved `trains.dat` record format

The catalogue stores exactly 128 bytes per record in `trains.dat`, encoded field by field. It
does not write a C++ object representation to disk.

| Offset | Bytes | Field | Encoding |
|---:|---:|---|---|
| 0 | 1 | Format version | `uint8_t`, currently 1 |
| 1 | 1 | Status | 1 active, 0 removed |
| 2 | 4 | Train number | nonzero `uint32_t`, little-endian |
| 6 | 40 | Train name | printable ASCII, right-padded with spaces |
| 46 | 8 | Source station | uppercase letters/digits, right-padded with spaces |
| 54 | 8 | Destination station | uppercase letters/digits, right-padded with spaces |
| 62 | 5 | Departure time | `HH:MM`, 24-hour |
| 67 | 5 | Arrival time | `HH:MM`, 24-hour |
| 72 | 1 | Running-days mask | Monday in bit 0 through Sunday in bit 6; bit 7 zero |
| 73 | 55 | Reserved | zero-filled |

The mask must have at least one weekday set. The mapping follows the fixture and approved design:
bit 0 is Monday through bit 6 Sunday. The daily mask is `0x7F`, Monday–Friday is `0x1F`, and
Saturday–Sunday is `0x60`. Arrival is not required to be later than departure, allowing overnight
journeys.

Train names allow printable ASCII and internal spaces, but leading and trailing spaces are
rejected so decoding the right-space-padded field round-trips the accepted name exactly.
Timestamps supplied to M6 mutations must be valid UTC values in `YYYY-MM-DDTHH:MM:SSZ` form,
including valid Gregorian month/day and leap-year combinations.

All train numbers, including soft-removed records, remain reserved. Train number is immutable
on update. The 2,000-record bound includes removed records. A malformed, partial, unsupported,
invalid-status, or duplicate record fails catalogue access rather than being skipped.

## Persistence, authorization, and audit

M6 uses the existing `PersistenceStore` with `AtomicJournal` to append or replace one 128-byte
record. There is no in-memory train cache; active list and find operations read persisted state,
so a successful change is immediately visible to subsequent M6 reads.

Each mutation first validates the supplied timestamp to prevent malformed values from entering
the audit log. It then calls `AuthorizationModule::require_administrator` using `ADD_TRAIN`,
`UPDATE_TRAIN`, or `REMOVE_TRAIN` before validating train fields or reading/writing catalogue
records. M5 owns denial auditing. After the journaled write succeeds, M6 appends exactly one
`TRAIN_ADDED`, `TRAIN_UPDATED`, or `TRAIN_REMOVED` event with timestamp, actor ID, `ADMIN` role,
and decimal train number. If audit append fails after the train record is persisted, M6 returns a
system failure stating that the mutation persisted but audit logging failed.

All M6 reads and mutations across `TrainCatalogueModule` instances in one process share a process-
wide mutex. Mutations hold it from validation and catalogue read through persistence and audit
handling; find/list reads hold it while reading. This closes stale-index races between in-process
callers. The supported writer model is one application process per data directory. Multiple
independent processes writing the same directory are not supported; in particular, the Windows
`FileLock` implementation is a stub.

The existing journal and text AuditLog do not offer one transaction spanning both files. M6
persists the train mutation durably, then appends its success audit event. If append fails, it
returns a system failure that explicitly says the train mutation persisted but audit logging
failed. A process crash between persistence and audit append can leave an unaudited catalogue
mutation. This limitation is surfaced and documented; M6 does not claim the two files form one
atomic transaction.

## Removal and downstream behavior

Removal changes only the train status byte. It does not delete or modify bookings, classes,
fares, or inventory. Public M6 find/list operations expose active trains only; removed records
remain internally available for duplicate detection and historical references.

M9 SearchModule remains separate and is not implemented here. `list_active_trains()` is the M6
source for active catalogue records. M9 must read fresh catalogue state or implement cache
invalidation so subsequent searches reflect add, update, and remove operations, and must exclude
removed trains. Search acceptance is not complete until M9 is integrated. Booking and retrieval
modules are also not implemented, so preservation of opaque booking bytes is tested, but future
display and cancellation behavior for removed trains remains to be integrated.

## Console integration boundary

`login_and_run_session` retains its existing one-argument callback API and adds session-aware and
interaction-aware callback overloads. The interaction-aware overload passes the exact `Session`
returned by successful authentication and a check against the same 15-minute inactivity clock
used by the outer session loop. The M17 train-management submenu checks the deadline around each
menu and detail input; an expired session exits the submenu and returns to the public menu. `main`
connects `MenuAction::ManageTrains` to that submenu, which supports add, update, remove, active-list,
and back actions. It uses the application data directory, existing persistence and journal modules,
the append-only audit log, and the existing UTC timestamp utility. M6 mutations still enforce
administrator role at their own API boundary; menu visibility is not the authorization check.
