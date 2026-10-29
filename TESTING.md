# Testing guide

Every issue ships its module and its tests in the same pull request. CI builds with
`-Wall -Wextra -Werror` and runs every test on each push to a `feature/**` branch and on each pull
request to `main`; a warning, a failing test or an empty test suite fails the build.

## Where tests go

| What | Where | Picked up by |
|------|-------|--------------|
| Module source | `src/<module>.hpp` + `src/<module>.cpp` | the `rrs_core` library, automatically |
| Unit tests (one module) | `tests/unit/test_<module>.cpp` | `rrs_tests`, automatically |
| Integration tests (two or more modules) | `tests/integration/test_<flow>.cpp` | `rrs_tests`, automatically |

Do not edit `CMakeLists.txt` or `.github/workflows/ci.yml` to add a module or a test.

## How to name and tag a test

- Start every `TEST_CASE` name with its Test Plan ID (Test Plan s13 or the issue's "Test case" field):
  `TEST_CASE("TC-REG-01: valid registration creates one PASSENGER account", "[unit][M4]")`
- Tag `[unit]` or `[integration]`, plus the module tag (`[M1]` ... `[M17]`).
- Where a criterion has no Test Plan ID, use `<backlog id>-NN` (for example `ENB-04-01`) and say so
  in the pull request.

## What to cover

For each acceptance criterion in the issue, write at least one **positive** test (the rule works)
and one **negative** test (bad input or a broken rule is refused with the right outcome).

Test the SRS boundary values on both sides, and assert the literal SRS figures:

| Rule | Boundary values |
|------|-----------------|
| Passengers per booking | 0, 1, 6, 7 |
| Age | 0, 1, 120, 121 |
| Name length | 0, 1, 60, 61 |
| Class capacity | 0 and negative are refused |
| Password length | 7 refused, 8 accepted |
| Failed logins | 5th still allowed, 6th locks the account |
| Refund slabs | exactly 48 h, 12 h and 4 h before departure, and one minute either side |
| Known answers | 45000 paise for (12345, SL); 180000 for four passengers; 90000 for an S2 cancellation; 72 seats minus a block of 12 gives 60 |

When an issue connects two or more modules (for example register, then log in from the saved
file), add an integration test for that interaction.

## Rules for every test

- **Isolation:** never touch a real data directory. Create a fresh temporary directory with
  `std::filesystem::temp_directory_path()`, use it, and remove it at the end of the test.
- **Synthetic data only:** no real names, phone numbers or passwords.
- **Time:** code that depends on the clock (15-minute lockout, 15-minute idle timeout, refund slabs)
  takes the current time as a parameter or an injectable clock, so tests set the time directly.
  Never `sleep` in a test.
- **Money:** `std::int64_t` paise everywhere; never `float` or `double`.
- **Deterministic:** a test passes or fails the same way on every run and in any order.

## Before you push

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"
cmake --build build --parallel
ctest --test-dir build --output-on-failure --no-tests=error
```

Run one test or one module while working:

```bash
./build/rrs_tests "TC-REG-01*"
./build/rrs_tests "[M4]"
```

## Coverage, static analysis and sanitizers

CI runs these on every push and pull request. Warnings from cppcheck, a sanitizer error or a
failing test fails the build; coverage is reported, with no minimum yet. To run them locally
(Linux with g++; on macOS add `--gcov-executable "xcrun llvm-cov gcov"` to `gcovr`):

```bash
cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0"
cmake --build build-cov --parallel && ctest --test-dir build-cov
gcovr --root . --exclude-directories build-cov/_deps --filter src/ --exclude 'src/main\.cpp' \
      --exclude-throw-branches --exclude-unreachable-branches --txt-metric branch --print-summary

cppcheck --enable=warning,performance,portability --std=c++17 --language=c++ \
         --suppress=missingIncludeSystem --error-exitcode=1 -I src src

cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
cmake --build build-san --parallel && ctest --test-dir build-san --output-on-failure
```

Look at the uncovered lines and branches in your module's report and add tests for the ones an
acceptance criterion reaches.

## Pull request

Fill in the pull request template: `Closes #<issue>`, the TC IDs and SRS requirement IDs covered,
and any acceptance criterion that cannot be tested yet because a dependency is not merged. Any
difference from the SRS, SAD or Test Plan goes to the QA lead.
