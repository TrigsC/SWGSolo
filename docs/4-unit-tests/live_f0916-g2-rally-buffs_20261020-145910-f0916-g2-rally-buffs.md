# Live verification: F_0.9.16 P.10p G2, rally and buffs

**Run:** `20261020-145910-f0916-g2-rally-buffs` (2026-10-20 to 2026-10-21)
**Plan:** `docs/1-plans/F_0.9.16_p10p-g2-rally-buffs.plan.md`
**Branch:** `feat/p10p-g2-rally-buffs` (uncommitted on v0.9.16, `004c7c605a`)
**Verdict:** LIVE_VERIFICATION_PASS on accumulated evidence (owner decision, 2026-10-21)

## Owner decision on the receipt

The contract required one boot with every scenario passing. Sixteen full or narrowed boots each
halted on a different flake in the G1-era harness, which borrows a live production bot in a live
world. None of those failures was in G2 code. The owner chose to record the receipt on the
accumulated evidence below rather than spend more of the week's usage on rerolls.

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild |
| `luac -p` / `node --check app.js` | clean |
| `SimBot*` gtests, server stopped (`timeout -s KILL 120`) | 200/200 on the final binary |
| Database holders before every start | none |
| Codex code review | APPROVED (3 rounds), then APPROVED for every verification-time delta |

## Assertions

| # | Assertion | Evidence | Result |
|---|---|---|---|
| 1 | Build, Lua, JS clean | every rebuild | PASS |
| 2 | gtests 200/200 server stopped | final binary | PASS |
| 3 | `group_rally_cantina`: rally, hold, ready line, release on leader arrival | iteration boots 2-3 | PASS |
| 4 | `group_buffs_and_rally`: meet-city buffs (real), hold, ready line | boot 3, order check | PASS |
| 5 | `group_buff_on_the_way`: Bestine -> Mos Eisley shuttle buffs -> back, hold | boots 5, order check | PASS |
| 6 | `group_rally_cross_planet`: Dantooine Mining Outpost hold | boot 5 | PASS |
| 7 | `group_follow_cancels_rally` | boot 6 | PASS |
| 8 | `group_unknown_place`: "Where's that?", no order | boot 6 | PASS |
| 9 | `group_shuttle_follow`: same-planet shuttle on follow | boot 7 | PASS |
| 10 | `group_rebuff_notice`: exactly one per visit | boot 7 | PASS |
| 11 | `group_directive_gate_off` | boot 7 | PASS |
| 12 | `group_buff_death_resume`: PLANNING on death, re-plan from the clone | boot 7 | PASS |
| 13 | `group_shuttle_fallback_walk`: failure -> suppression -> walk, no hold | boot 9 | PASS |
| 14 | `group_directive_during_travel`: epoch mismatch, plan after landing | boot 10 | PASS |
| 15 | Last narrowed run (38): 37/38; the miss was a stand-in despawn (harness, fixed) | narrowed run 3 | PASS |
| 16 | P.10 regression (store, buff, loadout, armor, food) | boot 1 (71/71 before G2 scenarios) and later boots | PASS |
| 17 | No crash, no new error signature | `gdb.log` unchanged at 1,190,538 bytes | PASS |
| 18 | Store invariants after reaping the 57 G1-harness orphans | `store_boot_invariant` PASS every boot | PASS |

## Defects found by the live run and fixed

**Product (G2):**
- a bot parked forever 6-10 m from its rally slot (HOLD band never counted as arrival);
- a released bot kept buffing for 2.5 min when an order landed the same second (`cancelGroupBuff` on order start and solo ticks);
- "meet me at narnia" read as the leader; chained phrases ("meet me and group up") read as unknown; "get" not filler;
- the follow-path shuttle start was not counted (shared `startGroupTravelLeg` helper).

**Harness:** directive-relative position checks, counter re-baseline and `a|b` sums, `byKind`
counter case bug, reach-leader preconditions, longer follow windows, stand-in re-homing, leash reset
(a leashing AiAgent is not attackable: the root of most stand-in flakes), zero-damage and
no-despawn stand-in, line-of-sight attacker placement, a self-describing combat-start failure,
armor-mitigation flake hardening (wait during the fight, deep target pools).

## Open harness debt

- G1 scenarios borrow a live production bot; world events (wild packs, its own missions) still
  interfere. A harness-only grouped identity would make the grouping set deterministic.
- The stand-in is a level-178 combat body; it is now disarmed and re-homed, but a purpose-built
  passive template would be cleaner.

## Configuration

Temporary verify settings (harness, start delay, reaper, join delay, scenario filter) were removed;
the live copy is back to the owner's 14 live-only lines. `directivesEnabled` stays false live
until the owner turns it on.

## Evidence

`bin/log/trip-verify-20261020-145910-f0916-g2-rally-buffs-evidence.log` (contract, every boot's result and cause), `bin/log/missiondiag.log`.
