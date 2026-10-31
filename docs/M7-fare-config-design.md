# M7 Fare Configuration Design

## Scope and requirements

Issue #22 implements per-train/class fare configuration in M7. RRS-F-008 requires a flat
per-passenger fare recorded in paise with an effective-from date. The amount must be positive,
must not contain more than two decimal places at the console boundary, and must not be rounded.
The effective-from date is an ISO 8601 date and cannot be earlier than the current UTC date.
The acceptance example is Rs 450.00 stored and returned as 45000 paise. A class with no
applicable fare has the booking-facing status `class not open for booking`.

Fare storage is implemented by a separate `FareConfigModule` and `fares.dat`; the existing
`ClassFareConfigModule`, `classes.dat` format, and class add/update/list behavior are unchanged.
M8 inventory, M11 fare calculation, and booking orchestration remain outside this issue.

## Public API and dependencies

`FareConfigModule` receives `PersistenceStore`, M6 `TrainCatalogueModule`, M7
`ClassFareConfigModule`, M3 `AuditLog`, and an injected UTC timestamp provider. Public mutations are
`configure_fare` and `replace_fare`. `list_fares` returns all dated fare records for a pair, and
`find_applicable_fare` returns the applicable dated record or an explicit lookup state. A missing
applicable fare returns exactly `class not open for booking`.

Before a mutation validates fare/date/business input, it obtains and validates the injected UTC
timestamp and calls M5 `require_administrator` with `MANAGE_FARES`. M6 verifies an active train and
M7 verifies a configured class. The module does not read or decode `trains.dat` or `classes.dat`.

## Record format (Issue #22 implementation decision)

The SRS names `fares.dat` and its logical fields but does not define field offsets, record size,
versioning, endianness, or reserved bytes. This implementation uses an explicit 32-byte fixed record;
it never serializes a C++ struct.

| Offset | Size | Field | Encoding and validation |
|---:|---:|---|---|
| 0 | 1 | Format version | `1` |
| 1 | 4 | Train number | Nonzero `uint32_t`, little-endian |
| 5 | 2 | Class code | Exact ASCII code `SL`, `3A`, `2A`, `1A`, `CC`, or `2S` |
| 7 | 8 | Fare in paise | Positive signed 64-bit value represented little-endian; values above `INT64_MAX` are rejected |
| 15 | 10 | Effective-from date | ASCII `YYYY-MM-DD`; valid Gregorian date |
| 25 | 7 | Record revision | Nonzero unsigned 56-bit little-endian value |

M1 `PersistenceStore` reads and writes fixed-size opaque records. M7 owns fare encoding and
validation. Unsupported versions, partial files, invalid fields, zero revisions, duplicate
`(train_number, class_code, effective_from)` keys, and duplicate revision identifiers are errors;
records are not skipped.
No total fare-record cap is imposed because the requirements specify none and dated history can
grow over time.

## Amount and effective-date behavior

The console parser accepts a positive decimal rupee amount with zero, one, or two fractional digits
and converts it using integer arithmetic only (`450.00` -> `45000`, `450` -> `45000`, `450.5` ->
`45050`). Empty, malformed, negative, zero, over-precision, and overflowing values are rejected.
The public module API accepts only integer paise.

Dates use strict Gregorian `YYYY-MM-DD` validation. Configuration and replacement reject a date
earlier than the UTC date obtained from the injected ISO UTC timestamp provider. Tests inject this
provider; they do not use machine time. The effective date is inclusive. For a requested journey
date, lookup selects the record with the greatest effective-from date less than or equal to that
journey date. Using journey date for applicability is an implementation interpretation: the SRS
says the class becomes bookable “from” the effective date but does not explicitly choose booking
date versus journey date.

## Duplicate and replacement behavior

Multiple effective dates may coexist for a train/class pair. A new record with an existing composite
key is refused; it is never silently overwritten. `replace_fare` identifies the exact existing
record by its effective date and the revision captured from the administrator's selection. If the
persisted revision differs, the request is stale and refused even if the amount has since returned to
its prior value. Every successful mutation receives a revision greater than every revision currently
stored. If the replacement moves its date, the target composite key must be unused. Other fare history
remains unchanged. The console lists
future-effective records and requires the administrator to choose either a new date or the exact
record number to replace; it never selects an arbitrary future record. Replacement is limited to a
currently or future-effective record, preserving past fare history.

The SRS requires a prompt when an existing fare has a future effective date but does not define
same-date behavior, multiple future records, or how a replacement changes the record. These are the
Issue #22 implementation choices described above.

## Persistence, authorization, and audit

The module uses a process-wide M7 fare mutex followed by M1's exclusive `PersistenceFileLock` on the
stable sidecar `fares.dat.lock`. The sidecar is not deleted when a lock is released. The transaction
lock covers the fresh fare read, duplicate/selection checks, index selection, the AtomicJournal
commit, and the success-audit append attempt. Read-only fare listing and lookup use the same lock so
they do not observe a fare write in progress. The lock order is M7 process mutex, M1 `fares.dat.lock`,
then AtomicJournal's `.rrs-atomic-journal.lock`; all fare mutations follow that order. M0
PlatformShim implements blocking exclusive locks with POSIX `fcntl(F_SETLKW)` and Win32
`LockFileEx`; target-specific APIs remain in M0. M1 exposes the persistence-level RAII guard, and M7
does not call platform APIs directly.

Journal construction performs recovery while the fare sidecar lock is held. Each append or indexed
replacement is committed through AtomicJournal, whose internal lock is acquired only after the M7
transaction lock. The journal provides recovery for the data write; the fare lock is released by RAII
on every return or exception and its lock file remains in place. Audit append follows persistence
and is not part of the journal transaction. If audit append fails, the result reports that the fare
was persisted but audit logging failed; recovery cannot make the audit and fare update one atomic
transaction.

Replacement re-resolves the selected `(train, class, effective date)` key under the lock and compares
its persisted revision (and displayed amount) with the administrator's captured selection. A stale
selection is refused. Revisions are unsigned 56-bit little-endian values and never wrap: mutations
are refused when the maximum revision is already stored. Replacements, including same-value
replacements, always receive a new revision. There is no deletion operation; a future deletion feature
must preserve the revision high-water mark so identifiers are never reused.

`PersistenceFileLock` supplies cross-process exclusion. POSIX `fcntl` locks are process-associated,
so callers must also serialize same-process operations on one file. M7's process-wide fare mutex
provides that coordination for `fares.dat`; the lock order remains process mutex, M1 sidecar lock,
then AtomicJournal lock.

M5 owns passenger-denial auditing; M7 does not add a second denial entry. Successful writes append
one M3 record using `FARE_CONFIGURED` or `FARE_UPDATED`, actual actor and role, the injected UTC
timestamp, and record ID `TRAIN_NUMBER:CLASS_CODE:YYYY-MM-DD`. Audit append follows durable
persistence. If it fails, the result says the fare was persisted but audit logging failed; the
journal and append-only `audit.log` are not one transaction.

## Console integration and limitations

The administrator `ManageFares` action passes the authenticated session, shared data directory,
shared audit log, timestamp provider, and session-activity callback. Input collection checks the
existing 15-minute idle timeout. Passenger menu visibility remains unchanged, and every public fare
mutation independently calls M5.

M7 exposes an absent-fare result with the required booking-facing message. The booking workflow is
not implemented here; M8, M11, and booking orchestration must remain separate issues. The configured
fare records are ready for a later booking-facing caller to query by journey date.

Cross-process integration tests launch independent copies of the Catch2 test executable and
synchronize them at a file barrier before configuring distinct fares, duplicate keys, and competing
replacements in one isolated data directory. The tests verify resulting fare records and audit
entries after reopening the module.
