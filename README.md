# Railway Reservation System (RRS)

A menu-driven console application for railway ticket reservation, written in ISO C++17.
Passengers search trains, book tickets for up to six people under one PNR, view tickets and
cancel for a slab-based refund. Administrators manage trains, travel classes, fares, seat
inventory and reports. Software Engineering mini-project, Team 2.

> **Status: development frozen on 12-11-2026.** Every SRS v2.0 feature is implemented. The 245
> automated test cases pass on Linux, and the 242 that build on Windows pass there. Final reports,
> the deviation record and both sprint videos are in [`docs/`](docs/).

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
- OpenSSL libcrypto development files (SHA-256 password digests)

## Build, run and test

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/railway_reservation
```

CI configures with `-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"`, so any
compiler warning fails the build. Use the same flags locally before pushing.

Every push and pull request also runs:

- a coverage build that reports line and branch coverage of `src/` (summary on the run page, full
  HTML report in the `coverage-report` artifact);
- cppcheck static analysis, where warnings fail the build and style findings are listed as advice;
- the test suite under AddressSanitizer and UndefinedBehaviorSanitizer;
- a Docker image build, which compiles and tests the application inside the container.

Each push to `main` that passes every check publishes the Linux binary and the Docker image as
build artifacts on the workflow run.

Run in Docker:

```bash
docker build -t rrs .
docker run --rm -it rrs
```

Tests: Catch2 v3.5.4 (downloaded by CMake on first configure, so that step needs network access).
Unit tests are in `tests/unit/test_<module>.cpp` and integration tests in
`tests/integration/test_<flow>.cpp`; new test files and new `src/` modules are picked up
automatically, with no CMake or CI edits. Each `TEST_CASE` name starts with its Test Plan ID and is
tagged `[unit]` or `[integration]` plus the module tag (for example `[M4]`). The integration tests
drive scripted console sessions end to end for both roles. See [TESTING.md](TESTING.md) for what
each pull request had to test and how.

## Repository layout

```
src/                  application source (one header + source file per module, plus one flow file per feature)
tests/unit/           unit tests (Catch2), one file per module
tests/integration/    integration tests (Catch2), one file per cross-module flow
docs/                 design notes, deviation record, include graph
docs/reports/         unit test, system validation, code and branch coverage reports, usability trial
docs/videos/          Sprint 1 and Sprint 2 demonstration videos
scripts/              include-graph check used by CI
cmake/                platform-shim and binary-size checks
.github/workflows/    CI: build, test, coverage, static analysis, sanitizers and Docker on pushes to main and
                      feature branches and on pull requests to main; release artifacts from main
Dockerfile            two-stage image: build and test, then a minimal runtime image
```

## Documents

| Document | File |
|----------|------|
| Software Requirements Specification v2.0 (authoritative) | `Team_2_Railway_Reservation_System_SRS_v2.docx` |
| Software Architecture and Design Specification v1.0 | `Team_2_RRS_SAD_v1.0.pdf` |
| Software Test Plan v1.0 | `Team_2_RRS_Test_Plan_v1.0.docx` |
| Deviations from the submitted documents | [`docs/Design_Deviations.md`](docs/Design_Deviations.md) |

## Reports and videos

| Deliverable | File |
|-------------|------|
| Project report v2.0 | [`docs/reports/Team_2_RRS_Project_Report.docx`](docs/reports/Team_2_RRS_Project_Report.docx) |
| Unit test report | [`docs/reports/Unit_Test_Report.md`](docs/reports/Unit_Test_Report.md) |
| System validation report | [`docs/reports/System_Validation_Report.md`](docs/reports/System_Validation_Report.md) |
| Code (line) coverage report | [`docs/reports/Code_Coverage_Report.md`](docs/reports/Code_Coverage_Report.md) |
| Branch coverage report | [`docs/reports/Branch_Coverage_Report.md`](docs/reports/Branch_Coverage_Report.md) |
| Coverage detail (gcovr HTML, text and Cobertura) | [`docs/reports/coverage/`](docs/reports/coverage/) |
| Usability trial TC-UX-01 | [`docs/reports/Team_2_RRS_Usability_Trial_TC-UX-01.docx`](docs/reports/Team_2_RRS_Usability_Trial_TC-UX-01.docx) |
| Sprint 1 demonstration video | [`docs/videos/RRS_Sprint1_Demo.mp4`](docs/videos/RRS_Sprint1_Demo.mp4) |
| Sprint 2 demonstration video | [`docs/videos/RRS_Sprint2_Demo.mp4`](docs/videos/RRS_Sprint2_Demo.mp4) |

## Workflow

Development is frozen. The team built the project with this workflow:

1. Pick an issue assigned to you from the GitHub Project board.
2. Branch from `main` as `feature/<issue-number>-short-name`.
3. Write the module and its tests together. Cover normal cases and boundary/error cases.
4. Push and open a pull request that says `Closes #<issue>`. CI must be green.
5. The repository owner reviews and merges.

The QA lead tracked deviations from the SRS, SAD and Test Plan. The final record is
[`docs/Design_Deviations.md`](docs/Design_Deviations.md).

## Team

| Name | SRN | Role (per Test Plan) |
|------|-----|----------------------|
| Anishwar Rajaram | PES2UG24CS068 | QA lead, authentication, console UI |
| Ankirapalle Joel Suhas | PES2UG24CS069 | Core transaction path: booking, cancellation, concurrency |
| Ankit Uttarakar | PES2UG24CS070 | Persistence, fixtures, search, non-functional runs |
| Ankit Verma | PES2UG24CS071 | Build, platform shim, validation, security |

Course Coordinator: Prof. Asha K A
