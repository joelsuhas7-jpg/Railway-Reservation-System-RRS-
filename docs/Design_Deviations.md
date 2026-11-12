# Deviations from the Submitted Documents

Railway Reservation System, Team 2, UE24CS341A

| Item | Value |
|---|---|
| Code baseline | main branch, commit `dd4b8ae` (development freeze) |
| Reference documents | Software Requirements Specification v2.0; Software Architecture and Design Specification v1.0; Software Test Plan v1.0 |

## 1. Purpose

This document records every point at which the delivered system or the project process differs from the submitted SRS, SAD and Test Plan. Each entry states the original position, the delivered position and the reason. The team presents these entries at the final demonstration.

## 2. Requirements Baseline

The team revised the SRS from v1.0 to v2.0 before implementation began. SRS v2.0 is the authoritative baseline for the implementation and for the System Validation Report.

| Area | SRS v1.0 | SRS v2.0 | Reason |
|---|---|---|---|
| Deployment | Desktop or server; local or networked database | Single-process console application with no sockets, threads or network | A self-contained console program fits the course scope and removes network security concerns |
| Scope boundary | Not stated | Explicit exclusions: waitlist, RAC, Tatkal, dynamic fares, concessions, seat preference, ticket modification, real payment gateway | Fixes the scope that two sprints can deliver |
| Travel class | Not modelled | Classes SL, 3A, 2A, 1A, CC and 2S with per-class capacity | Fares and seat inventory depend on the class |
| Fares | Fare calculation only | Administrator fare configuration separated from fare calculation; amounts in integer paise | Removes rounding error and gives the administrator control of fares |
| Concurrency | Not specified | Advisory lock over the inventory record, re-read inside the lock, `fsync` before release | Two running instances must never sell the same seat |
| Non-functional requirements | General statements | Measurable targets with metrics | Makes each requirement testable |
| Security | General statements | Seven security requirements including a salted iterated SHA-256 digest, account lockout and an idle timeout | Gives the Test Plan concrete security test cases |

## 3. Architecture and Design

The implementation keeps the eighteen modules M0 to M17 of the SAD. Each module maps to one header and one source file in `src/`. The following refinements add units or details that the SAD does not describe.

| Element | SAD v1.0 | Implementation | Reason |
|---|---|---|---|
| Console flows | One console UI module, M17 | M17 provides the menu loop, help frames and a menu router. Each feature registers its own flow unit: `auth_flow`, `admin_flow`, `inventory_flow`, `search_flow`, `booking_flow`, `ticket_flow`, `cancel_flow` and `report_flow`. `main.cpp` registers every flow. | Team members could build features in parallel without editing one shared file |
| Persistent storage | File or database interface | Fixed-length binary record files plus an append-only audit log | Fixed-length records allow in-place updates and simple crash recovery |
| Crash safety | Failed operations leave no partial records | The `atomic_journal` unit commits multi-record writes through a write-ahead journal and recovers an interrupted commit at start-up | Booking and cancellation each change more than one file |
| Technology stack | C/C++17 and standard libraries | C++17 and the standard library, with OpenSSL `libcrypto` for SHA-256 | SRS v2.0 requires a salted iterated SHA-256 digest, and the standard library has no SHA-256 |
| Password digest | Part of M4 | Separate `password_digest` unit used by M4 | Isolates the cryptographic code for review and testing |
| Audit module | M3 AuditModule | The `audit_log` unit implements the append-only log. The `AuditModule` class in `audit.hpp` remains an empty placeholder. | The log functions cover every audit requirement without an extra class |
| Authorization | Role check | A wrapper checks the role, audits a refusal and only then calls the action handler | Every refused administrator action leaves an audit record |
| Session and time | Not specified | `session_context` supplies the session, an ISO 8601 UTC clock and an injectable clock for the idle timeout | Makes timeout and refund-slab rules testable with fixed times |
| Seat inventory record | Seat availability and allocation | The inventory record holds the total, blocked and allocated seat counts. Allocation and release update it and the booking or cancellation record in one journal commit under the lock. | Free seats are derived from one record that the lock protects |
| Refund disbursement | Cancellation and refund processing | A cancellation records the refund as pending. The administrator records the disbursement reference from the booking and cancellation report. | The administrator menu of SRS v2.0 Section 3.1 has no separate disbursement item, so the report screen records it |
| Test data | Not described | The `fixtures` unit holds the known-answer dataset of the Test Plan. The `fixture_generator` unit builds reproducible large datasets for performance tests. | The Test Plan needs fixed known answers and a 2,000-train search dataset |
| Traceability numbering | SAD Section 3.8 uses SRS v1.0 requirement numbers | The code, the tests and the System Validation Report use SRS v2.0 numbers | SRS v2.0 renumbered the requirements after the SAD was issued |

## 4. Test Plan

| Area | Test Plan v1.0 | Delivered | Reason |
|---|---|---|---|
| Execution schedule | Tier tests from 01-10-2026; acceptance demonstration on 02-11-2026 | Tier tests ran from 28-10-2026 to 06-11-2026; usability trial on 12-11-2026; development freeze on 12-11-2026 | Implementation started after the design baseline was approved |
| TC-CONC-01 targets | Run on Linux and Windows | Runs on POSIX targets. The test cases start competing processes with `fork()`, which the Windows toolchain lacks. | The Windows build still compiles and passes every other test case |
| TC-PERF-01 and TC-SIZE-02 | Automated measurement | Measured with `/usr/bin/time` on the seeded trial dataset | No automated harness exists for start-up time and resident memory |
| TC-REL-01 and TC-INT-01 | Dedicated test cases | Partial evidence, as the System Validation Report records | The dedicated test cases were not completed before the freeze |
| TC-UX-01 procedure | Isolated sessions on a fixed task card | Sessions ran in succession in one setting. One participant received freedom to choose the ticket details. | The trial record documents both points. The verdict counts only the four participants who performed the card task. |
| Requirement status | Traceability table issued with every status N | The System Validation Report records the method, evidence and result for all 47 test cases | Records the executed state at the freeze |

## 5. Process

| Area | Course plan | Delivered | Reason |
|---|---|---|---|
| Sprint 1 | 19-10-2026 to 23-10-2026 | 19-10-2026 to 31-10-2026 | The Sprint 1 backlog of build, continuous integration and security items closed on 31-10-2026 |
| Sprint 2 | 26-10-2026 to 30-10-2026 | 01-11-2026 to 07-11-2026, followed by validation until 12-11-2026 | Sprint 2 started when Sprint 1 closed |
| Sprint 1 video | Recorded at the end of Sprint 1 | Recorded on 12-11-2026 from a build of commit `f6135c8`, the last Sprint 1 merge on main | The video shows the product exactly as Sprint 1 left it |
