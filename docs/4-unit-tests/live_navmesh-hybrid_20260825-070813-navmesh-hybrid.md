# Live Test — navmesh hybrid movement for traversal bots (mode 2 step 1)

**Verdict: HYPOTHESIS REFUTED. `useNavmeshHybrid` measured HARMFUL and left
`false`.** No receipt.

Flipping traversal bots onto the hunters' navmesh hybrid movement took the
matrix **11 PASS → 9 PASS** and converted the 11 `exit_not_outdoors` failures
into **13 `controller_path_failed`**. The bot no longer even reaches the pad.

- **Run ID**: `20260825-070813-navmesh-hybrid`
- **Hypothesis**: starports are POIs and therefore navmeshed; traversal bots
  inherit `usesNavmeshHybridMovement() = false` while hunters return `true`,
  and hunters are the bots observed using starports correctly.

## Result

| | Baseline (multicell) | Hybrid on |
| --- | --- | --- |
| PASS / FAIL | **11 / 14** | **9 / 16** |
| `exit_not_outdoors` | 11 | **0** |
| `controller_path_failed` | 2 | **13** |
| regressions | — | `combat_ends_outdoors`, `death_or_incapacity_recovery` |
| `zSanityViolations` / teleports | 0 / 0 | 0 / 0 |
| hollow scans fired | n/a | **0** |

The failure moved *earlier*. Previously the bot exited its cell and stopped on
the pad; now the egress path request goes unanswered:

```
generation=6 request  cell=0 world=(3527,-4803,5)
generation=6 accepted nodes=42
generation=6 request  cell=0 world=(3527,-4803,5)   <- no accepted line
```

## Why — and it was already written down

```cpp
// A hybrid controller (hunters) therefore walks to the building and then
// stalls short of the collector forever — observed live as a relocation
// wedged 87m out for 870s.
return usesNavmeshHybridMovement() && !interiorApproachLeg &&
    !cellEgressActive && ticketTravelPhase == TICKET_TRAVEL_NONE;
```

Hybrid movement already carries **three hand-added exclusions**, each from a
previously observed stall. Structure-traversal egress is not among them, so
hybrid engages on the Egress leg and recast fails.

**Hybrid movement is not a general capability. It is hunter movement plus three
patches**, and enabling it for another consumer re-runs each of those bugs. The
correct reading of this result is not "add a fourth exclusion" — that is the
symptom. Movement mode should be a property of **the leg** ("outdoor city
traverse → navmesh" vs "interior/egress → portal routing"), decided once, rather
than a per-controller boolean with an accumulating exclusion list.

## Measurement gap I created

The navmesh probe was placed inside the hollow scan, which only fires when a bot
is hollow-stuck. With hybrid on the bot never gets there, so **zero scans fired
and the "is the pad navmeshed?" question is still unanswered.** Instrumentation
has to sit on a path that survives the change it is measuring. To answer it, the
probe needs to move to the egress path request, or reuse the existing
`findNavAreaAt` helper at leg issue time.

## Cleanup

`useNavmeshHybrid` re-added to the committed config as `false` with the verdict
recorded inline; the saved baseline predated the knob and restoring it blindly
would have dropped it (third time this trap has appeared). All gates off, new
baseline md5 `b03e11f88356559d11fb6decb2aa8a15`. Evidence at
`bin/log/trip-verify-20260825-070813-navmesh-hybrid-structuretraversal.log`.
