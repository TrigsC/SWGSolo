# Plan - SimNPC Player-Dot Presentation (Part 1) & Swoop Mount Revival (Part 2)

Status: **IMPLEMENTED 2026-07-01 - built clean (-Werror), PENDING RESTART + VERIFY.**
Owner approved both parts same day. What shipped:
- Part 1: `presentationConfig.showSimNpcsAsPlayerDots = true` (lua);
  `applySimNpcPresentation()` helper applied at ALL SEVEN sim-NPC pvp-bitmask
  spawn sites (toggleBot imperial/rebel/miner, spawnFromConfig pvp/miner,
  spawnSimPlayerWithRoute pvp/miner); `simNpcPresentation` dashboard section.
- Part 2 B1: `arrangementDescriptorFilename = "abstract/slot/arrangement/player.iff"`
  added to all six artisan dressed_* object templates;
  `vehicleConfig.enableVehicleMechanics = true`, `selfTestEnabled = true`.
- Pre-restart baseline saved (vehicleMechanics deploys/mounts/failures all 0,
  simNpcPresentation absent). After restart verify: mounts > 0 with failures
  flat, miner count stable, simNpcPresentation.npcsFlaggedTotal == spawned sim
  NPCs, and owner checks radar dots + visual seat in-game (Phase B2).
Date: 2026-07-01. Companion research is summarized at the end (all claims
verified against the live tree this session).

## INCIDENT 2026-07-02: server-wide lockup during backup - root-caused and fixed

After the restart, the server froze during a DB backup (gdb dump:
`MMOCoreORB/bin/gdb.txt`). Root cause was NOT the backup and NOT Part 1 - it was
a **pre-existing latent bug in the P.4.4a vehicle self-test code that the
arrangement fix activated**:

- `Locker crossLocker(vehicle, agent)` requires the cross (agent) to already be
  write-locked by the current thread (`assert(cross->isLockedByCurrentThread())`
  in Locker.h - compiled out by `-DNDEBUG=1`). MountCommand satisfies it because
  the command framework pre-locks the creature; `deployAndMountMinerVehicle` and
  `dismountAndStore` did NOT - the agent locker taken earlier had already gone
  out of scope.
- With an unheld cross, the crosslock retry loop
  (`ReadWriteLock::wlock(Lockable*)`, ReadWriteLock.cpp ~255-266) executes
  `pthread_rwlock_unlock(&agent->rwlock)` on a lock this thread does not hold -
  undefined behavior that released/corrupted the miner's rwlock out from under
  whichever SimPlayer task legitimately held it (recovery, density and
  path-validation tasks all lock the same miner).
- The corrupted rwlock wedged: 4 task workers blocked forever on the miner's
  lock (self-test also held the vehicle), so the backup's `blockTaskManager()`
  could never park the workers → whole-server freeze that merely *surfaced*
  during the backup.
- Why P.4.4a's first run never hit it: mounts failed instantly and the vehicle
  was barely in-world, so `trywrlock(vehicle)` never lost a race and the buggy
  loop body never executed. The mount fix made vehicles live 10+ s with real
  containment traffic - first contended trylock detonated it.

**Fix (built clean 2026-07-02):** `Locker agentLocker(agent);` added immediately
before all three `Locker crossLocker(vehicle, agent)` sites (deploy block, mount
block, dismount/store block) in SimPlayerManager.cpp, restoring the MountCommand
choreography. The fourth crosslock (`vehicle, device`) was already correct.
Audited the rest of SimPlayerManager + SimPlayerController for object crosslocks
with unheld crosses - none remain. Pending restart + a full self-test cycle
(deploy→mount→hold→dismount→store) plus one backup window to verify.

Two independent, gated, reversible changes:

- **Part 1 - `showSimNpcsAsPlayerDots`:** SimMiners render on client radar/map
  exactly like players (blue neutral dot; purple/red follow automatically from
  faction/attackable flags later). Pure server-side. Low risk. Ship first.
- **Part 2 - NPC swoop mount (P.4.4 revival):** fix the exact P.4.4a failure
  (rider arrangement descriptor) with a server-Lua-only data change, re-run the
  existing gated self-test, and only touch client TRE if the visual seat fails.

Neither change mutates economy/inventory/persistence. Both default OFF.

---

## Part 1 - Show sim NPCs as player dots

### Root cause / mechanism (verified)
The client radar/map classifies "player" purely from **UpdatePVPStatusMessage**
(`pvpStatusBitmask` + faction). Real players carry `pvpStatusBitmask = PLAYER`
(`ObjectFlag::PLAYER = 0x10`) from `bin/scripts/object/creature/player/*.lua:51`.
`sendPvpStatusTo()` (TangibleObjectImplementation.cpp:312) recomputes
ATTACKABLE/AGGRESSIVE/TEF/ENEMY per viewer but passes PLAYER through untouched.
No TRE/UI edit involved - `ui_ground_hud_radar.inc` holds artwork only.

### Changes
1. **`bin/scripts/managers/sim_player_manager.lua`** - new block:
   ```lua
   -- Client presentation only: adds ObjectFlag::PLAYER (0x10) to spawned sim
   -- NPCs' pvpStatusBitmask so clients render them with player radar dots and
   -- player con-color rules. No server gameplay logic reads this bit on AiAgents.
   presentationConfig = {
       showSimNpcsAsPlayerDots = false,  -- default OFF
   },
   ```
2. **`SimPlayerManager.h`** - `bool simNpcPlayerDotEnabled = false;`
3. **`SimPlayerManager.cpp`:**
   - Parse `presentationConfig` in the existing Lua-config loader (same pattern
     as `vehicleConfig` parsing, ~line 13485).
   - In **both** miner spawn paths, immediately after the existing
     `agent->setCustomAiMap(String("simMiner").hashCode())` (~lines 6047 and
     20186, agent already locked there):
     ```cpp
     if (simNpcPlayerDotEnabled)
         agent->setPvpStatusBitmask(ObjectFlag::PLAYER, true);
     ```
     (include `templates/params/creature/ObjectFlag.h` if not already pulled in).
   - Dashboard: add a small `simNpcPresentation` section
     (`showAsPlayerDots`, `minersFlagged` counter) so it is verifiable via REST.

### Safety analysis (all verified in code this session)
- **No server gameplay effect on AiAgents.** Every server read of
  `ObjectFlag::PLAYER` is player-scoped: `isInvulnerable()` is
  `isPlayerCreature()`-gated (CreatureObjectImplementation.cpp:3775); pet
  bitmask propagation strips it (TangibleObjectImplementation.cpp:464);
  Invulnerable/Tame/PetTransfer/JediMindTrick paths operate on players only.
- **Crash-safe with miner internals.** Miners spawn from creature template
  `artisan` with `pvpBitmask = NONE`, so `closeobjects == nullptr`
  (AiAgentImplementation.cpp:304). `setPvpStatusBitmask()` →
  `broadcastPvpStatusBitmask()` **early-returns on null closeobjects**
  (TangibleObjectImplementation.cpp:385-388) - no deref, no crash.
- **Spawn-time set is sufficient.** Clients receive the bitmask on object
  discovery via `sendBaselinesTo()` → `sendPvpStatusTo()`
  (TangibleObjectImplementation.cpp:160-173). The broadcast no-op only means a
  *live* flip wouldn't reach players already in range - irrelevant since the
  flag is read at startup and applied at spawn.
- **No perf change.** We deliberately do NOT set `pvpBitmask = PLAYER` in the
  creature template: that would leave `closeobjects` allocated for hundreds of
  NPCs (combat-NPC observer behavior). The post-spawn set keeps the current
  optimization intact.
- **Colors follow player rules for free.** Miners are non-attackable neutral →
  blue dot. Purple (same faction) and red (attackable/TEF) will emerge
  automatically for future factioned/overt sim NPCs; TEF-red already confirmed
  working by owner.

### Verify / rollback
- Build (`-Werror`), deploy, **owner restarts**.
- Dashboard: `simNpcPresentation.minersFlagged` == live miner count.
- In-game: miner = blue player-style dot on radar + planetary map; con color
  player-style; examine/radial unchanged; miners still gather (watch
  `minerActivity`/`minerRecovery` for one cycle - expect no change).
- Rollback: set flag false (or leave default), restart. No data migration.

---

## P.4.4b MOUNTED TRAVEL (IMPLEMENTED 2026-07-02, pending restart+verify)

Self-test verified 3/3 mount cycles live (deploys=mounts=dismounts=stores=3,
failures=0), but nobody *traveled* mounted - owner's requirement is "mount like
a player and ride to the destination". Implemented:

- **Ride mechanics (player-faithful, all verified against the player path):**
  - Rider keeps WORLD coordinates; `GroundZoneComponent::updateZone` already
    calls `updateVehiclePosition()` for a rider, dragging the vehicle along
    server-side (position+direction+speed) - reused as-is.
  - The missing client half: AI movement broadcast only ever sent the RIDER's
    transform (vehicle would look frozen). Added a mounted branch to
    `AiAgentImplementation::broadcastNextPositionUpdate` mirroring
    `DataTransform.h::updateTransform`: bump the MOUNT's movement counter and
    broadcast `UpdateTransformMessage` for the MOUNT. Branch is dead code for
    every non-mounted agent (only sim miners can be vehicle-contained AI).
  - Speed: `AiAgent::findNextPosition` reads the raw `runSpeed` member, so the
    controller copies `vehicle->getRunSpeed()` onto the agent at mount and
    restores the pre-mount value at dismount (players get this implicitly via
    the virtual `CreatureObject::getRunSpeed`).
- **Lifecycle (SimMinerController):** `maybeMountForTravel(target)` before the
  three long-leg `moveTo` sites (intelligent activation, starport departure
  run, legacy move) - mounts only when the leg ≥ `mountedTravelMinLegMeters`
  (150 m) via the proven P.4.4a manager deploy+mount. `dismountIfMounted(reason)`
  at EVERY leg exit: stationed-sample start, legacy sample, path failure,
  recovery reset, shuttle boarding (before `switchZone`), and the manager's
  bot-stop path (before the controller map entry is dropped - the dismount
  resolves the agent through it).
- **Locking:** mount/dismount helpers never call the manager with the agent
  locked (the manager takes its own agent+vehicle crosslocks per the 2026-07-02
  postmortem).
- **Config/observability:** `vehicleConfig.enableMountedTravel = true`,
  `mountedTravelMinLegMeters = 150`, `selfTestEnabled = false` (superseded);
  dashboard `vehicleMechanics.mountedTravel{Enabled,MinLegMeters,Legs}`.
- **Verify after restart:** `mountedTravelLegs` climbing; deploys==stores with
  failures flat and activeVehicles matching currently-riding miners; in-game a
  traveling miner is SEATED on a moving swoop at swoop speed and the swoop
  disappears when it stations to sample. Kill switch: `enableMountedTravel=false`.

### 2026-07-02 owner feedback after first live ride (WORKING, one fix added)
Riding verified in-game: slow around city navmesh obstacles (acceptable),
excellent in open terrain. Two observations:
1. **White dot while riding (FIXED, pending restart):** while mounted, the
   map/radar dot players see is the VEHICLE's; a bare vehicle broadcasts
   pvpStatusBitmask=0 → white. Core3 sets no PVP status on player vehicles
   either (checked VehicleControlDevice spawn path) - the client colors a
   player-ridden speeder by rider/status data. Fix: `deployAndMountMinerVehicle`
   now mirrors the rider's faction + pvpStatusBitmask (incl. PLAYER bit) onto
   the transient swoop before zone insert, so it reads as a traveling player
   dot. Vehicle is destroyed at dismount - nothing to restore. If it STILL
   shows white after restart, the client's ridden-vehicle coloring likely keys
   on the rider having a PlayerObject ghost - next lever would be deeper.
2. Unidentified quick visual at dismount (owner couldn't catch it) - watch:
   rider extraction is `zone->transferObject(agent, -1)` + instant vehicle
   destroy; a posture/position pop is plausible. Not yet addressed.

## Part 2 - NPC swoop mount (P.4.4 revival)

### Root cause (corrects the P.4.4 postmortem)
`MountCommand`/P.4.4a do `vehicle->transferObject(agent, PlayerArrangement::RIDER /*=4*/)`;
containmentType ≥ 4 selects **arrangement group `containmentType − 4` = 0 of the
RIDER'S template**. Players have
`arrangementDescriptorFilename = "abstract/slot/arrangement/player.iff"`
(arrangement 0 = `rider`); NPC mobiles have `""` → zero arrangements → transfer
fails. **This field is server-Lua-overridable** (SharedObjectTemplate.cpp:76
loads the named arrangement iff from TRE at template load) - the earlier
"TRE-template-only, out of scope" conclusion was wrong for the server half.
Vehicle-side data (mount_rider slot, saddle/pose datatables) is already complete
and rider-agnostic - no edits there.

### Phase B1 - server Lua arrangement fix + gated self-test (no C++ required)
1. Add to the **server** template block (`object_mobile_dressed_... :new { ... }`,
   NOT the shared block) in each of the six artisan object templates
   (`bin/scripts/object/mobile/`):
   - `dressed_artisan_trainer_01/02/03.lua`
   - `dressed_commoner_artisan_trandoshan_male_01.lua`
   - `dressed_commoner_artisan_sullustan_male_01.lua`
   - `dressed_commoner_artisan_bith_male_01.lua`
   ```lua
   -- Grants the player rider arrangement so this mobile can be slotted into a
   -- vehicle RIDER container (sim miner mount). Server-side only; loaded from
   -- the client's abstract/slot/arrangement/player.iff at startup.
   arrangementDescriptorFilename = "abstract/slot/arrangement/player.iff",
   ```
2. Re-enable the existing gated P.4.4a self-test in `sim_player_manager.lua`:
   `vehicleConfig.enableVehicleMechanics = true`, `selfTestEnabled = true`
   (one stationed miner per interval: create PCD+swoop → deploy at miner →
   mount → hold `selfTestHoldSeconds` → dismount → store/destroy).

**Expected result:** `transferObject` returns true (arrangement 0 `rider`
matches the swoop's `mount_rider` slot), agent parent == vehicle, RIDINGMOUNT
state set; dashboard `vehicleMechanics.mountCount` increments with
`failureCount` flat and **zero** miner-count change.

### Safety analysis
- **The P.4.4a orphan bug cannot recur.** The hardened teardown
  (SimPlayerManager.cpp ~13606-13650) already guarantees: on any mount failure
  the agent is pulled back into the world (re-inserted at its coordinates if it
  ended up zone-less) BEFORE the riderless vehicle is destroyed. This shipped
  after the regression and is the safety net for the whole test.
- **Lock choreography unchanged** - reuses the proven P.4.4a
  device/vehicle/agent crosslocker sequence; no new locking introduced.
- **Arrangement side effects: none identified.** Arrangement descriptors only
  govern how the object itself is slotted INTO containers (containmentType ≥ 4
  transfers). These six templates are shared with world townsperson
  artisans/trainers, but nothing ever calls `transferObject(npc, 4)` on them
  outside our mount path; equipping items INTO NPCs uses their slot descriptors,
  which are untouched. (If owner prefers zero overlap with world NPCs, the
  alternative is cloned `sim_miner_*` object templates + a cloned creature
  template - more moving parts, not needed for the experiment.)
- Data-only, startup-loaded, one-line-per-file revert.

### Phase B2 - client visual check (owner, in-game)
Watch a self-test mount live. The client resolves
`UpdateContainmentMessage(containmentType=4)` against **its own TRE copy** of
`shared_dressed_*.iff`, which has no arrangement. Outcomes:
1. **Seated correctly** (client lenient) → done; skip B3.
2. **Rider standing/floating at the vehicle** (cosmetic mismatch) → Phase B3.
3. Client-side object weirdness/desync on that miner (unlikely; server state is
   consistent regardless) → disable flags, Phase B3.

Animation note: bith/sullustan are non-playable species; if their skeletons
lack the `vehicle_swoop_bike` pose, pin the test to the human
`dressed_artisan_trainer_*` appearances first.

### Phase B3 - contingency: client TRE patch (only if B2 fails)
Edit the SHOT form of the six `object/mobile/shared_dressed_*.iff` in a **new
high-priority patch .tre** for the client: set the same
`arrangementDescriptorFilename = "abstract/slot/arrangement/player.iff"`.
Private server → owner controls client install. Server needs no change (Lua
override already matches, keeping both sides consistent). Optionally narrow to
one template and pin miner appearance for deterministic testing.

### Phase B4 - explicitly out of scope
Driving (P.4.4b): controller must steer the VEHICLE while the agent rides
(speed, checkArrival, dismount-at-target, recovery interplay). Separate design
after mount is proven. Until then the self-test only mounts/holds/dismounts.

---

## Order of operations
1. Part 1 (player dots) - independent, smallest risk, immediate visible win.
2. Part 2 B1+B2 (mount experiment) - Lua-only, next restart window.
3. B3 only if the client visual fails.
Each step: edit via docker-cp channel → incremental `build` (-Werror, only
Part 1 needs C++) → owner restarts → verify dashboard + in-game → update
`docs/ai-miner-navigation-design.md` and memory.

## Open questions for owner
1. Reuse the shared artisan dressed templates for the arrangement line
   (analysis says safe), or clone dedicated sim-miner templates?
2. Player-dot scope: miners only, or all SimPlayerManager-spawned NPCs
   (future crafters/doctors/entertainers) under the same flag?
3. OK to run the mount self-test on the live server during a quiet window
   (it borrows one stationed miner per interval)?

## Verified code anchors (this session)
- `MountCommand.h` → `transferObject(creature, PlayerArrangement::RIDER=4)`;
  `PlayerArrangement.h` (RIDER=4); containment≥4 → arrangement group idx−4.
- `SharedObjectTemplate.cpp:76` - Lua `arrangementDescriptorFilename` honored.
- `bin/scripts/object/creature/player/*.lua:51` - players:
  `pvpStatusBitmask = PLAYER` and `arrangementDescriptorFilename = player.iff`;
  mobiles: `""`/`NONE`.
- `shared_vehicle_base.iff` → `slotDescriptorFilename = mount_rider.iff`.
- `TangibleObjectImplementation.cpp` - sendPvpStatusTo:312 (PLAYER passthrough),
  broadcast:385 (null-closeobjects early return), sendBaselinesTo:160
  (discovery-time pvp send), pet strip:464.
- `AiAgentImplementation.cpp:298-311` - pvp bitmask from creature template;
  `pvpBitmask==0` → `closeobjects=nullptr`.
- `SimPlayerManager.cpp` - spawn hooks ~6047/~20186; P.4.4a self-test + mount
  + hardened teardown ~13590-13800; vehicleConfig parse ~13485.
