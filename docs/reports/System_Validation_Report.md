# System Validation Report

Railway Reservation System, Team 2, UE24CS341A

| Item | Value |
|---|---|
| Code baseline | main branch, commit `dd4b8ae` (development freeze) |
| Reference documents | Software Requirements Specification v2.0; Software Test Plan v1.0 |
| Automated evidence | GitHub Actions run of 2026-11-06 on `dd4b8ae` ([run log](https://github.com/joelsuhas7-jpg/Railway-Reservation-System-RRS-/actions/runs/42261938156)) |
| Manual evidence | Usability trial TC-UX-01 of 2026-11-12; measurements of 2026-11-12 |

## 1. Purpose

This report validates the delivered system against every requirement in the traceability table of the Software Test Plan. It records the method that verified each test case, the evidence and the result. It replaces the status column of the Test Plan table, which the plan issued with every requirement marked N (not yet executed).

## 2. Validation Methods

| Method | Description |
|---|---|
| Automated | Catch2 test cases executed by CTest in continuous integration |
| CI check | A build step that fails the pipeline when a measured limit is exceeded |
| Measurement | A timed or metered run of the release binary on the facilitator workstation |
| Manual trial | A facilitated session with first-time users under the Test Plan procedure |

## 3. Results by Requirement

| Requirement | Description | Test case | Method | Evidence | Result |
|---|---|---|---|---|---|
| RRS-F-001 | Passenger registration | TC-REG-01 | Automated | 6 cases in `test_authentication.cpp`, `test_registration_flow.cpp` | Pass |
| RRS-F-002 | Authenticate user, bind role to session | TC-AUTH-01 | Automated | 5 cases in `test_authentication.cpp`, `test_login_flow.cpp` | Pass |
| RRS-F-003 | Logout and clear session | TC-AUTH-02 | Automated | 2 cases in `test_login_flow.cpp` | Pass |
| RRS-F-004 | First-run administrator bootstrap | TC-AUTH-03 | Automated | 6 cases in `test_authentication.cpp`, `test_registration_flow.cpp` | Pass |
| RRS-F-005 | Password reset and change | TC-AUTH-04 | Automated | 6 cases in `test_authentication.cpp`, `test_password_flow.cpp` | Pass |
| RRS-F-006 | Maintain train and schedule records | TC-ADMIN-01 | Automated | 22 cases in `test_train_catalogue.cpp`, `test_train_catalogue_flow.cpp` | Pass |
| RRS-F-007 | Define travel classes and capacity | TC-CLASS-01 | Automated | 17 cases in `test_class_fare_config.cpp`, `test_class_fare_config_flow.cpp` | Pass |
| RRS-F-008 | Configure per-class fare a priori | TC-FARE-01 | Automated | 29 cases in `test_fare_config.cpp`, `test_fare_config_concurrency.cpp`, `test_fare_config_flow.cpp` | Pass |
| RRS-F-009 | Initialise and adjust seat inventory | TC-SEAT-01 | Automated | 11 cases in `test_search_flow.cpp`, `test_seat_inventory.cpp`, `test_seat_inventory_flow.cpp` | Pass |
| RRS-F-010 | Search by source, destination, date | TC-SEARCH-01 | Automated | 10 cases in `test_fixtures.cpp`, `test_search.cpp`, `test_search_catalogue_flow.cpp`, `test_search_flow.cpp` | Pass |
| RRS-F-011 | Display train schedule detail | TC-SEARCH-02 | Automated | Train detail view driven through the passenger menu in `test_search_flow.cpp` (TC-SEAT-01 case, train detail shown) | Pass |
| RRS-F-012 | Display per-class fare and availability | TC-SEAT-02 | Automated | Fare and free seats per class shown by search in `test_search_flow.cpp` (2 cases) | Pass |
| RRS-F-013 | Collect and validate 1–6 passengers | TC-BOOK-01 | Automated | 11 cases in `test_booking_flow.cpp`, `test_passenger.cpp` | Pass |
| RRS-F-014 | Displayed fare from configured fare | TC-FARE-02 | Automated | 4 cases in `test_booking_flow.cpp`, `test_fare_calculation.cpp` | Pass |
| RRS-F-015 | Record payment outcome before confirm | TC-PAY-01 | Automated | 5 cases in `test_booking_flow.cpp`, `test_payment.cpp` | Pass |
| RRS-F-016 | Atomic seat allocation under lock | TC-CONC-01 (blocking) | Automated | 2 cases in `test_booking_concurrency.cpp` | Pass |
| RRS-F-017 | Generate unique PNR | TC-BOOK-02 | Automated | 2 cases in `test_booking.cpp` | Pass |
| RRS-F-018 | Retrieve booking by PNR | TC-VIEW-01 | Automated | 4 cases in `test_ticket_flow.cpp` | Pass |
| RRS-F-019 | Cancel eligible booking per slab table | TC-CAN-01 | Automated | 4 cases in `test_cancel_flow.cpp`, `test_cancellation_refund.cpp` | Pass |
| RRS-F-020 | Compute refund and release seats | TC-CAN-02 | Automated | 2 cases in `test_cancel_flow.cpp`, `test_cancellation_refund.cpp` | Pass |
| RRS-F-021 | Record refund disbursement | TC-CAN-03 | Automated | 2 cases in `test_cancel_flow.cpp`, `test_cancellation_refund.cpp` | Pass |
| RRS-F-022 | Booking and cancellation reporting | TC-REPORT-01 | Automated | 4 cases in `test_report_flow.cpp` | Pass |
| RRS-F-023 | Audit logging of nine action types | TC-AUDIT-01 | Automated | 1 case in `test_audit_log.cpp` | Pass |
| RRS-NF-SPD-001 | Start-up within 2 s | TC-PERF-01 | Measurement | Start-up and exit below 0.01 s against the 2 s limit (Section 4.2) | Pass |
| RRS-NF-SPD-002 | Search within 200 ms | TC-PERF-02 | Automated | 2 cases in `test_fixtures.cpp`, `test_search.cpp` | Pass |
| RRS-NF-SPD-003 | Critical section within 100 ms | TC-PERF-03 | Automated | 2 cases in `test_booking_concurrency.cpp` | Pass |
| RRS-NF-SPD-004 | Menu redisplay within 50 ms | TC-PERF-04 | Automated | 1 case in `test_console_usability.cpp` | Pass |
| RRS-NF-SIZ-001 | Binary image ≤ 10 MB | TC-SIZE-01 | CI check | Stripped release binary 293,376 bytes on Linux and 317,976 bytes on Windows against the 10 MB limit | Pass |
| RRS-NF-SIZ-002 | Resident memory ≤ 64 MB | TC-SIZE-02 | Measurement | Peak resident memory 2.0 MB against the 64 MB limit (Section 4.2) | Pass |
| RRS-NF-EOU-001 | Unaided task within 10 minutes | TC-UX-01 | Manual trial | 4 of 5 first-time users completed the task unaided within 10 minutes (Section 4.1) | Pass |
| RRS-NF-EOU-002 | Help frame per menu, depth ≤ 4 | TC-UX-02 | Automated | 5 cases in `test_console_usability.cpp`, `test_login_flow.cpp` | Pass |
| RRS-NF-REL-001 | 10,000-operation soak without failure | TC-REL-01 | Automated | 10,000-operation soak in `test_reliability_soak.cpp`; the case asserts the TC-REL-02 failure rate and does not assert a zero count separately | Partial |
| RRS-NF-REL-002 | System-failure rate ≤ 0.1 per cent | TC-REL-02 | Automated | 1 case in `test_reliability_soak.cpp` | Pass |
| RRS-NF-REL-003 | Cross-record consistency | TC-INT-01 (blocking) | Automated | Inventory and booking records checked for agreement after concurrent booking (TC-CONC-01, 2 cases) and fixture consistency checker (ENB-06-06); no standalone cross-record checker run | Partial |
| RRS-NF-ROB-001 | Recover or restart within 5 s | TC-ROB-01 | Automated | 2 cases in `test_atomic_journal_crash.cpp`, `test_console_ui.cpp` | Pass |
| RRS-NF-ROB-002 | Zero terminations from bad input | TC-ROB-02 | Automated | 1 case in `test_validation.cpp` | Pass |
| RRS-NF-ROB-003 | No partial write on abnormal exit | TC-ROB-03 | Automated | 10 cases in `test_atomic_journal.cpp`, `test_persistence.cpp` | Pass |
| RRS-NF-POR-001 | Target-dependent statements ≤ 2 per cent | TC-BUILD-01 | CI check | CMake platform-shim check: 38 of 3,321 production statements (1 per cent) against the 2 per cent limit | Pass |
| RRS-NF-POR-002 | Builds and passes on 2 targets | TC-BUILD-02 | CI check | `build-and-test` (Linux, 245 cases) and `windows-mingw` (Windows, 242 cases) both pass | Pass |
| RRS-NF-MNT-001 | No back edges in include graph | TC-BUILD-03 | CI check | `check_include_graph.py`: 35 headers, 96 edges, no back edges | Pass |
| RRS-SR-001 | Authentication before protected access | TC-SEC-01 | Automated | Role menu tests ENB-04-02 and ENB-04-03 and the TC-SEC-02 authorization cases (7 cases) | Pass |
| RRS-SR-002 | Role-based access to master data | TC-SEC-02 | Automated | 4 cases in `test_authentication.cpp`, `test_authorization.cpp` | Pass |
| RRS-SR-003 | Validation before persistence | TC-SEC-03 | Automated | Validation cases RRS-SR-003-01 to RRS-SR-003-05 (5 cases) | Pass |
| RRS-SR-004 | Salted iterated password digest | TC-SEC-04 | Automated | 5 cases in `test_authentication.cpp`, `test_password_digest.cpp` | Pass |
| RRS-SR-005 | Audit of security-relevant actions | TC-SEC-05 | Automated | 5 cases in `test_login_flow.cpp`, `test_registration_flow.cpp` | Pass |
| RRS-SR-006 | Password policy and account lockout | TC-SEC-06 | Automated | 3 cases in `test_authentication.cpp` | Pass |
| RRS-SR-007 | Session idle timeout | TC-SEC-07 | Automated | 2 cases in `test_login_flow.cpp` | Pass |

Summary: 45 of 47 test cases pass in full. 2 test cases pass in part. No test case fails.

## 4. Validations Outside the Automated Suite

### 4.1 Usability Trial TC-UX-01

Five participants with no earlier exposure to the application received a printed task card. The task was to book Sleeper class tickets for two passengers on train 12627 from SBC to NDLS. Each session started on a fresh copy of the same seeded data.

| Participant | Duration (m:ss) | Booking saved | Completed |
|---|---|---|---|
| P1 | 3:29.70 | 12627 SL, 2 passengers, Rs 900.00 | Yes |
| P2 | 2:00.20 | 12627 SL, 2 passengers, Rs 900.00 | Yes |
| P3 | 1:44.40 | 12627 SL, 2 passengers, Rs 900.00 | Yes |
| P4 | 1:25.07 | 12627 SL, 2 passengers, Rs 900.00 | Yes |
| P5 | 1:19.34 | 12649 2A, 2 passengers, Rs 3700.00 | No |

Four of five participants completed the task unaided within 10 minutes, which meets the acceptance criterion of RRS-NF-EOU-001. P5 received freedom to choose the ticket details and performed a different task. The sessions ran in succession in one setting, so later participants may have observed earlier sessions. P1 had no earlier session to observe and completed the task in 3 minutes 29.7 seconds. The full trial record is `Team_2_RRS_Usability_Trial_TC-UX-01.docx` in this folder.

### 4.2 Start-up Time and Memory

The release binary ran three times on the seeded trial dataset of three trains and five travel classes, from launch to the Exit option. Each run completed in less than 0.01 s and reached a peak resident set of 2.0 MB. The measurement used macOS `/usr/bin/time -l` on the facilitator workstation. A run against the 2,000-train fixture of TC-PERF-02 was not performed for these two cases.

### 4.3 Build, Size and Structure Checks

The continuous integration pipeline enforces TC-BUILD-01, TC-BUILD-02, TC-BUILD-03 and TC-SIZE-01 on every pull request. A breach of any limit fails the pipeline and blocks the merge. The values in Section 3 come from the run on the freeze commit.

## 5. Use-Case Validation

The demonstration videos in `docs/videos/` show the delivered behaviour end to end. The Sprint 2 video shows a passenger who logs in, searches the SBC to NDLS route, books a ticket, views it by PNR, attempts an invalid password change and cancels the booking with a refund under slab S1. The integration tests in `tests/integration/` drive the same use cases through the console for both roles.

## 6. Limitations

- TC-CONC-01 runs on POSIX targets only. Its test cases start competing processes with `fork()`, which the Windows toolchain does not provide. The Windows build passes every other case.
- TC-REL-01 and TC-INT-01 pass in part, as Section 3 records.
- TC-PERF-01 and TC-SIZE-02 were measured on the trial dataset and not on the 2,000-train fixture.

## 7. Conclusion

The system meets 45 of the 47 validation test cases in full and the remaining 2 in part. Both blocking test cases have automated evidence: TC-CONC-01 passes in full and TC-INT-01 passes in part. The usability trial meets its acceptance criterion.
