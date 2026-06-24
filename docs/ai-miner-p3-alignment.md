# P.3.0 End-State AI Miner Alignment and Coverage Model

This phase pauses feature work and aligns the existing AI economy pieces around the intended end-state behavior. It does not propose new economy mutations, real extraction, inventory changes, vendor changes, crafting changes, market changes, credit flows, or persistence writes.

The target is a small AI miner population that behaves as a coordinated coverage system. Individual miners are implementation details. The economy owns prioritization, coverage, assignment, and rebalance decisions.

## North Star

The primary optimization target is coverage stability.

The system should prefer:

- Stable profile/resource coverage.
- Long assignment duration once a useful target is reached.
- Repeated conceptual sampling while stationed.
- Rebalance only when demand, supply, resource quality, or resource lifecycle materially changes.

The system should not optimize for:

- More movement.
- More path attempts.
- More candidate generation.
- More assignment churn.
- More activation count.
- More target churn.

## End-State Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Unassigned
    Unassigned --> Assigned: coverage need selected
    Assigned --> Validating: target identity chosen
    Validating --> Assigned: validation pending or retry
    Validating --> Moving: verified reachable target
    Moving --> Stationed: arrived at target
    Stationed --> Sampling: sample tick
    Sampling --> Stationed: conceptual yield recorded
    Stationed --> RebalanceReview: periodic coverage review
    RebalanceReview --> Stationed: coverage still desired
    RebalanceReview --> Assigned: better coverage target
    Stationed --> Assigned: resource despawned or obsolete
    Stationed --> Unassigned: miner removed
```

Expected mature distribution:

| State | Expected role | Healthy share |
| --- | --- | --- |
| `stationed` / `covering` | Dominant steady state | 80-95% |
| `moving` | Relocation after rebalance or first assignment | 0-20% |
| `sampling` | Short recurring action inside stationed coverage | Bursty/transient |
| `unassigned` / idle | No valid coverage need or bootstrap gap | Very low |
| `candidate`, `validated`, `queued` | Planning and activation internals | Transient only |

## Coverage Architecture

```mermaid
flowchart TD
    ResourceSnapshot["Active resource spawns"] --> OpportunityScoring["Resource opportunity scoring"]
    DemandModel["Demand profiles and pressure"] --> CoveragePlanner["Coverage planner"]
    StockpileState["Conceptual and future durable stockpiles"] --> CoveragePlanner
    OpportunityScoring --> CoveragePlanner
    CoveragePlanner --> DesiredCoverage["Desired coverage by profile/resource/zone"]
    DesiredCoverage --> AssignmentReconciler["Assignment reconciler"]
    AssignmentReconciler --> ReachabilityGate["Reachability and path gate"]
    ReachabilityGate --> MinerControllers["Miner controllers"]
    MinerControllers --> StationedCoverage["Stationed coverage and repeated sampling"]
    StationedCoverage --> CoverageTelemetry["Coverage duration, sample continuity, yield rate"]
    CoverageTelemetry --> CoveragePlanner
    StationedCoverage --> ReachabilityMemory["Reachability memory"]
```

Key architectural shift: the planner should eventually ask, "What coverage do we need and what coverage do we already have?" before asking, "Can this miner generate a new candidate?"

## Current System vs Intended System

| Area | Current behavior | Intended behavior |
| --- | --- | --- |
| Planner | Demand-weighted plans select per-miner targets and cap profiles with `maxMinersPerProfile`. | Coverage planner emits desired coverage counts per need and target. |
| Assignment identity | Assignments are keyed to miner target hashes and lifecycle state. | Assignments remain tied to coverage slots until a rebalance condition invalidates them. |
| Lifecycle | `candidate -> validated -> queued -> activation_started -> sample_started -> sample_complete`, then assignment commonly clears. | `assigned -> validating -> moving -> stationed/covering -> repeated sampling`, with assignment retained. |
| Movement | Activation success and path validation are highly visible metrics. | Movement is a means to reach coverage, not a success metric. |
| Sampling | Conceptual sample completion is recorded as an event/yield. | Repeated sample continuity while stationed is a core health metric. |
| Reachability memory | Mostly learns which candidates validate, reject, activate, or sample. | Learns which resource-location buckets produce sustained coverage. |
| Dashboard | Rich development telemetry for path, activation, reachability, candidate, and blocker diagnosis. | First viewport should summarize coverage stability, stationed share, uncovered needs, rebalance pressure, and assignment age. |
| Stockpile | Conceptual totals and optional persistent baseline are diagnostics. | Stockpile levels drive desired coverage and rebalance thresholds. |
| Safety | Strong conceptual-only boundaries are present. | Keep these boundaries until explicit economy mutation phases are approved. |

## Lifecycle State Audit

| State | Current meaning | Final-design role | Classification |
| --- | --- | --- | --- |
| `candidate` | Assignment exists but has not matched verified path validation. | Internal planning state only. | Temporary/transient |
| `validated` | Assignment has verified path snapshot and identity match. | Internal gate before first movement. | Temporary/transient |
| `queued` | Controller accepted a movement request but has not started it. | Internal controller queue. | Temporary/transient |
| `activation_started` | Miner is moving toward target. | Real relocation state, but not primary success. | Transient |
| `sample_started` | Miner is sampling at/near target. | Sampling tick inside stationed coverage. | Transient, should not replace coverage state |
| `sample_complete` | Intelligent sample completed. | Event, not a long-lived state. | Event/temporary |
| Proposed `stationed` | Miner arrived and remains assigned to a coverage slot. | Dominant steady state. | Permanent |
| Proposed `covering` | Miner is counted toward desired coverage for a need. | Coverage planner state; may include stationed or active sampling. | Permanent |
| Proposed `assigned_active` | Assignment is live and retained beyond activation. | Bridge state if `stationed` and `covering` are split. | Likely permanent or transitional |

Recommendation: add a long-lived assignment state such as `stationed` or `covering` before implementing more movement improvements. The current lifecycle can report short actions, but it lacks a state that says "this miner is successfully doing the final job."

## Dashboard Metric Classification

Permanent metrics should remain first-class in the end-state dashboard. Temporary metrics are useful during development and should move lower or into diagnostics. Misleading metrics are valid facts but can encourage the wrong behavior if promoted.

| Panel or metric | End-state success? | Risk | Classification | Alignment recommendation |
| --- | --- | --- | --- | --- |
| Active Miners | Yes, as population baseline. | None if paired with stationed/coverage count. | Permanent | Keep. |
| AI Population Idle | Partly. | Misleading until `stationed` exists; miners can be conceptually sampling but "idle" in assignment lane. | Misleading now | Redefine as no coverage assignment, not no active activation. |
| Assigned Miners | Yes. | Too broad without coverage target. | Permanent | Split into assigned, stationed, covering. |
| Travel Plans / Remote Priority | Future capability. | Can imply movement is desired. | Temporary | Keep diagnostic until actual travel exists. |
| Economy Health status | Yes. | Needs coverage-first status. | Permanent | Reframe recommendation around coverage stability. |
| Top Opportunities / Uncovered | Yes. | Good coverage signal if "uncovered" maps to desired coverage gaps. | Permanent | Promote. |
| Recent Yields | Partly. | Recent event volume can reward churn. | Temporary/permanent hybrid | Convert to yield continuity by stationed assignment. |
| Aware Quantity / Stockpile | Yes. | Good if tied to desired reserve thresholds. | Permanent | Keep and connect to coverage goals. |
| Path Failed / Blocked by Path | Development health. | Over-emphasizes pathfinding as goal. | Temporary | Move to diagnostics after stable coverage exists. |
| Intelligent Active | No, not final success. | Encourages activation count and movement. | Misleading if primary | Replace with Stationed/Covering Active. |
| Queued / Moving / Sampling bars | Sampling and moving are useful context. | Can encourage action churn. | Temporary | Move below coverage overview. Add Stationed bar. |
| Candidate count | No. | Candidate growth looks like progress but may be churn. | Temporary/misleading | Keep only in diagnostics. |
| Validation count | Gate health only. | Can over-reward validation throughput. | Temporary | Keep as path gate diagnostic. |
| Direct fallback count | No. | Useful for path debugging only. | Temporary | Keep in path diagnostics. |
| Movement readiness | Development gate health. | Too activation-centric for final dashboard. | Temporary | Keep until `stationed` exists. |
| Supply Session/Known | Yes. | Needs distinction between conceptual and future authoritative supply. | Permanent | Keep with safety labels. |
| AI Stockpile | Yes. | Good basis for coverage planning. | Permanent | Promote once coverage planner consumes it directly. |
| Resource-Aware Stockpile | Yes. | Strong bridge between resource identity and conceptual yield. | Permanent | Keep; add coverage source assignment id. |
| Demand profiles | Yes. | Currently pressure-oriented but useful. | Permanent | Add desired coverage, actual coverage, station duration. |
| Resource Scout opportunities | Yes. | Good opportunity input. | Permanent | Keep as source data, not outcome metric. |
| Resource Coverage | Yes. | Closest current panel to end-state success. | Permanent | Promote to first-class headline. |
| Coverage Alignment | Yes. | Strong audit of assignment-to-opportunity fit. | Permanent | Keep and extend with coverage slots. |
| Path Validation | Gate only. | Over-emphasizes path attempts and failures. | Temporary | Demote after stable stationed state is implemented. |
| Reachability Calibration | Development learning. | Candidate-centric. | Temporary | Reorient around sustained coverage outcomes. |
| Reachability Memory | Potentially permanent. | Currently candidate-centric. | Temporary until reweighted | Add station duration, sample ticks, resource lifecycle outcome. |
| NavArea Density | Development feature staging. | Can encourage new target generation. | Temporary | Keep diagnostic-only until proven to improve coverage duration. |
| Safety Boundaries | Yes. | Critical guardrail. | Permanent | Keep visible. |
| Live Assignments | Yes if redefined. | Current statuses are transient-heavy. | Permanent with changes | Show coverage state, target, age, rebalance reason. |
| Assignment History | Development and audit. | Useful for diagnosing churn. | Temporary/permanent hybrid | Add churn rate and clear reason totals. |

## Pathfinding Alignment

Pathfinding should be treated as a gate to coverage, not an objective.

Current dashboard and logs expose path validation outcomes, direct fallback counts, path failures, mismatch reasons, and reachability buckets. These are valuable while bootstrapping the system, but they should not sit above coverage outcomes in the mature dashboard.

Recommended pathfinding success metrics:

- Percent of desired coverage slots with verified reachable stationed miners.
- Median time from coverage need creation to stationed miner.
- Rebalance movement success rate.
- Stationed duration after verified path.
- Path failures per rebalance, not path failures per candidate.

Metrics to demote:

- Raw path attempts.
- Raw candidate count.
- Raw direct fallback count.
- Raw validation count without coverage context.

## Reachability Memory Alignment

Reachability memory should evolve from candidate success memory into coverage outcome memory.

Current learning events are useful:

- candidate generated
- candidate validated
- candidate rejected
- activated
- sample complete

End-state learning should weight these outcomes more strongly:

- station duration reached 5 minutes
- station duration reached 30 minutes
- repeated sample ticks while target remained valid
- resource despawned after sustained coverage
- reassignment happened because coverage was satisfied, not because path churned

Recommended confidence hierarchy:

| Signal | Proposed confidence value |
| --- | --- |
| Candidate generated | Weak |
| Verified path | Medium |
| Activation started | Medium |
| First sample complete | Strong |
| Stationed sampling for several intervals | Very strong |
| Coverage retained until planned rebalance/despawn | Strongest |

## Coverage Model Proposal

Introduce a first-class coverage slot model before adding more movement behavior.

Example desired coverage snapshot:

| Need | Desired miners | Actual covering | Gap | Rebalance priority |
| --- | ---: | ---: | ---: | --- |
| Chef Buff Foods | 2 | 1 | 1 | High |
| Production Infrastructure | 3 | 2 | 1 | Medium |
| Composite Armor Supply | 2 | 2 | 0 | Stable |

Coverage slot identity should include:

- demand profile key
- resource spawn identity
- resource type
- zone
- target location bucket
- desired miner count
- actual assigned miner ids
- coverage state
- station duration
- last sample time
- rebalance reason

## Recommended Next Phases

1. P.3.1 Coverage State Schema
   Define in-memory coverage slot structs and dashboard JSON fields. No behavior change. Add `desiredCoverage`, `actualCoverage`, `stationed`, `coverageGap`, `coverageAgeSeconds`, and `rebalanceReason`.

2. P.3.2 Stationed Lifecycle State
   Add a long-lived `stationed` or `covering` assignment status after arrival. Sampling should loop inside that state instead of clearing assignment after each intelligent sample.

3. P.3.3 Coverage-First Dashboard Header
   Promote coverage stability metrics above activation metrics: stationed miners, coverage gaps, average assignment age, churn rate, and uncovered high-priority needs.

4. P.3.4 Rebalance Policy
   Add explicit rebalance reasons and thresholds: resource despawn, quality obsolete, demand pressure material change, stockpile reserve satisfied, stronger opportunity, or manual/operator reset.

5. P.3.5 Reachability Memory Reweighting
   Shift memory confidence toward sustained stationed coverage and repeated sample completion. Candidate generation should become a weak signal.

6. P.3.6 Movement Throttling by Coverage Need
   Movement should happen only when coverage planner opens a slot or rebalance policy invalidates a current slot. Avoid replacing stable coverage for minor score changes.

7. P.3.7 Persistence Design Review
   Only after the coverage model is stable, decide what coverage and stockpile state should survive restart. Keep actual economy mutation out of scope until explicitly approved.

## Working Definition of Success

P.3 is successful when the dashboard can answer these questions directly:

- Which demand needs should be covered?
- How many miners should cover each need?
- How many miners are currently covering each need?
- Which miners are stationed and for how long?
- Which coverage gaps are worth moving for?
- Which existing assignments should not churn?
- Why did each rebalance happen?

Until those questions are first-class, additional pathfinding and activation work risks optimizing the wrong thing.

## P.3.1 / P.3.2 Implementation - Coverage Slots + Stationed Lifecycle

P.3.1/P.3.2 adds the first conservative implementation of coverage-oriented miner state. The new API/dashboard section is `coveragePlanner`. It is memory-only and derived from current demand profile settings, live intelligent assignments, and conceptual stockpile totals. It reports desired coverage slots, covered slots, total gap, stationed/moving/sampling/unassigned miners, coverage by profile, coverage by resource, uncovered needs, rebalance candidates, station duration summary, and station sample summary.

The long-lived assignment status is `stationed`. When `stationedMinerConfig.enableStationedLifecycle=true`, a successful intelligent conceptual sample can retain its existing assignment instead of clearing it as `sampleComplete`. The retained assignment keeps its generation id, target hash, activation snapshot id, target resource identity, demand profile, zone, station timestamp, last station sample timestamp, station sample count, conceptual station yield quantity, and station duration.

Repeated stationed sampling is separately gated by `stationedMinerConfig.enableStationedRepeatedSampling=false` by default. When explicitly enabled, it schedules another conceptual sample after `stationedSampleIntervalSeconds` plus jitter and still uses only the existing conceptual yield path. It does not create real resources, create `ResourceContainer` objects, mutate inventory, mutate vendors/market/crafting/credits, or write persistence.

Stationed assignments clear or become eligible for reassignment only through explicit reasons such as `resourceDespawned`, `demandNoLongerValid`, `reserveSatisfied`, `maxStationDurationReached`, `maxStationSamplesReached`, `minerInvalid`, `zoneMismatch`, `manualReset`, or `emergencyDisabled`. `sampleFinished` remains an event; it is no longer treated as assignment completion when stationed lifecycle is enabled and the assignment remains valid.

Configuration defaults:

```lua
stationedMinerConfig = {
    enableStationedLifecycle = false,
    enableStationedRepeatedSampling = false,
    stationedSampleIntervalSeconds = 300,
    stationedSampleJitterSeconds = 60,
    stationedMaxSamplesPerAssignment = 12,
    stationedMaxDurationSeconds = 3600,
    stationedRequireDemandStillValid = true,
    stationedRequireResourceStillActive = true,
    stationedRequireSamePlanet = true,
    stationedClearWhenReserveSatisfied = true,
}
```

Existing activity diagnostics now distinguish movement activity from coverage activity. `minerActivity.coverageActiveCount` includes moving, sampling, and stationed assignments, and stationed miners are not counted as idle. The dashboard shows Coverage Planner rows before Coverage Alignment so stable coverage is visible above lower-level path and activation diagnostics.

Reachability memory is prepared for sustained coverage by recording `coverageRetainedCount`, `stationedSampleCount`, `stationedDurationSeconds`, and `sustainedCoverageConfidence` in runtime-only memory buckets. This strengthens positive evidence for locations that remain useful after arrival without changing candidate scoring, path validation trust, movement speed, NavArea behavior, extraction, inventory, market, vendor, crafting, credits, or persistence.
