# Deviation Report: Railway Reservation System (Team 2)

Status: Draft. Update at every sprint review and present at the final demo.
Baseline: **SRS v2.0 (03-09-2026)**. Compared against: SAD v1.0 (03-10-2026) and Test Plan v1.0 (16-09-2026; the copy in the repo root is the current revision and supersedes older copies).
SRS v1.0 (18-08-2025, "SRS_FINAL") is superseded by v2.0 and is no longer used.

## 1. Summary

- **SRS v2.0 and the Test Plan agree.** Spot checks passed: 47 requirements (23 FR, 17 NFR, 7 SR), the tier build order, test case IDs, refund slabs, locking, lockout, session timeout, the 2,000-train fixture and the 95th-percentile measures. Only minor items remain (section 4).
- **The SAD does not follow SRS v2.0.** Its traceability table uses v1.0 numbering, and it has no design for the concurrency model, travel-class/fare flow, atomic commits, two security requirements, or the booking and cancellation rules. These are the main deviations (section 2).
- **SRS v2.0 has internal ambiguities** that implementers will hit (section 3). The team should decide each one before Sprint 1 ends.
- The backlog adds a few items that have no SRS ID, and marks some items as `stretch` (section 5).

Severity: **High** = changes what is built or tested. **Med** = confuses review or traceability. **Low** = cosmetic.

## 2. SAD vs SRS v2.0 / Test Plan

| # | Sev | Area | SRS v2.0 / Test Plan says | SAD v1.0 says | Proposed resolution |
|---|-----|------|---------------------------|---------------|---------------------|
| S-01 | High | Traceability, admin restriction | F-003 = logout. Admin-only access is SR-002 (constraint C-005) | 3.8 maps "RRS-F-003 / administrator restriction" to M5 | Change to `RRS-SR-002 -> M5` |
| S-02 | High | Traceability, search | F-004 = admin bootstrap, F-005 = password reset, F-006 = train maintenance. Search/availability = F-010..F-012 | 3.8 maps F-004..F-006 to search and availability | Re-map to F-010..F-012 (M9, M7, M8, M6) and map F-004..F-006 to M4/M5/M6 |
| S-03 | High | Travel class and fare flow | F-007 (classes), F-008 (per-class fare), constraint C-006; M7 sits above M11 with no return edge (SRS s9) | Not in the traceability table or design; M7 appears only in the component list | Add F-007/F-008 and the M7 -> M11 rule to 3.8 and 4.1 |
| S-04 | High | Concurrency | s2.7: advisory record lock (F_SETLKW) around seat allocation and cancellation, stale-read re-check, fsync; TC-CONC-01 is blocking | No locking design; Figure 2 shows booking as one "update booking/inventory" step | Add a locking design (M0 shim, M8, M13) and redraw Figure 2 with payment, lock, re-read, PNR check, commit |
| S-05 | High | Atomic commit and storage | s3.4: eight named record files; journal commit via rename; NFR-ROB-003. SQLite is an optional alternative only (A-03, D-06) | 3.6: "file or database interface"; 4.4: "no partial records" without a mechanism | Decide on flat binary files (the SRS reference build) and say so; describe the journal commit |
| S-06 | High | Security requirements | 7 SRs. SR-004 = salted 16-byte SHA-256 x10,000; SR-006 = password policy and 15-min lockout; SR-007 = 15-min idle timeout | 3.9 lists SR-001..SR-005 only; SR-004 mapped to "Authentication/persistence design" | Add SR-006 and SR-007 and name the digest scheme and library (D-03) |
| S-07 | High | Cancellation and refund | Table 4.5-1 slabs S1-S4, int64 paise, truncating division, state model PENDING/CONFIRMED/REJECTED/CANCELLED | Figure 3 says "check eligibility / calculate refund" with no slab, lock, status change or disbursement step | Add slab selection, lock, state transitions and PENDING/SETTLED refund record to the design |
| S-08 | Med | Network | C-004 and s3.5: no sockets, no network at all | 3.7 risk table lists "connection failure where network communication is used" | Delete the row; state single-process, local only |
| S-09 | Med | Constraints | C-002 (std library + listed dependencies only), C-003 (single-threaded), C-004, C-005, C-006, C-007 (2,000 trains, 10 classes) | 3.1 lists only C/C++, console UI, persistence, admin-only, consistency | Carry C-002..C-007 into 3.1 |
| S-10 | Med | Search and PNR design | s4.3.1 (vector catalogue, multimap route index, bitset day mask, three empty-result explanations); PNR collision check (F-017) | Not described | Add a short search/PNR design subsection |
| S-11 | Med | UX | s3.1: public/passenger/admin menu trees; four outcome classes; stdout/stderr rules; menus hide out-of-role items | 4.5 omits change password, travel-class and fare management, audit-log view and password reset; outcomes are "success, failure, unavailable seats, invalid input" | Align 4.5 to s3.1 |
| S-12 | Med | Dependency graph | s9 / Figure 3: tiered DAG, M0 has no dependencies, build order fixed; NFR-MNT-001 checks it | Figure 1 is a flat component diagram with an M2 -> M1 edge and no tiers | Reconcile Figure 1 with SRS Figure 3; the include-graph check needs one authoritative graph |
| S-13 | Low | Data stores | s3.4 has no separate payment file (payment reference lives in bookings.dat) | 3.1 lists "payment records" as a store | Reword |
| S-14 | Low | Scope text | 1.2 includes travel class, fare configuration, multi-passenger booking and refund disbursement | 1.2 omits them | Reword |
| S-15 | Low | Template leftovers | n/a | Glossary keeps "ATM"; 4.6 says no open issues (section 3 below lists several); status Draft, approvals blank | Clean up; list section 3 items as open issues |
| S-16 | Low | Names and wording | "Ankirapalle Joel Suhas"; "ISO C++17" | "Ankirapalle Goel Suhas"; "C/C++17" | Use "Joel" and "ISO C++17" |

## 3. SRS v2.0 internal ambiguities (decide before implementing)

| # | Sev | Issue | Where | Suggested decision |
|---|-----|-------|-------|--------------------|
| R-01 | High | **Payment order.** s2.7 step 4 records the payment inside the lock. UC-01 records it at M6, before the lock (M7), and F-015 says to record it against the pending booking | s2.7, UC-01 M6/M7, F-015 | Record payment at M6 (before the lock), as UC-01 and A6 describe. Update s2.7 |
| R-02 | High | **Fare effective date.** F-008 says the fare is recorded before the class is bookable. UC-04 D4 refuses past dates. D5 allows a future-dated replacement. It is not stated which record applies to a given journey (compared with booking date or journey date) | F-008, UC-04, F-012 | Use the latest record whose effective-from date is on or before the booking date. Note it in the SRS |
| R-03 | Med | **Failed booking record.** UC-01 says on failure "no booking record exists", but A6 sets status REJECTED and records the failed payment, and s7.3 has a REJECTED state | UC-01, s7.3 | Keep REJECTED records (audit value). Reword the postcondition |
| R-04 | Med | **Payment reversal.** A7 says the payment is "marked for reversal under RRS-F-021", but F-021 covers refunds of cancellations only | UC-01 A7, F-021 | Either extend F-021 or drop the sentence. A7 also contradicts R-01 if payment is already recorded |
| R-05 | Med | **"Nine action types".** F-023 and TC-AUDIT-01 say nine, but the list has eight (registration, authentication attempt, password change, master-data change, booking, cancellation, refund settlement, report access). Lockout, session timeout, seat block and refused access are also audited elsewhere | F-023, SR-005, SR-007, F-009, B2 | Fix the list. Suggested ninth: session timeout, with lockout/seat block/refused access covered under master-data and authentication entries |
| R-06 | Low | **"Six administrative operations".** SR-002 and the Test Plan say six, but the admin menu has seven (trains, classes, fares, seat adjustment, report, audit log, password reset) | SR-002, s3.1 | State the six or change to seven |
| R-07 | Low | Constraint numbering starts at C-002 (no C-001) | s2.5 | Renumber or add C-001 |

## 4. Test Plan vs SRS v2.0 (minor)

| # | Sev | Item | Resolution |
|---|-----|------|------------|
| T-01 | Med | Schedule (s7): the current revision has dates from 28-09-2026 to 02-11-2026 (older copies had `<TBD>`). By its row order it expects tiers 0-5 tested by 19-10 and system tests by 21-10, but implementation only started with the skeleton on 28-10 | The dates no longer match actual progress. Logged as deviation 1 in section 6; re-baseline the schedule to the revised sprint plan |
| T-02 | Low | Repeats the "nine action types" count (see R-05) and "six administrative operations" (R-06) | Fix with the SRS |
| T-03 | Low | Coordinator name is filled in (Prof. Asha K A) in the current revision; approval signatures are still empty | Collect signatures |
| T-04 | Low | STP assigns tier-4 booking as a joint task and tier-3 to all four members; the backlog assigns one owner per item | Backlog marks F-016 as joint; cross-testers listed on every issue |

## 5. Deviations introduced by the backlog (planned)

| ID | Item | Reason |
|----|------|--------|
| P-01 | ENB-01..ENB-07 (build and CI, platform shim, persistence, console framework, fixtures, fixture generator, booking orchestration) have no SRS ID | They implement SAD/SRS modules M0, M1, M13, M17 and the Test Plan fixtures, which no FR names |
| P-02 | DOC-01, DOC-02 (demo videos, final documentation) | Course deliverables |
| P-03 | 12 items are labelled `stretch` (24 of 169 points): F-005, F-021, F-022, SR-007, SPD-004, SIZ-001/002, EOU-001/002, REL-002, ROB-001, MNT-001 | The backlog is large for two one-week sprints. Cut order follows SRS s9: reporting and search carry the slack, never seat allocation. Dropping a stretch item is a deviation. A failed NFR needs a variance signed by the course coordinator (SRS 6.2) |
| P-04 | Sprint 1 covers tiers 0-2, tier-1 security and the independent tier-3 modules F-013, F-014, F-015. Search (M9) is in Sprint 2 | Search has the most slack (SRS s9) and needs the fixture generator |
| P-05 | Fixtures: a small known-answer seed in Sprint 1; the 2,000-train generator in Sprint 2 | Capacity |
| P-06 | Storage: flat binary record files; SQLite not used | SRS reference build; resolves S-05 |
| P-07 | Points are team estimates and are recalibrated after the dry run | Velocity is unknown until then |

## 6. Implementation deviation log (fill in during the sprints)

| # | Date | Sprint | Backlog ID | Deviation from SRS/SAD/STP | Reason | Decided by | Documents to update |
|---|------|--------|-----------|----------------------------|--------|-----------|---------------------|
| 1 | 28-10-2026 | 1 | all | Test Plan s7 schedule assumed tiers 0-5 tested by 19-10 and system tests by 21-10; implementation started with the skeleton on 28-10 | Schedule slipped against the plan; work re-planned as daily issue delivery, then docs, video and review, with Sprint 2 following | Team | Test Plan s7 |
| | | | | | | | |
