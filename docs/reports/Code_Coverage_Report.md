# Code Coverage Report

Railway Reservation System, Team 2, UE24CS341A

| Item | Value |
|---|---|
| Code baseline | main branch, commit `dd4b8ae` (development freeze) |
| Execution | GitHub Actions job `coverage`, run on 2026-11-06 ([run log](https://github.com/joelsuhas7-jpg/Railway-Reservation-System-RRS-/actions/runs/42261938156)) |
| Tooling | GCC 13.3.0 with `--coverage`, gcovr 8.3 |
| Scope | Production sources under `src/`, excluding `src/main.cpp` |
| Exclusions | Third-party code under `_deps`; exception-only and unreachable branches |
| Test input | The full automated suite of 245 test cases |

## 1. Summary

Line coverage measures the share of executable source lines that the test suite ran at least once.

| Lines | Covered | Coverage |
|---:|---:|---:|
| 4530 | 3986 | 88 % |

## 2. Coverage by Source File

| Source file | Lines | Covered | Coverage |
|---|---:|---:|---:|
| `src/admin_flow.cpp` | 365 | 266 | 72 % |
| `src/app_info.cpp` | 2 | 2 | 100 % |
| `src/atomic_journal.cpp` | 173 | 151 | 87 % |
| `src/audit_log.cpp` | 44 | 42 | 95 % |
| `src/auth_flow.cpp` | 187 | 172 | 92 % |
| `src/authentication.cpp` | 251 | 244 | 97 % |
| `src/authentication.hpp` | 1 | 1 | 100 % |
| `src/authorization.cpp` | 28 | 25 | 89 % |
| `src/authorization.hpp` | 1 | 1 | 100 % |
| `src/booking.cpp` | 231 | 202 | 87 % |
| `src/booking_flow.cpp` | 178 | 153 | 86 % |
| `src/cancel_flow.cpp` | 89 | 79 | 88 % |
| `src/cancellation_refund.cpp` | 254 | 209 | 82 % |
| `src/cancellation_refund.hpp` | 2 | 2 | 100 % |
| `src/class_fare_config.cpp` | 208 | 189 | 90 % |
| `src/console_input.cpp` | 33 | 24 | 72 % |
| `src/console_ui.cpp` | 137 | 135 | 98 % |
| `src/fare_calculation.cpp` | 30 | 25 | 83 % |
| `src/fare_calculation.hpp` | 1 | 1 | 100 % |
| `src/fare_config.cpp` | 324 | 303 | 93 % |
| `src/fixture_generator.cpp` | 381 | 337 | 88 % |
| `src/fixture_generator.hpp` | 7 | 7 | 100 % |
| `src/fixtures.cpp` | 151 | 141 | 93 % |
| `src/inventory_flow.cpp` | 48 | 37 | 77 % |
| `src/passenger.cpp` | 28 | 28 | 100 % |
| `src/password_digest.cpp` | 48 | 41 | 85 % |
| `src/payment.cpp` | 6 | 6 | 100 % |
| `src/persistence.cpp` | 180 | 154 | 85 % |
| `src/platform_shim.cpp` | 57 | 38 | 66 % |
| `src/report_flow.cpp` | 100 | 84 | 84 % |
| `src/reporting.cpp` | 56 | 50 | 89 % |
| `src/search.cpp` | 176 | 149 | 84 % |
| `src/search.hpp` | 1 | 1 | 100 % |
| `src/search_flow.cpp` | 108 | 92 | 85 % |
| `src/seat_inventory.cpp` | 244 | 216 | 88 % |
| `src/session_context.cpp` | 15 | 15 | 100 % |
| `src/session_context.hpp` | 3 | 3 | 100 % |
| `src/ticket_flow.cpp` | 21 | 19 | 90 % |
| `src/ticket_retrieval.cpp` | 18 | 16 | 88 % |
| `src/train_catalogue.cpp` | 297 | 280 | 94 % |
| `src/validation.cpp` | 45 | 45 | 100 % |
| `src/validation.hpp` | 1 | 1 | 100 % |

## 3. Detailed Reports

The `coverage/` folder beside this report holds the gcovr output of the same run. `coverage/index.html` opens the annotated source listing for every file. `coverage/lines.txt` lists the uncovered lines by line number. `coverage/coverage.xml` holds the Cobertura export.
