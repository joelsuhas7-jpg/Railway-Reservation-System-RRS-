# Branch Coverage Report

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

Branch coverage measures the share of decision outcomes that the test suite took at least once. Each `if`, loop condition, `switch` case and short-circuit operand contributes one outcome per direction.

| Branches | Covered | Coverage |
|---:|---:|---:|
| 6286 | 4384 | 69 % |

## 2. Coverage by Source File

| Source file | Branches | Covered | Coverage |
|---|---:|---:|---:|
| `src/admin_flow.cpp` | 648 | 371 | 57 % |
| `src/app_info.cpp` | 0 | 0 | n/a |
| `src/atomic_journal.cpp` | 215 | 147 | 68 % |
| `src/audit_log.cpp` | 49 | 44 | 89 % |
| `src/auth_flow.cpp` | 201 | 161 | 80 % |
| `src/authentication.cpp` | 232 | 216 | 93 % |
| `src/authentication.hpp` | 0 | 0 | n/a |
| `src/authorization.cpp` | 33 | 22 | 66 % |
| `src/authorization.hpp` | 0 | 0 | n/a |
| `src/booking.cpp` | 285 | 198 | 69 % |
| `src/booking_flow.cpp` | 308 | 169 | 54 % |
| `src/cancel_flow.cpp` | 160 | 107 | 66 % |
| `src/cancellation_refund.cpp` | 316 | 207 | 65 % |
| `src/cancellation_refund.hpp` | 2 | 2 | 100 % |
| `src/class_fare_config.cpp` | 287 | 209 | 72 % |
| `src/console_input.cpp` | 55 | 35 | 63 % |
| `src/console_ui.cpp` | 129 | 115 | 89 % |
| `src/fare_calculation.cpp` | 36 | 23 | 63 % |
| `src/fare_calculation.hpp` | 0 | 0 | n/a |
| `src/fare_config.cpp` | 438 | 357 | 81 % |
| `src/fixture_generator.cpp` | 612 | 425 | 69 % |
| `src/fixture_generator.hpp` | 6 | 4 | 66 % |
| `src/fixtures.cpp` | 163 | 117 | 71 % |
| `src/inventory_flow.cpp` | 85 | 38 | 44 % |
| `src/passenger.cpp` | 26 | 26 | 100 % |
| `src/password_digest.cpp` | 57 | 36 | 63 % |
| `src/payment.cpp` | 9 | 9 | 100 % |
| `src/persistence.cpp` | 263 | 159 | 60 % |
| `src/platform_shim.cpp` | 28 | 8 | 28 % |
| `src/report_flow.cpp` | 170 | 112 | 65 % |
| `src/reporting.cpp` | 87 | 76 | 87 % |
| `src/search.cpp` | 244 | 191 | 78 % |
| `src/search.hpp` | 0 | 0 | n/a |
| `src/search_flow.cpp` | 193 | 83 | 43 % |
| `src/seat_inventory.cpp` | 369 | 245 | 66 % |
| `src/session_context.cpp` | 14 | 14 | 100 % |
| `src/session_context.hpp` | 0 | 0 | n/a |
| `src/ticket_flow.cpp` | 52 | 22 | 42 % |
| `src/ticket_retrieval.cpp` | 26 | 21 | 80 % |
| `src/train_catalogue.cpp` | 431 | 358 | 83 % |
| `src/validation.cpp` | 57 | 57 | 100 % |
| `src/validation.hpp` | 0 | 0 | n/a |

## 3. Detailed Reports

The `coverage/` folder beside this report holds the gcovr output of the same run. `coverage/index.html` opens the annotated source listing for every file. `coverage/branches.txt` lists the uncovered branches by line number. `coverage/coverage.xml` holds the Cobertura export.

## 4. Analysis

Branch coverage is lower than line coverage because the console flow files contain one branch per invalid-input path and per outcome kind. The modules that hold the business rules range from 63 to 100 per cent. The following files are below 60 per cent:

| Source file | Coverage |
|---|---:|
| `src/platform_shim.cpp` | 28 % |
| `src/ticket_flow.cpp` | 42 % |
| `src/search_flow.cpp` | 43 % |
| `src/inventory_flow.cpp` | 44 % |
| `src/booking_flow.cpp` | 54 % |
| `src/admin_flow.cpp` | 57 % |

Five of the six files are console flow files. Their uncovered branches are mainly re-prompts after malformed console input and outcome kinds that the integration tests do not drive through the console. The validation module reaches 100 per cent branch coverage, and the robustness test TC-ROB-02 exercises the malformed-input rules at module level. The uncovered branches in `src/platform_shim.cpp` are operating-system error paths, such as an interrupted lock call or a failed `fsync`, which require a failing file system.
