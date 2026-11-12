# Unit Test Report

Railway Reservation System, Team 2, UE24CS341A

| Item | Value |
|---|---|
| Code baseline | main branch, commit `dd4b8ae` (development freeze) |
| Execution | GitHub Actions job `build-and-test`, run on 2026-11-06 ([run log](https://github.com/joelsuhas7-jpg/Railway-Reservation-System-RRS-/actions/runs/42261938156)) |
| Platform | Ubuntu 24.04, GCC 13.3.0, CMake Release build with `-Wall -Wextra -Werror` |
| Framework | Catch2 v3.5.4, registered with CTest through `catch_discover_tests` |

## 1. Summary

| Measure | Result |
|---|---|
| Test cases executed | 245 |
| Passed | 245 |
| Failed | 0 |
| Unit test cases (`tests/unit`) | 176 in 23 files |
| Integration test cases (`tests/integration`) | 69 in 18 files |
| Total execution time | 62.2 s |

Every test case passed. The same suite passed in the `sanitizers` job under AddressSanitizer and UndefinedBehaviorSanitizer, in the `coverage` job and inside the Docker build stage. The `windows-mingw` job executed 242 test cases and all passed. The three remaining cases use POSIX `fork()` to start competing processes and are compiled only on POSIX targets.

## 2. Results by Test File

| Test file | Module or flow | Cases | Passed | Time (s) |
|---|---|---:|---:|---:|
| `tests/integration/test_atomic_journal_crash.cpp` | atomic_journal_crash | 1 | 1 | 0.32 |
| `tests/integration/test_booking_concurrency.cpp` | booking_concurrency | 4 | 4 | 0.44 |
| `tests/integration/test_booking_flow.cpp` | booking_flow | 6 | 6 | 0.13 |
| `tests/integration/test_cancel_flow.cpp` | cancel_flow | 6 | 6 | 0.15 |
| `tests/integration/test_class_fare_config_flow.cpp` | class_fare_config_flow | 3 | 3 | 0.03 |
| `tests/integration/test_fare_config_concurrency.cpp` | fare_config_concurrency | 3 | 3 | 0.03 |
| `tests/integration/test_fare_config_flow.cpp` | fare_config_flow | 4 | 4 | 0.06 |
| `tests/integration/test_fixture_generator_flow.cpp` | fixture_generator_flow | 2 | 2 | 0.04 |
| `tests/integration/test_login_flow.cpp` | login_flow | 9 | 9 | 0.09 |
| `tests/integration/test_password_flow.cpp` | password_flow | 3 | 3 | 0.05 |
| `tests/integration/test_registration_flow.cpp` | registration_flow | 6 | 6 | 0.05 |
| `tests/integration/test_reliability_soak.cpp` | reliability_soak | 1 | 1 | 1.20 |
| `tests/integration/test_report_flow.cpp` | report_flow | 4 | 4 | 0.11 |
| `tests/integration/test_search_catalogue_flow.cpp` | search_catalogue_flow | 1 | 1 | 0.01 |
| `tests/integration/test_search_flow.cpp` | search_flow | 4 | 4 | 0.08 |
| `tests/integration/test_seat_inventory_flow.cpp` | seat_inventory_flow | 1 | 1 | 0.01 |
| `tests/integration/test_ticket_flow.cpp` | ticket_flow | 4 | 4 | 0.09 |
| `tests/integration/test_train_catalogue_flow.cpp` | train_catalogue_flow | 7 | 7 | 0.05 |
| `tests/unit/test_app_info.cpp` | app_info | 2 | 2 | 0.00 |
| `tests/unit/test_atomic_journal.cpp` | atomic_journal | 3 | 3 | 0.01 |
| `tests/unit/test_audit_log.cpp` | audit_log | 6 | 6 | 0.00 |
| `tests/unit/test_authentication.cpp` | authentication | 27 | 27 | 0.30 |
| `tests/unit/test_authorization.cpp` | authorization | 3 | 3 | 0.00 |
| `tests/unit/test_booking.cpp` | booking | 5 | 5 | 0.01 |
| `tests/unit/test_cancellation_refund.cpp` | cancellation_refund | 4 | 4 | 0.00 |
| `tests/unit/test_class_fare_config.cpp` | class_fare_config | 14 | 14 | 0.09 |
| `tests/unit/test_console_ui.cpp` | console_ui | 8 | 8 | 0.00 |
| `tests/unit/test_console_usability.cpp` | console_usability | 5 | 5 | 0.01 |
| `tests/unit/test_fare_calculation.cpp` | fare_calculation | 2 | 2 | 0.00 |
| `tests/unit/test_fare_config.cpp` | fare_config | 22 | 22 | 0.22 |
| `tests/unit/test_fixture_generator.cpp` | fixture_generator | 3 | 3 | 0.02 |
| `tests/unit/test_fixtures.cpp` | fixtures | 8 | 8 | 0.00 |
| `tests/unit/test_menu_router.cpp` | menu_router | 5 | 5 | 0.00 |
| `tests/unit/test_passenger.cpp` | passenger | 9 | 9 | 0.00 |
| `tests/unit/test_password_digest.cpp` | password_digest | 3 | 3 | 0.04 |
| `tests/unit/test_payment.cpp` | payment | 4 | 4 | 0.00 |
| `tests/unit/test_persistence.cpp` | persistence | 7 | 7 | 0.00 |
| `tests/unit/test_search.cpp` | search | 7 | 7 | 58.40 |
| `tests/unit/test_seat_inventory.cpp` | seat_inventory | 8 | 8 | 0.07 |
| `tests/unit/test_train_catalogue.cpp` | train_catalogue | 15 | 15 | 0.07 |
| `tests/unit/test_validation.cpp` | validation | 6 | 6 | 0.00 |

## 3. Test Case Listing

Test case names begin with the Test Plan case identifier (TC-), the requirement identifier (RRS-) or the enabling backlog item (ENB-, UC-, SMOKE-) that the case verifies.

### `tests/integration/test_atomic_journal_crash.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-ROB-01: 100 SIGKILLs recover every multi-file transaction within five seconds | Passed | 0.32 |

### `tests/integration/test_booking_concurrency.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-CONC-01: two processes racing for the last seat confirm exactly one booking | Passed | 0.11 |
| TC-CONC-01: four processes never oversell a class beyond its free seats | Passed | 0.02 |
| TC-PERF-03: single-instance booking critical section p95 is within 100 ms | Passed | 0.09 |
| TC-PERF-03: booking latency is reported under 2 and 4 instance contention | Passed | 0.22 |

### `tests/integration/test_booking_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-FARE-02: four SL passengers pay 180000 paise and hold four seats | Passed | 0.03 |
| TC-FARE-02: a class without a configured fare is refused before any passenger entry | Passed | 0.03 |
| TC-BOOK-01: counts and ages out of range are refused naming the field | Passed | 0.02 |
| TC-BOOK-01: the booking module refuses a seven-passenger block | Passed | 0.01 |
| TC-PAY-01: a failed payment leaves no booking, no PNR and no seat allocation | Passed | 0.02 |
| RRS-F-016: the check inside the lock refuses a booking the free seats cannot cover | Passed | 0.02 |

### `tests/integration/test_cancel_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-CAN-01: a booking cancelled 50 hours before departure succeeds once | Passed | 0.02 |
| TC-CAN-01: a cancellation 3 hours before departure is refused citing slab S4 | Passed | 0.02 |
| TC-CAN-02: 180000 paise cancelled in slab S2 refunds 90000 and frees four seats | Passed | 0.02 |
| TC-CAN-03: a refund stays PENDING until its disbursement reference is recorded | Passed | 0.03 |
| UC-02 B6: declining after seeing the refund leaves the booking unchanged | Passed | 0.02 |
| UC-02 B2: another passenger cannot cancel; an administrator can | Passed | 0.04 |

### `tests/integration/test_class_fare_config_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-CLASS-01: administrator adds, updates and enquires class capacity | Passed | 0.01 |
| TC-CLASS-01: ManageClasses timeout prevents mutation and returns to public login | Passed | 0.01 |
| TC-CLASS-01: ManageClasses rejects invalid choices and handles end of input | Passed | 0.01 |

### `tests/integration/test_fare_config_concurrency.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-FARE-01: separate processes preserve distinct simultaneous fare appends | Passed | 0.01 |
| TC-FARE-01: separate processes serialize duplicate fare appends | Passed | 0.01 |
| TC-FARE-01: stale concurrent replacement refuses without changing fare history | Passed | 0.01 |

### `tests/integration/test_fare_config_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-FARE-01: authenticated administrator configures and lists 45000 paise | Passed | 0.01 |
| TC-FARE-01: console explicitly selects which future fare to replace | Passed | 0.01 |
| TC-FARE-01: ManageFares handles invalid choices, back and end of input | Passed | 0.03 |
| TC-FARE-01: expired ManageFares session cannot persist a fare and requires relogin | Passed | 0.01 |

### `tests/integration/test_fixture_generator_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| ENB-06-05: failed materialization never returns a verified handle | Passed | 0.00 |
| ENB-06-06: small multi-class fixtures materialize and checker detects corruption | Passed | 0.04 |

### `tests/integration/test_login_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-AUTH-01: login opens the menu tree for the account's role | Passed | 0.01 |
| TC-SEC-05: failed logins are refused uniformly and append audit fields | Passed | 0.01 |
| TC-SEC-05: successful login appends action timestamp actor and role | Passed | 0.01 |
| TC-SEC-05: refused administrator access appends action timestamp actor and role | Passed | 0.00 |
| TC-AUTH-02: after logout the public menu returns and re-login is required | Passed | 0.02 |
| TC-AUTH-02: end of input inside a session ends it without error | Passed | 0.01 |
| TC-SEC-07: 15 idle minutes end the session with one audit entry | Passed | 0.01 |
| TC-SEC-07: activity within 15 minutes keeps the session open | Passed | 0.01 |
| TC-UX-02: ? at a menu shows its help frame and the session carries on | Passed | 0.01 |

### `tests/integration/test_password_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-AUTH-04: change, then reset, through the menus; each is audited | Passed | 0.03 |
| TC-AUTH-04: refused changes are not audited and keep the old password | Passed | 0.02 |
| RRS-F-005-01: audit timestamps are ISO 8601 UTC | Passed | 0.00 |

### `tests/integration/test_registration_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-AUTH-03: first-run console setup retries until one administrator exists | Passed | 0.01 |
| TC-AUTH-03: setup ends cleanly when input ends | Passed | 0.00 |
| TC-SEC-05: first administrator creation is audited; rejected bootstrap is not | Passed | 0.01 |
| TC-REG-01: register from the public menu, then reject the duplicate | Passed | 0.01 |
| TC-SEC-05: registration success is audited and duplicate registration is not | Passed | 0.01 |
| TC-REG-01: input ending mid-form registers nothing | Passed | 0.01 |

### `tests/integration/test_reliability_soak.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-REL-02: system failures stay within 0.1 per cent over 10000 operations | Passed | 1.20 |

### `tests/integration/test_report_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-REPORT-01: totals are exact for each filter | Passed | 0.04 |
| TC-REPORT-01: a passenger cannot invoke the report | Passed | 0.02 |
| TC-REPORT-01: malformed and reversed date ranges are refused naming the field | Passed | 0.02 |
| TC-REPORT-01: the administrator console shows totals and settles a pending refund | Passed | 0.03 |

### `tests/integration/test_search_catalogue_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEARCH-01: searches observe M6 additions and removals immediately | Passed | 0.01 |

### `tests/integration/test_search_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEARCH-01: the passenger menu searches and shows fare and free seats per class | Passed | 0.02 |
| TC-SEARCH-01: an unserved route and invalid input are explained | Passed | 0.02 |
| TC-SEAT-01: an administrator blocks seats and the next search shows the change | Passed | 0.02 |
| TC-SEAT-01: blocking more seats than are free is refused without a change | Passed | 0.02 |

### `tests/integration/test_seat_inventory_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEAT-01: configured train class initializes, blocks and survives module reopen | Passed | 0.01 |

### `tests/integration/test_ticket_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-VIEW-01: the owner sees every field of the ticket | Passed | 0.02 |
| TC-VIEW-01: another passenger's PNR is refused as no such booking and audited | Passed | 0.03 |
| TC-VIEW-01: unknown and malformed PNRs give the same answer | Passed | 0.02 |
| TC-VIEW-01: an administrator may retrieve any passenger's ticket | Passed | 0.02 |

### `tests/integration/test_train_catalogue_flow.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-ADMIN-01: authorized train lifecycle is durable and audited once per change | Passed | 0.01 |
| TC-ADMIN-01: passenger train mutations are denied and audited by M5 | Passed | 0.00 |
| TC-ADMIN-01: removal preserves opaque booking and dependent master records | Passed | 0.00 |
| TC-ADMIN-01: authenticated administrator adds a train from ManageTrains | Passed | 0.01 |
| TC-ADMIN-01: ManageTrains handles end of input cleanly | Passed | 0.01 |
| TC-ADMIN-01: session expires during train details and requires a new login | Passed | 0.01 |
| TC-ADMIN-01: active session completes multiple train operations | Passed | 0.01 |

### `tests/unit/test_app_info.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| SMOKE-01: application name matches the SRS title | Passed | 0.00 |
| SMOKE-02: application version is non-empty and dotted | Passed | 0.00 |

### `tests/unit/test_atomic_journal.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-ROB-03-UNIT-01: journal commits all records across files | Passed | 0.01 |
| TC-ROB-03-UNIT-02: recovery replays an interrupted transaction | Passed | 0.00 |
| TC-ROB-03-UNIT-03: invalid transactions are rejected before journaling | Passed | 0.00 |

### `tests/unit/test_audit_log.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-AUDIT-01: one action appends exactly one five-field entry | Passed | 0.00 |
| TC-AUDIT-02: multiple actions append entries without changing earlier entries | Passed | 0.00 |
| TC-AUDIT-03: all five audit fields are mandatory | Passed | 0.00 |
| TC-AUDIT-04: fields containing tabs or line breaks are rejected | Passed | 0.00 |
| TC-AUDIT-05: an existing audit log is preserved when a new entry is appended | Passed | 0.00 |
| TC-AUDIT-06: an invalid audit log path reports a system error | Passed | 0.00 |

### `tests/unit/test_authentication.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-AUTH-03: empty data directory requires the administrator bootstrap | Passed | 0.00 |
| TC-AUTH-03: bootstrap creates exactly one ADMIN account and closes | Passed | 0.01 |
| TC-AUTH-03: a second bootstrap is refused, also after reopening the store | Passed | 0.01 |
| TC-AUTH-03: invalid bootstrap input names the field and keeps the path open | Passed | 0.00 |
| TC-SEC-04: users.dat never contains the plaintext password | Passed | 0.01 |
| TC-SEC-06: password policy needs 8 characters, a letter and a digit | Passed | 0.00 |
| RRS-F-004-01: user id length and character boundaries | Passed | 0.00 |
| RRS-F-004-02: contact length boundaries | Passed | 0.00 |
| RRS-F-004-03: user record round-trips and rejects corruption | Passed | 0.01 |
| RRS-F-004-04: user lookup ignores letter case | Passed | 0.01 |
| TC-REG-01: valid details create exactly one PASSENGER account | Passed | 0.01 |
| TC-REG-01: a duplicate user id is rejected naming the field | Passed | 0.01 |
| TC-REG-01: registration is refused before the administrator bootstrap | Passed | 0.00 |
| TC-REG-01: invalid registration input names the field and creates nothing | Passed | 0.01 |
| TC-SEC-04: two accounts with the same password get distinct salts | Passed | 0.01 |
| TC-AUTH-01: correct credentials open a session bound to the account's role | Passed | 0.01 |
| TC-AUTH-01: wrong password and unknown id fail with the same message | Passed | 0.02 |
| TC-AUTH-01: failures count up and a successful login resets the counter | Passed | 0.02 |
| TC-SEC-06: fifth failure locks and the lock expires at 15 minutes | Passed | 0.02 |
| TC-SEC-06: successful login resets consecutive failures | Passed | 0.02 |
| TC-AUTH-01: an unknown id costs as much digest work as a real account | Passed | 0.02 |
| RRS-F-002-01: update rewrites the matching record in place | Passed | 0.01 |
| TC-AUTH-04: change password writes a new salt and digest; the old one stops working | Passed | 0.02 |
| TC-AUTH-04: change password refuses a wrong current password or a weak new one | Passed | 0.02 |
| TC-AUTH-04: administrator resets a passenger password and clears the lock | Passed | 0.03 |
| TC-SEC-02: password reset is refused to a passenger session even when called directly | Passed | 0.01 |
| TC-AUTH-04: reset refuses unknown, administrator and weak targets | Passed | 0.01 |

### `tests/unit/test_authorization.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEC-02: passenger sessions are refused and audited at admin operation boundary | Passed | 0.00 |
| TC-SEC-02: administrator sessions pass without creating denial audit entries | Passed | 0.00 |
| TC-SEC-02: invalid operation names are rejected before authorization | Passed | 0.00 |

### `tests/unit/test_booking.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| ENB-07: only the SRS 7.3 booking transitions are legal | Passed | 0.00 |
| ENB-07: a booking record survives the fixed-length encoding | Passed | 0.00 |
| ENB-07: oversized fields and corrupt records are refused | Passed | 0.00 |
| TC-BOOK-02: generated PNRs are ten alphanumeric characters and distinct | Passed | 0.01 |
| TC-BOOK-02: an injected collision is detected and a fresh PNR issued | Passed | 0.00 |

### `tests/unit/test_cancellation_refund.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-CAN-01: refund slabs follow Table 4.5-1 at each boundary | Passed | 0.00 |
| TC-CAN-02: the refund is the slab percentage of the fare in truncated paise | Passed | 0.00 |
| TC-CAN-01: the interval to departure is counted across month and year ends | Passed | 0.00 |
| TC-CAN-03: a cancellation record survives the encoding and reports its status | Passed | 0.00 |

### `tests/unit/test_class_fare_config.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-CLASS-01: all supported classes persist and known capacities report exactly | Passed | 0.01 |
| TC-CLASS-01: every supported class code is accepted once per train | Passed | 0.01 |
| TC-CLASS-01: unsupported codes and non-positive capacities are rejected | Passed | 0.01 |
| TC-CLASS-01: duplicate class add is refused and capacity can only increase | Passed | 0.01 |
| TC-CLASS-01: unknown and soft-removed trains cannot receive class changes | Passed | 0.01 |
| TC-CLASS-01: concurrent class adds through separate module instances stay indexed | Passed | 0.01 |
| TC-CLASS-01: query distinguishes unknown trains from empty class lists | Passed | 0.01 |
| TC-CLASS-01: class records use the exact 16-byte little-endian layout | Passed | 0.00 |
| TC-CLASS-01: malformed, partial, unsupported and duplicate records are rejected | Passed | 0.01 |
| TC-CLASS-01: passenger mutation is denied and audited exactly once | Passed | 0.00 |
| TC-CLASS-01: authorization precedes invalid operation timestamps | Passed | 0.00 |
| TC-CLASS-01: persisted class survives module recreation | Passed | 0.00 |
| TC-CLASS-01: audit append failure reports the persisted class mutation | Passed | 0.00 |
| TC-CLASS-01: update audit append failure reports the persisted capacity | Passed | 0.01 |

### `tests/unit/test_console_ui.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| ENB-04-01: each menu tree lists exactly the SRS s3.1 items in order | Passed | 0.00 |
| ENB-04-02: rendered menus show own items and no out-of-role items | Passed | 0.00 |
| ENB-04-03: availability is limited to the role's menu tree | Passed | 0.00 |
| ENB-04-04: selection accepts 1..n and rejects everything else | Passed | 0.00 |
| ENB-04-05: success and validation outcomes go to stdout only | Passed | 0.00 |
| ENB-04-06: system failures are also written to stderr, refusals are not | Passed | 0.00 |
| ENB-04-07: menu loop dispatches selections and stops on Exit, Logout or end of input | Passed | 0.00 |
| TC-ROB-01: handled operation failures return to the menu | Passed | 0.00 |

### `tests/unit/test_console_usability.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-UX-02: the help frame count equals the menu count | Passed | 0.00 |
| TC-UX-02: every role-menu option is explained in its help frame | Passed | 0.00 |
| TC-UX-02: help is requested with ? h or help only | Passed | 0.00 |
| TC-UX-02: the deepest passenger path is within 4 selections | Passed | 0.00 |
| TC-PERF-04: menu and availability redisplay p95 is within 50 ms | Passed | 0.01 |

### `tests/unit/test_fare_calculation.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-FARE-02: the total is the configured fare times the passenger count | Passed | 0.00 |
| TC-FARE-02: no total exists without a positive fare or for an overflowing product | Passed | 0.00 |

### `tests/unit/test_fare_config.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-FARE-01: decimal fare input converts exactly to integer paise | Passed | 0.00 |
| TC-FARE-01: effective dates use strict Gregorian ISO validation | Passed | 0.00 |
| TC-FARE-01: 45000 paise and the effective date persist in the 32-byte format | Passed | 0.01 |
| TC-FARE-01: fare serialization preserves all eight little-endian paise bytes | Passed | 0.01 |
| TC-FARE-01: multiple effective dates coexist and select the latest applicable fare | Passed | 0.01 |
| TC-FARE-01: duplicate date requires explicit selected-record replacement | Passed | 0.01 |
| TC-FARE-01: replacement refuses a fare changed since it was selected | Passed | 0.01 |
| TC-FARE-01: stale selection is refused after fare changes away and back | Passed | 0.01 |
| TC-FARE-01: same-value replacement advances revision and invalidates old selection | Passed | 0.01 |
| TC-FARE-01: moved fare date cannot stale-replace a recreated original key | Passed | 0.03 |
| TC-FARE-01: revisions increase uniquely and persist across reopen | Passed | 0.01 |
| TC-FARE-01: revision uses all seven little-endian bytes | Passed | 0.01 |
| TC-FARE-01: invalid fare, date, train and class inputs do not persist or audit success | Passed | 0.00 |
| TC-FARE-01: today is accepted and an inactive train cannot receive a fare | Passed | 0.01 |
| TC-FARE-01: absent fare is explicit and lookup rejects invalid references | Passed | 0.00 |
| TC-FARE-01: passenger denial is audited once before invalid fare validation | Passed | 0.01 |
| TC-FARE-01: fare records survive module recreation | Passed | 0.01 |
| TC-FARE-01: AtomicJournal recovery restores a durable fare write | Passed | 0.01 |
| TC-FARE-01: malformed fare records are rejected rather than skipped | Passed | 0.03 |
| TC-FARE-01: revision overflow refuses mutation without persistence or success audit | Passed | 0.01 |
| TC-FARE-01: audit append failure reports that fare persistence already succeeded | Passed | 0.01 |
| TC-FARE-01: replacement audit append failure reports the updated persisted fare | Passed | 0.01 |

### `tests/unit/test_fixture_generator.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| ENB-06-01: SplitMix64 has stable known answers and unbiased range bounds | Passed | 0.00 |
| ENB-06-02: fixture plans are reproducible and contain valid bounded records | Passed | 0.01 |
| ENB-06-03: manifest fingerprint covers logical fixture fields | Passed | 0.01 |

### `tests/unit/test_fixtures.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-PERF-02: seeded 2000 train search fixtures are reproducible and consistent | Passed | 0.00 |
| TC-SEARCH-01: seed fixture provides train 12345, route and classes for search enquiry | Passed | 0.00 |
| ENB-05-01: deterministic seed loads complete known-answer dataset | Passed | 0.00 |
| ENB-05-02: known-answer train 12345, travel classes and capacities match SRS | Passed | 0.00 |
| ENB-05-03: known-answer fare 45000 paise and derived figures match SRS figures | Passed | 0.00 |
| ENB-05-04: admin and 4 passenger fixtures satisfy account and security policy | Passed | 0.00 |
| ENB-05-05: corrupted seed data fails validation | Passed | 0.00 |
| ENB-05-06: missing train class, unknown account, and invalid passenger counts are refused | Passed | 0.00 |

### `tests/unit/test_menu_router.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| ENB-08-01: the router runs the handler registered for an action | Passed | 0.00 |
| ENB-08-02: an action without a handler is refused as not available yet | Passed | 0.00 |
| ENB-08-03: duplicate and empty registrations are rejected | Passed | 0.00 |
| ENB-08-04: each feature registers its own actions without clashing | Passed | 0.00 |
| ENB-08-05: the context falls back to the UTC clock without a timestamp provider | Passed | 0.00 |

### `tests/unit/test_passenger.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-BOOK-01-01: one valid passenger is accepted | Passed | 0.00 |
| TC-BOOK-01-02: six valid passengers are accepted | Passed | 0.00 |
| TC-BOOK-01-03: seven passengers are rejected | Passed | 0.00 |
| TC-BOOK-01-04: empty passenger list is rejected | Passed | 0.00 |
| TC-BOOK-01-05: passenger name must contain 1 to 60 characters | Passed | 0.00 |
| TC-BOOK-01-06: passenger age must be between 1 and 120 | Passed | 0.00 |
| TC-BOOK-01-07: supported gender values are accepted | Passed | 0.00 |
| TC-BOOK-01-08: invalid gender value is rejected | Passed | 0.00 |
| TC-BOOK-01-09: invalid passenger identifies its position | Passed | 0.00 |

### `tests/unit/test_password_digest.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEC-04: digest uses a 16-byte salt and 10,000 rounds of SHA-256 | Passed | 0.01 |
| TC-SEC-04: the correct password verifies and a wrong one does not | Passed | 0.02 |
| TC-SEC-04: every digest gets a distinct salt | Passed | 0.01 |

### `tests/unit/test_payment.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-PAY-01: successful payment outcome and reference are recorded | Passed | 0.00 |
| TC-PAY-01: failed payment outcome and reference are recorded | Passed | 0.00 |
| TC-PAY-01: empty payment reference is rejected | Passed | 0.00 |
| TC-PAY-01: invalid payment outcome is rejected | Passed | 0.00 |

### `tests/unit/test_persistence.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-ROB-03-01: all seven record files support indexed append and read | Passed | 0.00 |
| TC-ROB-03-02: indexed read modify write preserves other records | Passed | 0.00 |
| TC-ROB-03-03: invalid record sizes and names are rejected | Passed | 0.00 |
| TC-ROB-03-04: cannot create sparse indexes or accept partial records | Passed | 0.00 |
| TC-ROB-03-05: missing files are empty and accept their first record | Passed | 0.00 |
| TC-ROB-03-06: binary record bytes are preserved without interpretation | Passed | 0.00 |
| TC-ROB-03-07: bulk record read preserves order and rejects partial files | Passed | 0.00 |

### `tests/unit/test_search.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEARCH-01: route matching sorts departure times and preserves past-only state | Passed | 0.01 |
| TC-SEARCH-01: query station codes use M6 validation and exact matching | Passed | 0.00 |
| TC-SEARCH-01: all weekday bits map Monday through Sunday and non-running days refuse | Passed | 0.01 |
| TC-SEARCH-01: date validation handles leap years and rejects malformed dates | Passed | 0.00 |
| TC-SEARCH-01: availability distinguishes no class, available and unknown | Passed | 0.00 |
| TC-SEARCH-01: deterministic 2000-train catalogue returns exact matching order | Passed | 0.01 |
| TC-PERF-02: 100 searches use a verified seeded 2000-record fixture | Passed | 58.37 |

### `tests/unit/test_seat_inventory.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-SEAT-01: first reference initializes configured class capacity | Passed | 0.01 |
| TC-SEAT-01: repeated reference returns the same persisted inventory | Passed | 0.01 |
| TC-SEAT-01: blocking twelve seats leaves sixty free and is audited | Passed | 0.01 |
| TC-SEAT-01: blocks are cumulative and cannot exceed free inventory | Passed | 0.02 |
| TC-SEAT-01: invalid date and unknown train or class are rejected | Passed | 0.01 |
| TC-SEAT-01: only administrators can block seats | Passed | 0.00 |
| TC-SEAT-01: invalid block counts and invalid dates do not mutate inventory | Passed | 0.00 |
| TC-SEAT-01: inventory is isolated by journey date | Passed | 0.01 |

### `tests/unit/test_train_catalogue.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| TC-ADMIN-01: train records use the approved 128-byte format and round-trip | Passed | 0.00 |
| TC-ADMIN-01: add rejects duplicate and invalid train data without writing | Passed | 0.00 |
| TC-ADMIN-01: removed train IDs stay reserved and removed records survive reopen | Passed | 0.00 |
| TC-ADMIN-01: accepted train names round-trip without trimming | Passed | 0.00 |
| TC-ADMIN-01: active trains remain available after catalogue reopen | Passed | 0.00 |
| TC-ADMIN-01: timestamps require a real UTC date and time before mutation | Passed | 0.00 |
| TC-ADMIN-01: update audit failure reports the persisted updated train | Passed | 0.00 |
| TC-ADMIN-01: remove audit failure reports the persisted removal | Passed | 0.00 |
| TC-ADMIN-01: catalogue rejects a 2,001st stored record | Passed | 0.03 |
| TC-ADMIN-01: multiple trains remain independently readable | Passed | 0.01 |
| TC-ADMIN-01: audit append failure reports persisted mutation as system failure | Passed | 0.00 |
| TC-ADMIN-01: update is immutable, validates before writing and refuses inactive IDs | Passed | 0.01 |
| TC-ADMIN-01: malformed and duplicate records fail catalogue reads | Passed | 0.00 |
| TC-ADMIN-01: concurrent distinct additions preserve every train and audit | Passed | 0.02 |
| TC-ADMIN-01: concurrent duplicate additions yield one record and one audit | Passed | 0.00 |

### `tests/unit/test_validation.cpp`

| Test case | Result | Time (s) |
|---|---|---:|
| RRS-SR-003-01: presence rejects empty and whitespace-only values | Passed | 0.00 |
| RRS-SR-003-02: length accepts the inclusive range only | Passed | 0.00 |
| RRS-SR-003-03: integer type accepts whole base-10 numbers only | Passed | 0.00 |
| RRS-SR-003-04: range accepts the inclusive bounds only | Passed | 0.00 |
| RRS-SR-003-05: integer combines presence, type and range | Passed | 0.00 |
| TC-ROB-02: 500 malformed integer inputs fail validation without exceptions | Passed | 0.00 |
