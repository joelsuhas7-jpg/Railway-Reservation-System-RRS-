Closes #

## What changed
<!-- One or two lines: module(s) added or changed. -->

## Traceability
| Test case ID | SRS requirement ID | Test file |
|--------------|--------------------|-----------|
|              |                    |           |

## Checklist (see TESTING.md)
- [ ] Module in `src/<module>.hpp/.cpp`; tests in `tests/unit/` (and `tests/integration/` if two or more modules interact)
- [ ] Each acceptance criterion has a positive and a negative test, including the SRS boundary values
- [ ] Every `TEST_CASE` name starts with its TC ID and is tagged `[unit]`/`[integration]` plus the module tag
- [ ] Tests use a temporary directory, synthetic data and an injected clock where time matters
- [ ] Money is `std::int64_t` paise
- [ ] Built locally with `-Wall -Wextra -Werror`: zero warnings, `ctest` green
- [ ] No change to `CMakeLists.txt` or `ci.yml`

## Not testable yet / deviations
<!-- Acceptance criteria blocked by an unmerged dependency, and anything that differs from the SRS, SAD or Test Plan. Write "None" if none. -->
