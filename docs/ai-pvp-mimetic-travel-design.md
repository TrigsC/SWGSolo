# P.6.5 - Player-Mimetic Routed Travel for PvP Squads

Status: **P.6.5-0 spike VERIFIED LIVE 2026-07-07 (§9.1 - all 11 starports
pathable); P.6.5a ROUTED TRAVEL SHIPPED and LIVE-PARTIAL VERIFIED 2026-07-09**
(compiled clean `-Werror` first try, 15.7s incremental; see §10.1 for retained
dashboard/log evidence and remaining verification gaps).
Owner decisions recorded in §8: P.6.5 runs AHEAD of P.6.4 GCW presence; staging
= rebels Moenia / imperials Bestine; **NO waypoints written to players** (the
groupWaypoints idea is DROPPED - squads communicate by chat only); pacing =
squads move at **player run speed** when players are grouped (jog/run, not
walk - "whatever the player speed is").
Related docs: `ai-pvp-squad-design.md` (P.6 squad system this extends),
`ai-miner-navigation-design.md` (P.4 travel primitives),
`ai-jedi-frs-rank-design.md` (P.7.4, same session's second workstream).

---

## 0. Owner request (2026-07-07)

Current squads feel static ("theed shuttle → theed starport, mos eisley shuttle →
cantina") and travel is fake - a direct `switchZone` to any city regardless of how
players actually get there. Owner wants:

1. **Real travel rules.** Shuttles are intra-planet only; starports do
   inter-planet, and only between CONNECTED planets (Naboo↔Tatooine↔Corellia
   interconnect; Rori is reachable only from Naboo). An unconnected destination
   requires a **hop** through an intermediate planet/city.
2. **Player-style tactics.** Real groups stage somewhere (e.g. rebels at Moenia),
   ticket to a city, run shuttle→starport hunting red dots, loiter to bait the
   enemy, then move on - and they do NOT drop directly into a contested
   starport; they arrive one city away (e.g. Kor Vella) and take the intra-planet
   shuttle into the target city (Coronet shuttle B), then run in.
3. **Grouped players must be able to follow.** Today a grouped player has no idea
   where the squad is heading and can't keep up when it sprints back to the
   shuttle. Squad must communicate its route and pace itself / wait.
4. Starport boarding realism will need interior pathing ("navmesh for starports")
   - most starport interiors share the same layout except Dantooine/Lok/Rori/
   Dathomir, which squads never need to enter (main PvP planets are Naboo,
   Tatooine, Corellia).

---

## 1. Current state

- `boardPvpSquad` (`SimPlayerManager.cpp:21365`): picks a **random** entry from the
  flat `allShuttleports` list (any planet), avoids same-faction-occupied cities,
  then `switchZone`s the whole squad **directly** to that city's pad. Zero
  connectivity rules, zero legs, zero ticketing mimicry. Convergence (P.6.2)
  overrides the pick but still teleports direct.
- Phase machine (`SimPvPController`): TO_HANGOUT → LOITERING → TO_SHUTTLE →
  AWAITING_SHUTTLE → board. The run back to the pad exists; there is no notion of
  WHERE the shuttle goes or of multi-leg journeys.
- City pool is only 4 cities (moenia, theed, coronet, mos_eisley) in lua
  `shuttleports`.
- Comms (P.6.3a/b/c) are live and verified: spatial announcements, GCW faction
  rooms, join-a-squad groups with group chat. Dashboard 2026-07-07: 312
  announcements, 224 room posts, 1 player join, groups working. **All the
  communication plumbing this design needs already exists.**
- Grouped-player experience gaps: no destination announcement with route, no
  waypoints, leader runs at ~4.8 m/s, squad boards (teleports) regardless of
  where the grouped player is, and never waits on the arrival side.

## 2. Engine facts (verified 2026-07-07, file:line)

- **`PlanetTravelPoint`** (`managers/planet/PlanetTravelPoint.h:18`): per-point
  `pointZone`, `pointName`, `arrivalVector`, `departureVector` (kept equal to the
  live shuttle/transport object's world position via `setShuttle`, :95),
  `interplanetaryTravelAllowed`, `incomingTravelAllowed`. `isInterplanetary()`
  :158 = "this is a starport". So **pad positions come from live game data - no
  hand-typed coordinates needed** for transit cities.
- **`PlanetTravelPointList`** (`PlanetTravelPointList.h:13`) is an enumerable
  `VectorMap<String, Reference<PlanetTravelPoint*>>` + ReadWriteLock; held
  `protected` on `PlanetManager` (`PlanetManager.idl:50`). Public lookups that
  already exist: `getPlanetTravelPoint(name)` (idl:413),
  `getNearestPlanetTravelPoint(position, range, interplanetaryOnly)` (idl:210).
- **Travel legality** (`PlanetManagerImplementation::isTravelToLocationPermitted`
  :660): same zone → **always allowed** (any point to any point = the
  intra-planet shuttle rule); cross-planet → **both** endpoints must have
  `interplanetaryTravelAllowed` (the starport rule).
- **Planet connectivity** = the client fare datatable. `loadTravelFares` (:485)
  reads `datatables/travel/travel.iff` into a symmetric planet×planet matrix;
  `getTravelFare(dep, arr)` (:528, exposed `PlanetManager.idl:261`). **Fare > 0 ⇒
  a purchasable route exists.** This is the exact data that encodes
  "Naboo↔Tatooine↔Corellia, Rori only via Naboo" - the NPC router reads the same
  truth players see in the ticket terminal. (Spike P.6.5-0 dumps the matrix to
  confirm the convention on OUR tre set before we depend on it.)
- **Interior pathing**: building interiors do NOT use the outdoor recast navmesh;
  they path via building floor-mesh path graphs with world↔cell transitions -
  `PathFinderManager::findPathFromWorldToCell` (`PathFinderManager.h:79`) and
  siblings. So "navmesh for starports" is likely NOT a navmesh build at all: the
  pipeline already exists (indoor NPCs use it), and the open question is only
  whether the street→ticket-collector transition resolves reliably at starports.
  Spiked before any behavior depends on it (§5, phase 0).
- **Teleport choreography** (P.6.1a/b/d lessons, non-negotiable at EVERY leg):
  `prepareForRelocation()` + agent-locked `setMovementState(OBLIVIOUS)` →
  `clearPatrolPoints()` → `clearSavedPatrolPoints()` → `clearCurrentPath()` →
  `switchZone(zone, X, Z, Y, 0)` (parentID 0 = outdoor arrival).
- **Player waypoints**: the mission system sets named waypoints on a player's
  ghost (WaypointObject via PlayerObject); squads can reuse that pattern for
  "Squad: Theed Starport" markers. Exact API confirmed at build time (verify
  step, low risk - heavily used path).

## 3. Architecture

### 3.1 TravelGraph (manager-side, read-only game data)

Nodes = the configured city set (lua `shuttleports`, expanded - §6), each resolved
at load to its `PlanetTravelPoint` by `travelPointName` (exact match via
`getPlanetTravelPoint`) or by `getNearestPlanetTravelPoint(spawnPos)` as fallback;
cached with `pointZone`, arrival position, `isInterplanetary()`.

Edges:
- **shuttle edge**: any two configured points on the SAME zone (mirrors
  `isTravelToLocationPermitted` same-zone rule).
- **starport edge**: two points on different zones where both
  `isInterplanetary()` AND `getTravelFare(zoneA, zoneB) > 0`.

Routing = BFS, minimum legs, from current city to destination city. Examples this
produces naturally:
- moenia → theed: 1 shuttle leg.
- theed → mos_eisley: theed(starport) → mos_eisley(starport), 1 starport leg
  (naboo↔tatooine connected).
- theed → restuss (rori) from tatooine: tatooine → naboo starport → rori
  (2 starport legs - the hop, straight from the fare matrix).
- **Tactical arrival** (`avoidHotArrival`): if the destination city is "hot"
  (enemy squad present, or an unexpired enemy contact reported there), the router
  retargets the final starport leg to a DIFFERENT same-planet city and appends an
  intra-planet shuttle leg into the destination - the owner's
  "Kor Vella, then Coronet shuttle B" behavior, emergent from data.

Zero core changes needed for the graph: config names + existing public lookups.
(If we later want ALL travel points instead of configured cities, add one tiny
additive accessor on PlanetManager - not needed for v1.)

### 3.2 Route legs in the squad

`SimPvpSquad` gains `Vector<PvpTravelLeg> pendingRoute` (each leg: depZone,
depPointName, depBoardingPos, arrZone, arrPointName, arrPos, legType
shuttle|starport) + `routeDestCity` (guarded by `pvpSquadMutex`, same discipline:
never held while locking agents).

`boardPvpSquad` becomes: (1) if `pendingRoute` empty → plan a route (convergence
destination still wins, then spread-out pick from the pool); (2) pop the next
leg; (3) existing halt+switchZone choreography to the leg's ARRIVAL point;
(4) if more legs remain → controller enters TO_SHUTTLE toward the NEXT leg's
departure boarding point (usually meters away - the port they just landed at) →
AWAITING_SHUTTLE dwell (legDwellSeconds, feels like waiting for the ship) →
board again; (5) route empty → TO_HANGOUT as today. Every leg keeps every
existing watchdog (stateTtl, board-anyway, hard-stuck, phantom-combat) - a
failed leg degrades to "board anyway from here", never a wedge.

### 3.3 Starport boarding realism (P.6.5b)

For starport legs the run target (`depBoardingPos`) is, in order of preference:
1. the ticket-collector position INSIDE the port (if the phase-0 spike proves
   street→cell pathing reliable at Naboo/Tatooine/Corellia-style ports) - full
   player mimicry;
2. else a configured `boardingPoint` at the starport ENTRANCE (outdoor, on
   navmesh) - still reads as "went to the starport and boarded".
Arrivals stay OUTDOOR always (parentID 0) - unchanged owner constraint.
Optional knob `boardOnActualShuttle`: gate the final dwell on
`checkShuttleStatus` (legacy `isNearestShuttleBoardable` pattern,
`SimPlayerManager.cpp:20739` legacy trace) so squads visibly leave "when the
ship is in".

### 3.4 Group-aware pacing + comms (P.6.5c - the player-facing heart)

All player communication reuses P.6.3 plumbing (announcePvpEvent, group chat,
faction rooms) - new event types only:

- **MOVEOUT announce** on route selection, with the actual route:
  "Moving out - shuttle to Theed, then the starport to Coronet." Spatial + group
  chat + faction room (faction room already carries city context).
- **BOARDING announce** per leg: "Boarding for Coronet - last call!"
- ~~Group waypoints~~ **DROPPED (owner decision 2026-07-07)**: no waypoints are
  ever written to players. The squad communicates destinations exclusively via
  spatial/group/faction-room chat, which must therefore always name the full
  route with planet + city ("shuttle to Theed, then the Theed Spaceport to
  Coronet, Corellia") so a player can buy the same ticket. This also removes
  the design's only player-state write - the sim now touches players by chat
  and group membership only.
- **Pacing** (`groupPacing`, active only while the squad group contains ≥1
  player) - owner decision: **player run speed**, not walk:
  - leader (and thus the FOLLOWing members) moves at the player base RUN speed
    (~5.376 m/s; bots currently do ~4.8) so a running player paces the squad
    naturally - clamp, don't boost: squads never move faster than a player can;
  - **hold at departure**: in AWAITING_SHUTTLE, if a grouped player is farther
    than `holdRadiusMeters` (64) from the boarding point, extend the dwell up to
    `departureHoldMaxSeconds` (120), announcing "Shuttle's here - waiting on
    you" at intervals; then board regardless (never let a player hold a squad
    hostage forever);
  - **arrival-side wait**: players are NEVER teleported - they ride the real
    ticket system. After a leg that left grouped players behind, the squad
    dwells `arrivalHoldSeconds` (90) at the arrival pad and announces "We're at
    the Coronet shuttleport - grab a ticket and catch up" before running to the
    hangout. Chat announces at arrival repeat the current city + hangout so a
    catching-up player knows where to head.

### 3.5 Staging + destination dynamism (P.6.5a)

- Per-faction **staging city** (`travel.staging`): initial squad spawns and
  full-wipe reforms happen there (rebels e.g. moenia, imperials e.g. bestine) -
  players learn where to find their faction's squads forming up.
- Destination pick: unchanged priority (convergence first), then spread-out
  random weighted to `mainPlanets` (naboo/corellia/tatooine), now over an
  expanded city pool. Transit-only cities (no hangout defined) are used for hops
  but never as loiter destinations.

## 4. Config (lua `pvpConfig.travel`, C++ defaults ALL off)

```lua
travel = {
    enableRoutedTravel = false,   -- master gate; off = today's direct teleport
    mainPlanets = { "naboo", "corellia", "tatooine" },
    maxLegsPerRoute = 3,
    legDwellSecondsMin = 20, legDwellSecondsMax = 45,
    avoidHotArrival = true,
    boardOnActualShuttle = false,
    staging = {   -- owner-approved 2026-07-07
        rebel = { planet = "naboo", city = "moenia" },
        imperial = { planet = "tatooine", city = "bestine" },
    },
    groupPacing = {
        enabled = true,
        matchPlayerRunSpeed = true,   -- owner: jog/run at player speed
        holdRadiusMeters = 64,
        departureHoldMaxSeconds = 120,
        arrivalHoldSeconds = 90,
    },
},
comms += { announceRoutes = true }   -- groupWaypoints DROPPED (owner)
```

`shuttleports` entries gain optional `travelPointName` (exact PlanetTravelPoint
name), `boardingPoint = {x,y,z}` (starport entrance/collector), `starport = true`,
`transitOnly = true` (no hangout → never a loiter destination). New cities to add
(owner pick, §8): kor_vella, tyrena (corellia); bestine, mos_entha (tatooine);
keren, kaadara (naboo); restuss (rori, transit target).

## 5. Phases (each: build clean `-Werror`, owner restarts, dashboard verify)

- **P.6.5-0 SPIKE (diagnostics only, zero behavior change).**
  (a) Behind a flag, at boot log the full travel graph: every PlanetTravelPoint
  per active zone (name / interplanetary / arrival pos) + the fare matrix for all
  zone pairs → confirms the connectivity convention (fare>0) matches the owner's
  stated topology on OUR tre set. Also gives exact pad coordinates for the new
  city configs for free.
  (b) Interior-path spike: a dashboard-triggered one-shot task walks ONE test bot
  from the street to the ticket collector inside theed + coronet + mos_eisley
  starports; logs pathable yes/no per port. Decides §3.3 option 1 vs 2.
  DISCIPLINE: instrument first (P.6.1c lesson) - no travel behavior ships until
  the graph dump + interior verdict are read.
- **P.6.5a Routed travel core**: TravelGraph + BFS router, multi-leg
  boarding, expanded city pool, staging, MOVEOUT/BOARDING announces, dashboard
  `pvpTravel` (per-squad current route + legs executed / hops / holds counters).
- **P.6.5b Starport realism**: boarding-point runs (interior or entrance per
  spike), optional shuttle-status dwell, avoidHotArrival tactical arrival.
- **P.6.5c Group pacing**: waypoints, walk-when-grouped, departure hold,
  arrival-side wait.

## 6. Dashboard additions (`pvpTravel`)

Per-squad: routeDest, legsRemaining, currentLeg (dep→arr, type), groupedPlayers,
holdingForPlayer. Counters: routesPlanned, legsExecuted, hopLegs,
tacticalArrivals, departureHolds, arrivalHolds, interiorBoardings,
entranceFallbacks.

## 7. Safety analysis

- **Simulation-only preserved**: no tickets bought, no credits, no inventory -
  fare/travel data is read-only; the teleport primitive is the proven switchZone.
- **Per-leg failure containment**: every existing watchdog applies per leg;
  worst case per leg = board-anyway (today's behavior). `maxLegsPerRoute` caps
  journey length; a route that can't be planned falls back to the current
  direct pick (flag-off path == today's code path).
- **Zero player-state writes** (waypoints dropped by owner decision): the sim
  touches players via chat and group membership only - no new mutation surface
  at all.
- **Players are never repositioned.** Holds are time-capped so players can't
  stall squads indefinitely (grief-cap).
- **Locking**: graph snapshot copies primitives out of PlanetTravelPointList
  under its own RW lock, held across nothing else; route state lives in
  SimPvpSquad under `pvpSquadMutex` (existing rule: never held while locking
  agents); waypoint writes lock only the player, mirroring mission code.
- **Interior pathing risk** is contained by spike-first + entrance fallback +
  arrivals always outdoor.
- **Reversible**: `enableRoutedTravel=false` at runtime (30s config refresh)
  restores exactly today's behavior.

## 8. Owner decisions (ANSWERED 2026-07-07)

1. Phase order approved; **P.6.5 runs ahead of P.6.4 GCW base presence.**
2. Staging: **rebels Moenia (naboo), imperials Bestine (tatooine).** City pool:
   proposed additions stand (kor_vella, tyrena on corellia; bestine, mos_entha
   on tatooine; keren, kaadara on naboo; restuss as rori transit) - spike
   output provides exact pad coords; owner can trim the list at P.6.5a config
   review.
3. **NO waypoints for players** - chat-only communication. (Removes the
   design's only player-state write.)
4. Pacing: **player run speed** ("jog/run pace whatever the player speed is")
   - clamp squad movement to the player base run speed when players are
   grouped; never walk-crawl, never outrun.
5. avoidHotArrival: shipped default per design (on) - owner did not object;
   runtime-flippable.

## 9. P.6.5-0 spike - SHIPPED (2026-07-07, compiled clean `-Werror` first try,
18.7s incremental, PENDING RESTART)

As-built (all in `SimPlayerManager` + lua; read-only, one-shot per boot on the
PvP maintenance thread, moves no agents, safe to leave enabled):
- Config: `pvpConfig.travel.diagnostics { dumpTravelGraph=true,
  testStarportInteriorPaths=true, interiorPathPoints={11 starports on naboo/
  corellia/tatooine/rori} }` (C++ defaults false). Exact travel point names
  taken from `scripts/managers/planet/planet_manager.lua` - beware: it's
  "Theed Spaceport", and Moenia's starport point is named just "Moenia".
- `runPvpTravelDiagnosticsIfNeeded()` (guarded once-per-boot under
  pvpSquadMutex) called from `runPvpMaintenanceTask` after
  `reconcilePvpSquadGroups()`; `runPvpTravelDiagnostics()` does the work and
  stores results under pvpSquadMutex.
- Fare dump: pairwise `getTravelFare` over the 10 ground zones (matrix is
  symmetric) → `SimPvpTravelSpike fare naboo<->tatooine fare=N connected=bool`.
- Nearest-starport resolution: `getNearestPlanetTravelPoint(cityPos, 16000,
  interplanetaryOnly=true)` for every configured shuttleport city →
  `SimPvpTravelSpike nearestStarport <planet>/<city> -> "<point>" dist=N`.
- Interior test per configured starport: octree scan 175m around the point's
  arrival position (`getInRangeObjects`), collecting TICKETCOLLECTOR objects
  both top-level AND inside building cells (cells keyed 1..totalCellNumber;
  collectors are static snapshot objects - read-only scan); nearest collector
  → `PathFinderManager::findPath(street arrival point → WorldCoordinates(
  collector))` in try/catch → status ok|noCollector|pathFailed|exception,
  pathNodes, collectorInCell. Coordinate handling copies the proven
  P.4.5a setX/setY/setZ pattern (never positional Vector3 ctor).
- Dashboard: `pvpActivity.travelDiagnostics { dumpTravelGraph,
  testStarportInteriorPaths, ran, fares[], nearestStarports[],
  interiorPaths[{zone,point,status,collectorTemplate,collectorInCell,
  pathable,pathNodes,collectorX/Y/Z}] }`.

### 9.1 Spike results - VERIFIED LIVE 2026-07-07

- **Fare matrix confirms the owner's topology exactly** (fare>0 = connected):
  naboo↔tatooine 500, naboo↔corellia 500, corellia↔tatooine 600 (the main
  triangle); **rori only from naboo (300)**; corellia is the hub for dantooine
  (1000), dathomir (2000), endor (4000), yavin4 (3000), talus (300); naboo
  additionally serves endor (1750) + lok (1250); lok also from tatooine (1250).
  (Dathomir-via-corellia also matters for the P.4.5b miner dispatch.)
- `nearestStarports` all sane (moenia→"Moenia" 315m, theed→"Theed Spaceport"
  574m, coronet→"Coronet Starport" 283m, mos_eisley→"Mos Eisley Starport" 228m).
- **Interior paths: ALL 11 ports status=ok.** Key finding: the NEAREST ticket
  collector is OUTDOORS at 10/11 ports (3–6 path nodes) - the boarding run is
  trivially safe almost everywhere; and **Theed Spaceport's collector is inside
  a cell and pathed fine (22 nodes)** - the world→cell pipeline works, so
  P.6.5b can use collector runs with no navmesh work at all.
- Data caution: **Kaadara Starport's travel point z=-192 in the planet data**
  (under-the-world quirk; its collector reports the same) → kaadara EXCLUDED
  from the v1 city pool; P.6.5b should sanity-check collector z vs terrain.

## 10. P.6.5a routed travel - SHIPPED (2026-07-07, compiled clean `-Werror`
first try, 15.7s incremental; live-partial verified 2026-07-09)

As-built (all flag-gated by `pvpConfig.travel.enableRoutedTravel`, C++ default
OFF; lua ships ON; runtime-flippable via the 30s config refresh - off restores
exactly the legacy direct-teleport pick):

- **City pool** (`shuttleports` + new per-city `starport` = exact
  PlanetTravelPoint name): naboo moenia/theed/keren; corellia coronet/
  kor_vella/tyrena; tatooine mos_eisley/bestine/mos_entha; rori restuss
  (off-main → exercises the naboo hop). New city spawns = starport pads,
  hangouts = near their (spike-verified outdoor) ticket collectors. NOTE:
  miners also spawn across this list now (incl. restuss).
- **Router** (`planPvpRoute`): resolves each city's starport via
  `getPlanetTravelPoint` + planet connectivity via `getTravelFare` OUTSIDE the
  squad mutex; then under `pvpSquadMutex`: destination pick (unexpired
  convergence stamp wins and is consumed; else spread-out random with
  `offMainPlanetChancePct` bias toward `mainPlanets`, same-faction-occupancy
  avoidance), BFS shortest-hop over city adjacency (same planet = intra leg;
  cross-planet = both starports + fare>0), `maxLegsPerRoute` cap, legs stored
  on the squad. Failure at any step → `pvpRouteFallbacksTotal++` + legacy pick
  (never wedges).
- **Multi-leg boarding** (`boardPvpSquad`): pops one leg per boarding
  (`popNextPvpRouteLeg`; a FRESH convergence stamp drops the remaining route →
  replan straight to the contact). Every leg keeps the full proven teleport
  choreography. Final leg → `beginCityLoop` at the city's hangout; non-final →
  **`SimPvPController::beginTransitStop`** (new): hangout=shuttle=pad,
  transit dwell `transitDwellSecondsMin..Max` (20–45s), arrival/departure
  announces suppressed, then the normal AWAITING_SHUTTLE →
  `runPvpShuttleWaitTask` boards the next leg (still waits for a real ship via
  `isNearestShuttleBoardable`, board-anyway cap unchanged).
- **MOVEOUT route callout**: on planning, `announcePvpEvent(squadId,
  PVP_ANNOUNCE_MOVEOUT, "Route: kor vella (corellia), then coronet
  (corellia).")` - spatial + faction room ("Departing <city> - <route>") +
  squad group chat; **bypasses the per-squad 45s cooldown** (would otherwise
  be swallowed by the DEPARTURE shout seconds earlier), global 4s gap kept.
- **Staging**: `spawnPvpSquad` places new/reformed squads at the faction
  staging city (rebels moenia, imperials bestine) instead of a random city.
- **Dashboard**: `pvpActivity.routedTravel` (config + routesPlannedTotal /
  routeLegsExecutedTotal / hopRoutesTotal / transitStopsTotal /
  fallbacksTotal) + per-squad `routeDest` / `routeLegsRemaining`.
- Logs: `SimPvpRoutePlanned squad= from= dest= legs= convergence=`;
  `SimPvpSquadTraveled ... routed= transit= legsRemaining=`.

### Verify after restart
1. Squads spawn at moenia (rebels) / bestine (imperials); dashboard
   `routedTravel.enabled=true`.
2. `routesPlannedTotal` climbs with travels; `fallbacksTotal` ≈ 0.
3. A restuss route from tatooine/corellia shows `legs=2` in SimPvpRoutePlanned
   and `hopRoutesTotal`/`transitStopsTotal` > 0; squad row shows
   `routeDest=rori:restuss`, `routeLegsRemaining=1` mid-journey, and the squad
   visibly waits ~20–45s at the naboo connection pad then boards on.
4. MOVEOUT lines audible in spatial + posted in GCW rooms with the route text.
5. Convergence still works (`SquadTraveled convergence=true`) and a mid-route
   convergence replans (route dropped, new SimPvpRoutePlanned convergence=true).
6. No stateTtl/hardStuck regression vs the pre-P.6.5a baseline; scouts travel
   routed too.
7. Miners: confirm the new cities didn't break miner spawn placement (they now
   spread across 10 cities incl. restuss).

### 10.1 Live verification snapshot - 2026-07-09

Dashboard snapshot from `swgemu_server` at `2026-07-09T00:36:55Z`:

- **Routed travel enabled and moving:** `routedTravel.enabled=true`,
  `routesPlannedTotal=461`, `routeLegsExecutedTotal=473`,
  `fallbacksTotal=0`, `hopRoutesTotal=15`, `transitStopsTotal=15`,
  `stagingRebel=naboo:moenia`, `stagingImperial=tatooine:bestine`,
  `transitDwellSecondsMin=20`, `transitDwellSecondsMax=45`.
- **Counters climbed during verification:** between two live snapshots,
  `routesPlannedTotal` advanced `460 -> 461`, `routeLegsExecutedTotal`
  advanced `472 -> 473`, `announcementsTotal` advanced `975 -> 977`, and
  `factionRoomPostsTotal` advanced `558 -> 559`.
- **Retained route logs confirm fresh routed travel:** current `core3.log`
  contains 19 recent `SimPvpRoutePlanned` / `SimPvpSquadTraveled` pairs after
  log rotation, all with `routed=true` and `fallbacksTotal=0` on the dashboard.
- **MOVEOUT route text path is live by code and counters:** `planPvpRoute`
  builds the route summary and calls `announcePvpEvent(...,
  PVP_ANNOUNCE_MOVEOUT, routeSummary)`; `announcePvpEvent` appends that detail
  to spatial chat and posts `Departing <city> - <route>` to the faction room.
  The live dashboard counters above show announcements and room posts advancing
  with route planning. Retained logs do not record chat message bodies, so
  actual audible spatial text still needs an in-client confirmation.
- **Restuss / two-leg route evidence:** dashboard `hopRoutesTotal=15` and
  `transitStopsTotal=15` prove multi-leg routes and connection stops have
  executed with zero fallbacks. Because the v1 city pool's only off-main city is
  `rori:restuss`, and Rori is reachable only through Naboo, these hop counters
  are strong evidence that Restuss routes occurred. However, the current retained
  `core3.log` segment has no `dest=rori:restuss legs=2` line and no
  `transit=true` line; direct log proof was not retained.
- **Staging:** dashboard config reports the correct staging cities, and retained
  `SimPvpSquadSpawned` logs show rebel reformed patrols spawning at
  `naboo:moenia`. No imperial re-spawn occurred in the retained log window, so
  `tatooine:bestine` is config/code-verified but not directly observed in the
  retained runtime logs.
- **Convergence / scouts:** not verified in this snapshot. Dashboard
  `pvpActivity.scouts.enabled=false`, `contactsReportedTotal=0`, and
  `convergencesTotal=0`; retained logs contain no `convergence=true`
  travel/replan lines. Re-enable scouts or force a contact before closing this
  checklist item.
- **Stuck / phase health:** dashboard `recoveryActionsTotal=0`; retained logs
  have no hard-stuck lines. The retained window does contain two
  `reason=stateTtl` force-advances and several `pathFailStreakExhausted`
  phase advances, so compare against the pre-P.6.5a baseline before marking
  "no stateTtl regression" fully closed.
- **Miners:** live dashboard shows `activeMiners=10`, `pathFailures=0`,
  `movementArrivalTimeoutCount=0`, `sampleTimeoutCount=0`, and
  `configuredMinerSpawnZones=naboo,tatooine,corellia,rori`. Current active
  miner zones are corellia/naboo/tatooine with `rori` configured but
  `activeMiners=0`, so Restuss spawn placement is config-verified but not
  directly proven by the current live rows.
