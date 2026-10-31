# M7 ClassFareConfigModule design

## SRS requirements

RRS-F-007 requires an authenticated administrator to define one or more classes for a train. The
allowed codes are `SL`, `3A`, `2A`, `1A`, `CC`, and `2S`; every class has a positive integer seat
capacity. TC-CLASS-01 requires train 12345 to report exactly `SL(72)` and `3A(64)`, and requires
rejection of an unsupported code or non-positive capacity. C-007 bounds a train at ten class
records. RRS-SR-002 restricts class and capacity creation/modification to administrators.
RRS-F-023 requires master-data changes to be audited with timestamp, actor, role, action code and
affected record ID.

The SRS identifies `classes.dat` with logical fields train number, class code and seat capacity,
but does not define the record bytes. This implementation follows the approved Issue #21 design
below. Both SRS C-007 and the Test Plan specify a maximum of ten classes per train. The allowed
class-code set contains six values: `SL`, `3A`, `2A`, `1A`, `CC`, and `2S`. M7 rejects duplicate
`(train number, class code)` pairs, so only six distinct classes can currently be configured per
train through valid operations. The ten-class guard remains implemented, but its boundary cannot
be reached under the six-code and uniqueness rules. The Test Plan's fixture description of 1–10
classes drawn from the six codes does not clarify whether duplicate class codes are intended to be
possible.

## Record format (implementation decision)

Each `classes.dat` record is exactly 16 bytes. Encoding is explicit; no C++ struct is serialized.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 1 | Format version | `1` |
| 1 | 4 | Train number | Unsigned 32-bit little-endian; nonzero |
| 5 | 2 | Class code | Exact two-byte ASCII code from the SRS set |
| 7 | 4 | Capacity | Unsigned 32-bit little-endian; nonzero |
| 11 | 5 | Reserved | All zero |

The `uint32_t` capacity is an implementation representation. Its maximum representable value is
not an SRS capacity requirement. A future format change must use a new version rather than changing
the existing record interpretation silently.

## Validation and data behavior

All records are decoded and validated when the catalogue is read. Partial files, unsupported
versions, zero train numbers, unsupported class codes, zero capacities, nonzero reserved bytes and
duplicate train/class pairs are errors; records are not skipped. M1 remains generic and handles
only indexed fixed-size opaque records. M7 owns class encoding, validation and duplicate detection.

M7 calls M6 `TrainCatalogueModule::find_train()` to require an active train; it does not read or
decode `trains.dat`. Queries report whether the train exists separately from an empty class list or
missing class lookup. Add rejects an already-configured pair. Update changes the capacity of an
existing pair and permits increases. No remove operation is exposed in Issue #21.

Capacity reductions are refused until M8 exposes a reliable check against allocated seats. M7 does
not assume all seats are free and does not create an M8 API. Fare records and effective dates belong
to Issue #22 and are implemented by the separate `FareConfigModule` within M7; fare calculation
remains outside this class-configuration scope.

## Persistence, authorization and audit

M7 uses the branch's `PersistenceStore::{record_count,read_record,write_record}` API and
`AtomicJournal` for each class append or indexed update. A process-wide M7 mutex protects record
reads and read/modify/write decisions across module instances in this process. The per-train count
is checked before append.

Each mutation calls `AuthorizationModule::require_administrator()` with operation
`MANAGE_CLASSES`. M5 records a passenger denial once; M7 does not duplicate that denial entry.
Successful writes append one M3 entry with action `CLASS_ADDED` or `CLASS_UPDATED` and record ID
`TRAIN_NUMBER:CLASS_CODE` (for example `12345:SL`). Audit append occurs after persistence. If it
fails, M7 reports that the class mutation persisted but audit logging failed; the two actions are
not represented as one atomic transaction.

## Console integration

The authenticated administrator `ManageClasses` action supports add, capacity update, list/enquiry
and return to the administrator menu. It receives the active `Session`, shared data directory,
shared `AuditLog`, timestamp provider and session-activity callback. Each input step checks session
activity so the existing 15-minute idle timeout remains in force during this interaction.
