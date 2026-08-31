# Live Test — reject incomplete paths (mode 2 diagnosis)

**Verdict: the mechanism WORKS and the answer is NEGATIVE.** `requireCompletePath`
left **default-off** — it is a measuring instrument, not a fix.

Rejecting truncated paths does not rescue starport egress, because **no complete
path exists to find**. That is the decisive result: the truncation is not a race,
a fluke, or a retry problem.

- **Run ID**: `20260825-110559-require-complete-path`
- **Matrix**: 10 PASS / 15 FAIL (baseline 11 / 14) — one regression, no new passes
- **Rejections fired**: 15

## The decisive line

```
result=rejected reason=incomplete_path endDist=47.5082 tolerance=10
       pathEnd=(3573.4,-4813.2,5.08505) destination=(3527,-4803,5)
SCENARIO_RESULT mos_eisley_starport_front status=FAIL reason=controller_path_failed
```

The egress path ends 47.5 m short, is rejected, is retried — and the retry
cannot do better.

**The mechanism itself is sound.** Med-centre ingress rejections
(`endDist=22.0`, `13.4`) were retried and the `enter` step still **PASSED**.
So rejecting incomplete paths recovers correctly wherever a complete path is
achievable. It only fails where none is.

## Correction to my previous report

I reported "same NavArea, so recast should be able to route." That was wrong in a
specific way: **NavArea identity is not mesh connectivity.** Two points inside
one NavArea's region can sit on disconnected polygon islands. The pad is meshed,
the street is meshed, they share a NavArea — and no walkable corridor joins them.

Third time in this feature a boolean answered a coarser question than the one
that decided the design (`wouldBlock` hiding truncation, `crossGraphChildren`
conflating edge types, `navAt*`/`sameArea` hiding connectivity), and the second
time after I had already named the pattern.

## Three independent measurements now agree

| Method | Result |
| --- | --- |
| Radial ray scan | 72/72 rays blocked — no gap in the enclosure |
| Blocker histogram | 65/72 blocked by `starport_tatooine` itself |
| Navmesh routing | no polygon corridor from pad to street |

Geometry, blocker identity and navmesh all say the same thing: **the only way off
the pad is through the building.** The owner's spur model is confirmed three ways,
and mode 2's fix is the door-walking plan — not a gap, not a navmesh flip, not a
smarter retry.

## Cost of the gate

`cantina_to_corellia_hospital` regressed: its route legitimately ends short of
the nominal target and the 10 m tolerance rejects it. That is the same
one-consumer's-default-is-another's-bug pattern seen with hybrid movement and
`allowPartial` — which is exactly why this stays a gated diagnostic rather than
becoming a fourth global behaviour switch.

`egressPathFailures` stayed 0 and `zSanityViolations`/`teleportsDetected` 0/0.
No crash (gdb.log 876, alive 1h09m).

## Cleanup

All gates default-off; `requireCompletePath` and `completePathToleranceMeters`
retained in the committed config at `false`/10 with the verdict inline. New
baseline md5 `b206459b1e130ad0d267bd5de1983922`.
