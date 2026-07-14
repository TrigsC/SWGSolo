# Testing Guidelines

## Test Framework

GoogleTest / GoogleMock, vendored at
`MMOCoreORB/utils/googletest-release-1.13.0`. Tests are compiled directly
into the `core3` server binary as a library target (`core3tests`) rather
than a standalone test executable — there is no separate `ctest`/`gtest`
binary to invoke; you run the server binary itself with a special flag.

## Running Tests

All commands run inside the `swgemu_server` Docker container (see
`docs/ARCHI.md` §6 for the build step that must precede a test run whenever
source has changed).

```bash
# Run all tests
docker exec -u swgemu swgemu_server bash -lc 'cd ~/workspace/Core3/MMOCoreORB/bin && ./core3 runUnitTests'

# Run a specific test / suite (standard gtest --gtest_filter syntax)
docker exec -u swgemu swgemu_server bash -lc 'cd ~/workspace/Core3/MMOCoreORB/bin && ./core3 runUnitTests --gtest_filter=ZoneTest.*'

# Coverage: not defined — no coverage build/target is wired into this
# project's CMake (gcovr is installed in the container image but unused).
```

## Test Organization

- All test sources live flat under `MMOCoreORB/src/tests/`, one
  `<SubjectUnderTest>Test.cpp` per suite. `MMOCoreORB/src/tests/CMakeLists.txt`
  globs `*.cpp`/`*.h` in that directory, so a new test file is picked up by
  the next build automatically — no CMake edit needed.
- Shared scaffolding:
  - `TestCore.h` — base test harness used by most suites.
  - `MockBehavior.h` / `MockCompositeBehavior.h` — GoogleMock doubles for
    behavior-tree testing.
  - `DeadlockTestBase.h`/`.cpp` — base for concurrency/locking tests.
- Existing suites (non-exhaustive): `ZoneTest`, `CreatureObjectTest`,
  `CreditObjectTest`, `ConfigManagerTest`, `TerrainManagerTest`,
  `BasicTerrainTest`, `SpaceZoneTest`, `NameManagerTest`, `JediManagerTest`,
  `CommandLuaTest`, `LuaMobileTest`, `LuaShipAgentTest`,
  `BasicScreenPlayTest`, `AreaShapeTests`.

## Writing Tests

- Name the file `<SubjectUnderTest>Test.cpp`; name the gtest fixture class
  to match.
- Follow the existing suites' pattern for constructing a minimal
  `TestCore`-based fixture rather than standing up a full zone/server.
- For Lua↔C++ boundary logic, mirror `CommandLuaTest.cpp` /
  `LuaMobileTest.cpp` rather than writing a new integration harness.
- For anything touching distributed objects, respect the same
  `Reference`/`ManagedReference`/`Locker` discipline required in production
  code (see `docs/ARCHI.md` §5, §9) — tests are not exempt.

## Coverage Requirements

Not defined. There is no CI gate or minimum-coverage enforcement in this
project. In practice, the AI-economy/PvP/Jedi layers (this fork's primary
active work) have little to no GoogleTest coverage and are instead verified
against the **live dashboard**
(`https://127.0.0.1:44443/v1/aieconomy/dashboard/`, Bearer-token gated) or
in-game, before/after a change — see `docs/ARCHI.md` §11-§13 and
`.claude/skills/TRIP-test/SKILL.md` for the live-verification workflow.
Uncovered risky paths should still get a one-line entry in
`docs/4-unit-tests/COVERAGE-DEBT.md` per the hard-to-cover-code policy in
`TRIP-2-implement`/`TRIP-test`.
