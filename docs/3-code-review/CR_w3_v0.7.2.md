# Code Review: Doctor/Entertainer Buffer Player-Chat Crash Fix

**Review Date**: 2026-07-31
**Version**: 0.7.2
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/custom_scripts/smart_entertainer_helper.lua`
- `MMOCoreORB/bin/scripts/screenplays/custom/aiGlobalChatHandler.lua`
- `MMOCoreORB/bin/scripts/screenplays/custom/smartDoctorBuffer.lua`
- `MMOCoreORB/src/server/zone/objects/creature/LuaCreatureObject.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/LuaCreatureObject.h`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `docs/1-plans/F_0.7.2_doctor-buffer-player-chat-crashfix.plan.md`
**Codex loop**: 2 rounds (start `REQUEST_CHANGES` → resume `APPROVED`) → synthesized.

---

## Executive Summary

The change eliminates a server-aborting Lua cast when real players chat with buffer NPCs. It adds a safe creature-level sim-bot predicate and guards the related doctor, entertainer, and legacy-healing paths. No code defects remain; two process findings were accepted with documented scope and sequencing overrides, both now resolved.

**APPROVED with observations**

---

## Changes Overview

`LuaCreatureObject` now exposes `isSimPlayerBot()` through a safe `asAiAgent()` lookup that returns false for non-agents. The doctor and entertainer helpers validate creature type before using the new binding, while the legacy healing path validates `isAiAgent()` before constructing `LuaAiAgent`. The PvE player-bot design document records the crash cause and required safe-wrapper conventions.

---

## Findings

### Critical Issues

None.

### Major Issues

#### Out-of-scope dirty `engine3` modification

- **Location**: `MMOCoreORB/utils/engine3/MMOEngine/src/engine/core/TaskManager.h:137`
- **Description**: The initial review treated the unrelated `LambdaTask` ownership edits in the dirty submodule as part of the feature change.
- **Disposition**: **Accepted with override.** The submodule pointer remains unchanged, and standing release policy explicitly excludes this pre-existing working-tree content (`docs/2-changelog/w3_v0.7.0.md`). It will not ship in the feature commit and must not be reverted as part of this work.

#### Live acceptance not yet executed

- **Location**: `docs/1-plans/F_0.7.2_doctor-buffer-player-chat-crashfix.plan.md`
- **Description**: The initial review blocked approval because real-player buffer chat had not been verified live.
- **Disposition**: **Resolved.** Live acceptance was completed after review via an owner-approved restart: the owner spoke a buff request to the doctor as a real player and confirmed the buff worked with no SIGABRT; server-side evidence captured no `gdb.log`, a live `core3` process, and a live REST dashboard. The crash trigger requires a real in-game player action that TRIP-verify cannot script, so an **owner-attested PASS receipt** (4/4 assertions) was recorded (`.claude/verification/latest.json`; evidence `MMOCoreORB/bin/log/verify_f072_attestation.txt`, report `MMOCoreORB/bin/log/verify_f072_report.md`). A separate pre-existing bot→doctor buff-trip fallback anomaly was observed and is tracked for its own diagnosis — it is not a regression of this change.

### Minor Issues

None.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; implementation matches the plan and handles player, non-creature, non-agent, and sim-bot inputs.
- [x] 2. Code Quality — Passed; changes are concise, typed, and consistent with nearby binding and Lua patterns.
- [x] 3. Architectural Compliance — Passed; structural safety resides in C++, behavioral guards remain in Lua, and design documentation is updated.
- [x] 4. Distributed Object / IDL Discipline — Passed; no IDL, generated-code, locking, persistence, or retained-reference changes.
- [x] 5. Lua/C++ Boundary — Passed; unsafe casts are guarded at `smartDoctorBuffer.lua:246`, `smart_entertainer_helper.lua:34`, and `aiGlobalChatHandler.lua:487`.
- [x] 6. AI-Economy / Simulation Safety — Passed; no mutation or new ungated behavior introduced; live acceptance completed via the owner-attested receipt.
- [x] 7. Error Handling — Passed; `LuaCreatureObject.cpp:800` safely returns false for non-agents, and Lua callers retain protected-call handling.
- [x] 8. Security — Not applicable; no authentication, endpoint, credential, or untrusted-input surface changed.
- [x] 9. Performance — Passed; the new predicate is O(1) and outside per-tick loops.

---

## Verdict

**APPROVED with observations**

The code diff has no open critical or major defects. The pre-existing `engine3` work is excluded by standing release policy. Live player verification is complete via the owner-attested receipt. Lint and type-check are clean; 12 affected tests pass, and the sole failure (`LuaMobileTemplatesTest`) is confirmed on the clean base and unrelated to this change.
