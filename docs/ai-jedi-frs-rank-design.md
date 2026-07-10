# P.7.4 — NPC Jedi FRS Ranks: Ranked Templates, Rank-Scaled Power, FRS XP on Kill

Status: **IMPLEMENTED (Codex) + VERIFIED LIVE 2026-07-09 (28 ranked Jedi
active; XP hook paid a real player 3,782 FRS XP / 5 kills). Two review bugs
FIXED (dark control ladder, regen double-count — §6.5), compiled clean,
PENDING RESTART.** Owner decisions (§7): tier order is
**Knight → Sentinel → Master** = FRS titles (Knight is the FRS entry rank,
Sentinel the next band, Master the top — matches the FRS title ladder Padawan/
Knight/Sentinel I–IV/Consular I–III/Arbiter I–II/Council); **normal FRS XP per
kill by rank** (npcXpFactor = 1.0 — supersmall-population server); and the
NPC's ability stats (Force Armor etc.) must be the **actual FRS per-rank
values**, which the owner supplied (pastebin light/dark tables, §3.2 — light
and dark values are identical, only tier titles differ).
Related docs: `ai-jedi-force-archetype-design.md` (P.7 archetypes this extends),
`ai-pvp-squad-design.md` (P.6 squads that field the Jedi),
`ai-pvp-mimetic-travel-design.md` (P.6.5, same session's first workstream).

---

## 0. Owner request (2026-07-07)

The Force Ranking System should supplement AI PvP, because FRS XP is hard to
gain: being part of a kill of a Jedi Knight or higher-ranked Jedi grants FRS XP.
Today only the low-tier Jedi NPCs are fielded; add higher-rank NPC Jedi
(Sentinel-tier and Master-tier at least, from available templates), with:
1. **Increased FRS XP per rank** for players who take part in killing them.
2. **Rank-scaled power** mirroring player FRS perks — e.g. a Jedi Master with
   Force Armor takes very little damage and spends very little force per hit
   compared to a Knight; other abilities scale similarly. Beef up NPC Jedi by
   rank.

## 1. Current state

- Squads field `dark_jedi_sentinel` / `light_jedi_sentinel` (CL 88, the lowest
  Jedi tier) with `jediArchetype="random"` (P.7.1 defender/enhancer ladder,
  shipped; full P.7.1–P.7.3 force-management ladder live in the binary).
- **FRS is 100% player-only.** `isValidFrsBattle`
  (`FrsManagerImplementation.cpp:1066`) requires BOTH ghosts; the award path is
  `PlayerManagerImplementation.cpp:6992` (doPvpDeathRatingUpdate, player-kills-
  player, damage-contribution-weighted). Killing an NPC Jedi awards **zero** FRS
  XP today.
- Template inventory (verified):
  - dark side: `thug/dark_jedi_sentinel` CL 88, `thug/dark_jedi_knight` CL 265,
    `thug/dark_jedi_master` CL 291 (+ quest dolac variants).
  - light side: only `light_jedi_padawan` and `faction/rebel/light_jedi_sentinel`
    (CL 88). **No light knight/master templates exist** — they must be created
    (simple lua clones of the dark stat blocks using the existing
    `light_jedi` dressgroup + `jedi_light` weapon groups).
- AiAgents already have a force pool (hardcoded 6850 + regen task) and the P.7
  archetype system bakes skillmod packages at spawn — a rank multiplier slots in
  cleanly at the same place (`initializeJediArchetype`).

## 2. Engine facts (verified 2026-07-07, file:line)

- **FRS XP is table-driven and reusable for NPCs.**
  `getBaseExperienceGain` (`FrsManagerImplementation.cpp:~1145`) builds a key
  from the OPPONENT: `"rank<N>_win"` if the opponent is a ranked Jedi (else
  `padawan_`/`nonjedi_`/`bh_`), then indexes the value table by the PLAYER's own
  rank. Tables live in `bin/scripts/managers/jedi/frs_manager.lua`
  (`experienceValues`, e.g. `rank1_win = 900` across all player ranks,
  `nonjedi_win = 0`). → An NPC victim with a **synthetic rank N** can reuse the
  exact live tables: `experienceValues["rank<N>_win"][playerRank]`. Rank scaling
  comes free.
- `adjustFrsExperience` (`:864`) is the single live mutator for FRS XP (handles
  messaging). XP alone never promotes — FRS promotion is a separate
  petition/voting flow — so granting XP cannot skip rank gates.
- **"Part of a kill" already exists for NPC deaths**:
  `PlayerManagerImplementation::disseminateExperience` (`:1906`) walks the
  victim's ThreatMap with per-player damage contribution — the natural, proven
  hook point (same contribution concept the player-kill FRS path uses at
  `:6992`).
- FRS ranks: 11 per council with XP thresholds (frs_manager.lua:34-56);
  councils LIGHT/DARK; XP only flows to players **in a council**
  (`getBaseExperienceGain` returns 0 for council 0) and live rules only credit
  cross-council kills — we keep both invariants.
- **THE KEY FIND (2026-07-07): Core3 already has the FRS ability-scaling
  machinery** — we don't invent scaling, we feed it. In `JediQueueCommand.h`:
  - `getFrsModifiedBuffValue` (:220): buff skillmod value = command base +
    `force_control_light|dark` skillmod × per-command `frsLight|DarkBuffModifier`
    (the buff is applied with this value at :213-214).
  - `getFrsModifiedExtraForceCost` / `getFrsModifiedForceCost`: per-hit and
    cast force costs reduced by `force_manipulation_light|dark` × per-command
    cost modifiers.
  - Per-command bases live in the command headers (e.g. `ForceArmor1Command.h`
    `skillMods.put("force_armor", 25)`, ForceArmor2 45, Shield1 25/Shield2 45,
    Feedback1 65/Feedback2 95, resists 25) and the FRS modifiers in
    `bin/scripts/commands/*.lua` (forceArmor1: buff 0.25, extraCost −0.003;
    forceArmor2: buff 0.35, extraCost −0.003; shield2 0.35; feedback2 0.45;
    resists 0.35).
  - For players the `force_control_*`/`force_manipulation_*` skillmods come
    from the FRS rank skill boxes. **For AiAgents these functions currently
    early-out** (no ghost → base values; `getFrsModifiedForceCost` :251
    explicitly returns base for AI). AiAgents DO have a working skillModList
    (P.7.1 bakes packages into it and `getSkillMod` reads it).
- **Derived per-rank skillmod ladder (verified against the owner's tables):**
  inverting the pastebin values through the formulas above yields clean
  integers that reproduce EVERY defensive table exactly (checked FA1, FA2,
  shields, feedbacks, resists; e.g. rank 11 FA2 = 45 + 120×0.35 = 87% ✓ at
  cost 0.30 − 80×0.003 = 6% ✓):

  | FRS rank (light title) | force_control | force_manipulation | +maxForce | +regen |
  |---|---|---|---|---|
  | 0 (Knight) | 5 | 5 | 90 | 1 |
  | 1 (Sentinel I) | 10 | 8 | 160 | 2 |
  | 2 (Sentinel II) | 15 | 12 | 230 | 3 |
  | 3 (Sentinel III) | 20 | 16 | 300 | 4 |
  | 4 (Sentinel IV) | 25 | 20 | 370 | 5 |
  | 5 (Consular I) | 35 | 25 | 500 | 6 |
  | 6 (Consular II) | 45 | 30 | 650 | 8 |
  | 7 (Consular III) | 55 | 35 | 800 | 9 |
  | 8 (Arbiter I) | 70 | 45 | 1050 | 12 |
  | 9 (Arbiter II) | 85 | 55 | 1300 | 14 |
  | 10 (The Council) | 100 | 65 | 1600 | 17 |
  | 11 (Council Leader / Master) | 120 | 80 | 1950 | 20 |

  Dark titles differ (Dark Knight, Enforcer I–IV, Templar I–III, ...) but the
  VALUES are identical. Raw tables archived from the owner's pastebins
  (light 4vGV8evt, dark 8JjDtj0x).

## 3. Design

### 3.1 Ranked NPC Jedi (P.7.4a)

- New creature-template lua field **`frsRank`** (0–11, default 0 = unranked) +
  **`frsCouncil`** derived (light/dark from template line). Parsed in
  `loadTemplateData` beside `jediArchetype` (same pattern).
- New light-side templates: `light_jedi_knight`, `light_jedi_master` (lua clones
  of the dark equivalents' stat blocks; light weapons/dress; CL tuned — see
  decision #2, the raw CL 265/291 thug blocks may be too hot next to CL 88
  squadmates).
- Tier map (OWNER DECIDED — knight first, sentinel second, master final,
  matching the FRS title ladder):
  `*_jedi_knight` → frsRank 0 (FRS entry "Knight"),
  `*_jedi_sentinel` → frsRank 1–4 (the Sentinel band; roll within it at spawn),
  `*_jedi_master` → frsRank 11 (top rank — "Jedi Master"/"Council Leader").
- Squad composition: `pvpConfig.templates` entries stay strings but the squad
  spawner recognizes the ranked templates; optional weighted table later
  (e.g. masters rare). Rank surfaces in the bot's displayed title/announces
  ("Dark Jedi Master <name> has arrived") via existing comms — cheap flavor,
  P.7.4c.

### 3.2 Rank-scaled power = REAL FRS values (P.7.4a) — REVISED per owner

The owner requires NPC ability stats to BE the FRS per-rank values, and Core3
already computes those from skillmods (§2 key find). So instead of an invented
multiplier, P.7.4a does exactly what the FRS rank skill boxes do for players:

1. **Bake the rank's skillmods at spawn** (extends `initializeJediArchetype`):
   `force_control_light|dark` + `force_manipulation_light|dark` from the §2
   ladder table (side picked by `frsCouncil`), plus the rank's
   `jedi_force_power_regen` bonus. (Force pool: agents already carry 6850 —
   far above player max+1950 — leave it; revisit only if R5 normalizes agent
   pools.)
2. **Make the three `getFrsModified*` functions in `JediQueueCommand.h`
   AI-aware** (same file already has AI branches from P.7.1/1a): for an
   AiAgent, read council from the agent's `frsCouncil` and the control/
   manipulation mods via `agent->getSkillMod(...)` (works — P.7.1 bakes into
   skillModList). Then **every Jedi command an NPC casts gets the exact
   player FRS math through the identical code path** — an NPC Master's Force
   Armor 2 is 87% reduction because a player Master's is; no per-ability
   tables, no drift when command lua is rebalanced.
3. Ladder tuning by rank stays as designed (cast window 6–10s shrinking to
   4–7s at high rank; masters keep Force Armor 2 up permanently — with 87%
   reduction that delivers "takes very little damage").
4. "Uses very little force per hit": NPCs currently pay **no** per-hit Force
   Armor drain (P.7 R3 — handleBuff early-returns without a ghost), so the
   master-vs-knight efficiency contrast is trivially satisfied today. IF R3
   is ever implemented, the AI-aware `getFrsModifiedExtraForceCost` from step
   2 already yields the correct rank-scaled per-hit cost (Master 6% vs Knight
   28.5% on FA2) — R3 becomes a one-line change that cannot regress this.

### 3.3 FRS XP for players on NPC Jedi kills (P.7.4b — the one core hook)

Flag-gated block in `disseminateExperience` (early-out when disabled, pattern
identical to the P.6.3 hooks):

```
victim is AiAgent && victim->frsRank valid && frsFromNpcJedi enabled:
  for each ThreatMap PLAYER with damage contribution >= minContributionPct:
    player in FRS council && council != npc council (cross-council rule kept)
    xp = experienceValues["rank<npcRank>_win"][playerRank]
         * contribution * npcXpFactor        -- OWNER: 1.0 = normal FRS values
    (optional daily cap — default OFF per owner), adjustFrsExperience(player, xp)
```

- Rank-0 (Knight) victims use the `rank0_win` table row (exists: 750), masters
  `rank11_win` (9750) — the normal per-rank values, exactly as the owner asked.
- **Award-only**: no FRS XP loss ever from NPC fights, victim-side code
  untouched, arena/council/voting code untouched. A player DYING to an NPC Jedi
  costs nothing.
- **Anti-farm** (OWNER decision: normal XP, small-population server — so
  factors stay neutral, but the knobs exist if the population grows):
  - `npcXpFactor` default **1.0** (normal FRS XP per kill by rank);
  - `npcFrsXpDailyCap` default **0 = disabled** (in-memory per-player rolling
    cap available if ever needed);
  - `minContributionPct` (default 0.10) so tag-alongs don't leech;
  - only OVERT, combat-engaged victims count (they always are — squad bots are
    OVERT by design).
- Locking mirrors the live award at `:6999`: deferred task, `Locker` on
  frsManager + player, never inside the death path's hot locks.

## 4. Config (lua, C++ defaults ALL off)

```lua
pvpConfig.jediRanks = {
    enableRankedJedi = false,      -- gates frsRank parsing + FRS-value stats
    frsFromNpcJedi = false,        -- gates the XP hook (P.7.4b)
    npcXpFactor = 1.0,             -- OWNER: normal FRS XP by rank
    npcFrsXpDailyCap = 0,          -- OWNER: no cap (small pop); >0 enables
    minContributionPct = 0.10,
}
```

## 5. Phases (each: build clean `-Werror`, owner restarts, verify)

- **P.7.4a Ranked templates + FRS-value power**: frsRank/frsCouncil fields,
  light knight/master lua templates, the §2 skillmod ladder baked in
  initializeJediArchetype, AI-aware getFrsModified* in JediQueueCommand.h,
  squad template wiring, dashboard `pvpActivity.jediRanks` (per-squad ranked
  jedi + rank distribution). No FRS XP yet — verify visible power difference
  (a master squad jedi visibly outlasts a sentinel under fire; buff skillmod
  values on a spawned master match the §2 table) + no balance blowups vs
  troopers.
- **P.7.4b FRS XP hook**: disseminateExperience block + caps + counters
  (dashboard: npcFrsXpAwardedTotal, awardsToday per player count, capHits).
  Needs a real FRS-council player to verify end-to-end (owner test, like
  P.6.3c).
- **P.7.4c Flavor polish**: rank in names/announces; optionally masters as rare
  "boss" squad members.

## 6. Safety analysis

- One core-file hook (disseminateExperience), flag-gated inert-when-off — the
  established pattern (P.6.3 chat hooks).
- `adjustFrsExperience` is the live-proven mutator; XP cannot promote by
  itself (promotion = separate petition/vote flow), so no rank-gate bypass.
- **This intentionally mutates real player FRS XP when enabled** — it is the
  first sim feature to touch player progression, which is exactly the owner's
  ask ("supplement FRS XP gain"). Shipped default-OFF; enabled explicitly by
  the owner; capped + fractional so it can't outpace real PvP.
- NPC-side changes (rank stats) are spawn-time skillmods on sim bots only —
  same containment as P.7.1.
- Reversible at runtime: both gates re-read on the 30s config refresh.

## 6.5 P.7.4 AS-BUILT (implemented by Codex, reviewed + fixed 2026-07-09)

Codex implemented the full design; verified live on the dashboard
(`pvpActivity.jediRanks`): 28 ranked Jedi active across squads, and the XP
hook already paid a real player 3,782 FRS XP over 5 NPC-Jedi kills. As-built
notes (matches the design unless flagged):

- Templates (all CL 88, clone-down as planned): light knight (rank 0 fixed) /
  sentinel (1–4) / consular (5–7) / master (11 fixed); dark knight (0) /
  enforcer (1–4) / templar (5–7) / master (11) — FRS tier titles per side.
  `frsRank` or `frsRankMin/Max` + `frsCouncil` template fields
  (CreatureTemplate.h). Squad picks are WEIGHTED
  (`pvpConfig.templates` entries `{template=, weight=}`): trooper 35 /
  knight 28 / band-2 19 / band-3 12 / master 6.
- Rank bake in `initializeJediArchetype` (AiAgentImplementation.cpp ~:769):
  control/manipulation/maxForce/regen skillmods per rank; AI-aware
  `getFrsModified*` in JediQueueCommand.h exactly per design (§3.2).
- XP hook in `disseminateExperience` (PlayerManagerImplementation ~:1944) →
  `FrsManager` award path with contribution + factor; per-player daily-cap
  bookkeeping on the sim manager (`recordPvpNpcFrsXpAward`, pvpSquadMutex).
- Dashboard `pvpActivity.jediRanks`: per-agent rows (rank/council/mods),
  byRank histogram, awardsTodayByPlayer, cap counters.

**Two bugs found in review, FIXED 2026-07-09 (compiled clean, PENDING
RESTART):**
1. **Invented weaker dark control ladder** — Codex used
   `darkControlByRank {4,6,8,10,12,15,20,25,35,45,60,75}`, but the owner's
   light and dark tables carry IDENTICAL values (verified by diffing the raw
   tables — only tier titles differ). A dark Master's Force Armor 2 was 71%
   instead of 87%. Fix: one shared `controlByRank` = the verified light
   ladder.
2. **Regen double-count** — the bake passed
   `getSkillMod("jedi_force_power_regen") + regenByRank[rank]` into
   `addSkillMod`, which ACCUMULATES; an enhancer's baked +25 was counted
   twice (observed live: enhancer knights at regen 51 instead of 26; defenders
   correctly at 1). Fix: pass only `regenByRank[rank]`.
   KEY LESSON: `addSkillMod(TEMPLATE, ...)` adds to the existing mod — never
   read-modify-write it.

## 6.6 NPC Force Armor: WAS COMPLETELY DEAD — root-caused + FIXED (P.7.4c,
2026-07-09, compiled clean, PENDING RESTART)

Owner report: "saw the NPC cast Force Armor but my (saber, Dervish2 — client
STF renames it 'Wrath of Kun') hits don't seem mitigated." Owner then re-tested
against a forced-enhancer master and STILL saw nothing. **The owner was right.**

**CORRECTION OF THE RECORD (two prior claims were WRONG):** the P.7.1-era note
"NPC Force Armor mitigates (skillmod-based)" and this doc's first-pass 07-09
"verified working" analysis both missed the same thing. The buff mods DO land
(live rank-6 templar carried BUFF-group `force_armor=61` + `force_shield=61` —
45 + 45×0.35, the exact table row) and AiAgent::getSkillMod DOES read them —
but **`CombatManager::getArmorReduction` early-returns inside its
`defender->isAiAgent()` branch (template armor only), BEFORE the jedi
mitigation block**. Force Armor/Shield/Feedback/Absorb were unreachable for
NPC defenders: the whole P.7 ladder was casting dead defensive buffs.

**P.7.4c fixes (both flag-gated, inert when off):**
1. **Mitigation itself** (`pvpConfig.jediRanks.npcMitigation`, lua true / C++
   off): inside the AiAgent branch, before template armor (mirroring the
   player order), apply `force_armor` vs non-force damage and `force_shield`
   vs force damage — identical math, observers notified, jediMitigation
   recorded on the hitList. Force Absorb stays player-only (crediting force
   back needs a ghost); NPC Force Feedback is a possible follow-up.
2. **Visibility** (`pvpConfig.comms.showNpcMitigation`, lua true / C++ off):
   `sendMitigationCombatSpam` gained an optional `attacker` param (default
   nullptr, other call sites untouched); FORCEARMOR/FORCESHIELD/FORCEABSORB
   call sites pass it; when the defender is an AiAgent, the spam is delivered
   to the attacking PLAYER. (Fix 1 is what makes the hitList value nonzero so
   this ever fires. Note: the cbt_spam STF may render second-person — "your
   barrier..." — cosmetic, revisit if confusing.)

Verify in-game after restart: saber a buffed sim Jedi → damage numbers DROP
while Force Armor is up AND green absorption lines appear; a rank-11 master
with FA2 (87%) should be dramatically tanky vs sabers/blasters. Force powers
vs an ENHANCER (shield up) mitigate 61-87%; vs a DEFENDER archetype they
still land raw (no shield in that ladder — by-design live behavior; owner may
opt to add Shield to defenders).

## 6.7 P.7.5 — Full NPC buff-effect AUDIT + knockdown/state recovery reflex
(2026-07-09, compiled clean, PENDING RESTART)

Owner (after confirming Force Armor now works in-game): "are we sure ALL
buffs work for NPCs?" + spec for the missing healStatesSelf behavior. Audit
of every P.7 ability's EFFECT path (call-path traced end to end — the §6.6
lesson):

| Ability / effect | NPC status |
|---|---|
| Force Armor 1/2 mitigation | ✅ fixed §6.6 (owner-verified in-game) |
| Force Shield 1/2 mitigation | ✅ fixed §6.6 |
| Force Feedback 1/2 reflect | ✅ **fixed NOW** — was still dead; added to the §6.6 gated block, same math as players (incl. attacker force_defense reduction), reflect spam already broadcast by the caller |
| Force Absorb 1/2 | ❌ still inert for NPCs (stock `isPlayerCreature` gate; crediting force back needs ghost/AI plumbing — noted follow-up; enhancer ladder still casts it, harmless) |
| Resists vs states | ✅ AI-aware path exists (jedi_state_defense AiAgent override in applyStates — owner's earlier creatureskills work) |
| DoT resists (bleed/disease/poison) | ✅ generic getSkillMod reads, type-agnostic |
| Avoid Incapacitation | ✅ generic (CreatureObjectImplementation:1096, setHAM path) |
| Drain / Transfer / Channel Force | ✅ AI-aware (P.7.2/P.7.3) |
| Force pool / regen mods | ✅ live-verified |
| Per-hit Force Armor cost | ❌ by design so far (R3) |

**P.7.5 knockdown/state recovery reflex** (owner-specified player combo:
dizzy+KD → heal states → stand, "fluid, not dizzy-KD-heal-stand scripted"):
- Discovery: the BT gated the ENTIRE jedi branch on NOT-KNOCKEDDOWN (and the
  HEAL/NOTIFYHELP sockets still are) and **nothing in stock AI ever stands
  an NPC back up** — a floored jedi was helpless. The NOT-KD gate (default.lua
  ids 3811110007-9) is REMOVED; ManageJediForce now owns the floored window.
- `runJediForceManagement` starts with the reflex, OUTSIDE the 6-10s buff
  window (emergency, not rotation), self-paced 0.8-1.6s per action
  ("jedi_state_recovery") so the combo reads human:
  - KD + health <40% + force ≥300 → normal force heal FIRST while down
    (healCreatureTarget self — jedi can heal while KD, exactly the owner's
    "getting chunked" play);
  - KD + dizzy → direct heal-states (mirrors HealStatesSelfCommand exactly:
    state bits 12-15, removeStateBuff, 25 force/state, heal-self
    clienteffect; own 3-4.2s cooldown = defaultTime 3 + jitter). Re-dizzied
    before standing → waits out the cooldown like a player, with a 25%
    desperate stand-anyway roll per pass;
  - KD + not dizzy (or just cleared) → stand (setPosture UPRIGHT).
- Upright + INTIMIDATED (owner: rare — intimidate is free for most
  professions, clearing drains force for nothing): only when force >65% max,
  20% roll, 45s own cooldown, consumes the normal buff window.
- Sim-bot safety net (SimPvpBotController::onTick): combat over but still
  KNOCKEDDOWN → stand (the jedi reflex only ticks in combat; covers troopers
  too).

Verify in-game: dizzy + knock down a squad jedi → heal-self effect (states
cleared) then it stands within ~1-2s; chunk it while down → it heals first;
no sim bot left lying around after a fight.

## 7. Owner decisions (ANSWERED 2026-07-07)

1. Tier→rank map: **knight first, sentinel second, master final** — mapped to
   FRS: knight=rank 0, sentinel=ranks 1–4 band, master=rank 11 (§3.1). This
   matches the FRS title ladder in the owner-supplied tables.
2. **Normal FRS XP per kill by rank** (npcXpFactor 1.0, no daily cap) —
   deliberately generous for a super-small-population server.
3. **Ability stats must be the real FRS values** — implemented via the
   force_control/force_manipulation skillmod ladder + AI-aware
   getFrsModified* (§3.2); owner supplied the authoritative tables
   (pastebin light 4vGV8evt / dark 8JjDtj0x, §2).

Still open (small, can default sensibly at build):
- CL/stat tuning for knight/master squad members — thug CL 265/291 blocks
  as-is vs cloned-down (default plan: clone-down to keep squad TTK sane;
  the FRS skillmods provide the rank power difference).
- Master spawn weighting (default: masters rare — weighted template pick).
- Creating light_jedi_knight/light_jedi_master lua templates (required for
  light-side parity; assumed approved as part of the design).
