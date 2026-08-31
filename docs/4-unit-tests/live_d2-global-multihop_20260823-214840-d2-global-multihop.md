# Live Verification — D2 Retry 4 (`global_multihop`)

**Verdict: `LIVE_VERIFICATION_FAIL` for the fix. `ROOT CAUSE ESTABLISHED` for
the defect.** No receipt.

Retry 4 did not repair D2 — the two med-centre scenarios still fail
`controller_path_failed` and the matrix is unchanged at 10 PASS / 15 FAIL. But
the telemetry it carried answers D2 definitively, and **refutes my own
hypothesis** in a way that points at the fix.

- **Run ID**: `20260823-214840-d2-global-multihop`
- **Change**: Retry 4 `repair=global_multihop`, Codex APPROVED after 3 rounds
- **Building**: `1697358` cell `1697364` (Naboo med centre), template-level

## The measurement

Every one of the 5 source global nodes, on every attempt:

```
repair=global_multihop candidate globalId=13 crossGraphChildren=1 neighborCount=2 floorReachable=1 floorResult=0 pathNodes=none
repair=global_multihop candidate globalId=15 crossGraphChildren=1 neighborCount=2 floorReachable=1 floorResult=0 pathNodes=none
repair=global_multihop candidate globalId=10 crossGraphChildren=1 neighborCount=2 floorReachable=1 floorResult=0 pathNodes=none
repair=global_multihop candidate globalId=14 crossGraphChildren=1 neighborCount=2 floorReachable=1 floorResult=0 pathNodes=none
repair=global_multihop candidate globalId=16 crossGraphChildren=1 neighborCount=2 floorReachable=1 floorResult=0 pathNodes=none
repair=global_multihop result=failed candidatesTried=5 candidatesTotal=5
```

Three facts, none of which were previously known:

1. **`crossGraphChildren=1` — my hypothesis was WRONG.** I predicted that if
   no multi-hop route existed we would see zero cross-graph children. Every node
   has exactly one. Cross-cell links *do* exist.
2. **`floorReachable=1`, `floorResult=0` — the bot can walk to every one.**
   In-cell reachability is not the obstacle either.
3. **`pathNodes=none` regardless.** A\* still cannot reach the exterior.

## The actual root cause

Global-ID topology, read straight off the run:

| Graph | Global IDs exposed |
| --- | --- |
| source cell `1697364` | **10, 13, 14, 15, 16** |
| exterior floor mesh | **1** (both `finalExteriorNode=14` and `=16` report `global=1`) |

`PortalLayout::connectFloorMeshGraphs()` creates an edge **only** where two
nodes share a global ID. The interior set and the exterior set are **disjoint**,
so no edge into the exterior graph is ever created from the interior side. The
interior nodes' one cross-graph child each links them to *other interior cells* —
a chain that can never terminate at the exterior.

**The path-graph route from interior to exterior does not exist in this
template's data, at any hop count.** No fifth strategy over the path graph can
succeed. Retries 1–4 were all searching a graph with no edge to the destination.

## The fix this points to, and the evidence for it

The same run, same building, D7's exit set:

```
ST_EGRESS exitSet=graph building=1697358 cellNumber=6 exteriorNodes=17 globalNodes=5 entrancesRaw=0 doorNodes=5
ST_EGRESS exitSet=candidate index=0 model=-19.243,0.00299905,7.25 world=-5027.71,4180.44,6.003 distFromBot=3.09418
```

Compare the exterior node the repair ladder gives up on:

```
repair=all_failed ... finalExteriorNode=14(global=1 pos=(-19.243,0.00299905,7.25))
```

**Identical position.** Both systems identify the *same door*. D7's
portal-geometry route reaches it and produces a usable world coordinate; the
path-graph route cannot build an edge to it. The door is not missing — the
*graph edge* is.

So D2's fix is not another repair strategy. It is to egress these templates by
**portal geometry** (`CellPortal` nodes, exactly as D7's exit set already does)
when the path graph has no route, rather than continuing to repair a graph that
is structurally disconnected.

## Assertions

| # | Assertion | Actual | |
| --- | --- | --- | --- |
| Retry 4 repairs D2 | 2 med-centre scenarios pass | still `controller_path_failed` | **FAIL** |
| No regression | 10 PASS / 15 FAIL, same 10 | unchanged | PASS |
| Telemetry is decisive | root cause identifiable in one run | **yes — global-ID disjointness** | PASS |
| No crash | gdb.log unchanged | 876 lines, alive 10h38m | PASS |
| No wall-crossing introduced | floor filter active | `floorReachable` gate on every candidate | PASS |

## Cleanup

Config restored to all gates off, tunable retained (md5
`7a054f01b6f7c148b3cc65492469d39e`). Sampler stopped. Evidence at
`bin/log/trip-verify-20260823-214840-d2-global-multihop-structuretraversal.log`. Retry 4 is left in place:
it is correct, bounded, gated default-off, and its telemetry is what produced
this diagnosis. It costs at most 16 bounded A\* attempts on a path that would
otherwise report `all_failed` anyway.
