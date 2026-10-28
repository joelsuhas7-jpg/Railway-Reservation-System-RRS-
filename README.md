# Railway Reservation System (RRS)

A menu-driven console application for railway ticket reservation, written in ISO C++17.
Passengers search trains, book tickets for up to six people under one PNR, view tickets and
cancel for a slab-based refund. Administrators manage trains, travel classes, fares, seat
inventory and reports. Software Engineering mini-project, Team 2.

> **Status: Sprint 1 in progress.** The repository currently holds the build/CI skeleton and a
> placeholder test. Features land issue by issue; see the project board for what is done.

## Design at a glance

- Single-process console application. No network, no sockets, single-threaded.
- Persistence in local binary record files (`users.dat`, `trains.dat`, `classes.dat`,
  `fares.dat`, `inventory.dat`, `bookings.dat`, `cancellations.dat`) plus an append-only `audit.log`.
- Seat allocation and cancellation are the only critical section, protected by advisory record
  locking so two instances cannot double-book a seat.
- Money is held as integer paise, never floating point.
- Modules M0 to M17 are built in tiers (platform shim and persistence first, booking and
  cancellation last). The authoritative graph is Section 9 of the SRS.

## Requirements

- A C++17 compiler (g++ 11 or later; Linux is the reference target, Windows/MinGW secondary)
- CMake 3.16 or later

## Build, run and test

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/railway_reservation
```

CI configures with `-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"`, so any
compiler warning fails the build. Use the same flags locally before pushing.

Tests: the skeleton has one placeholder CTest case. The Test Plan specifies Catch2 v3 with one
test file per module, plus scripted end-to-end console sessions. Both are added as modules land.

## Repository layout

```
src/                  application source (one header + source file per module, as modules land)
tests/                unit and integration tests
.github/workflows/    CI: build and test on pushes to main and feature branches, and on pull requests to main
```

## Documents

| Document | File |
|----------|------|
| Software Requirements Specification v2.0 (authoritative) | `Team_2_Railway_Reservation_System_SRS_v2.docx` |
| Software Architecture and Design Specification v1.0 | `Team_2_RRS_SAD_v1.0.pdf` |
| Software Test Plan v1.0 | `Team_2_RRS_Test_Plan_v1.0.docx` |
| Deviations from the submitted documents | `docs/DEVIATIONS.md` |

## Workflow

1. Pick an issue assigned to you from the GitHub Project board.
2. Branch from `main` as `feature/<issue-number>-short-name`.
3. Write the module and its tests together. Cover normal cases and boundary/error cases.
4. Push and open a pull request that says `Closes #<issue>`. CI must be green.
5. The repository owner reviews and merges.

Any deviation from the SRS, SAD or Test Plan is recorded in `docs/DEVIATIONS.md`.

## Team

| Name | SRN | Role (per Test Plan) |
|------|-----|----------------------|
| Anishwar Rajaram | PES2UG24CS068 | QA lead, authentication, console UI |
| Ankirapalle Joel Suhas | PES2UG24CS069 | Core transaction path: booking, cancellation, concurrency |
| Ankit Uttarakar | PES2UG24CS070 | Persistence, fixtures, search, non-functional runs |
| Ankit Verma | PES2UG24CS071 | Build, platform shim, validation, security |

Course Coordinator: Prof. Asha K A
