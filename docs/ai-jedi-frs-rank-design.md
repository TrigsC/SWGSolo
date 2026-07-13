# P.7.4 — NPC Jedi FRS Ranks: Ranked Templates, Rank-Scaled Power, FRS XP on Kill

Status: **IMPLEMENTED (Codex) + VERIFIED LIVE 2026-07-09 (28 ranked Jedi
active; XP hook paid a real player 3,782 FRS XP / 5 kills). Two review bugs
FIXED (dark control ladder, regen double-count — §6.5), compiled clean,
PENDING RESTART. Rank-correct council robes are IMPLEMENTED and VERIFIED on
clean Human and non-Human mobile bodies (§6.8); ranked-only clean-body groups
and randomized species appearance are IMPLEMENTED and pending build/deployment
verification.** Owner decisions (§7): tier order is
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
  enforcer (1–4) / templar (5–7) / master (8–10) — FRS tier titles per side.
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

## 6.8 P.7.6 — Rank-correct NPC Jedi robes (2026-07-10)

Status: **SERVER OBJECT CREATION, CONTAINMENT, CREO6 EQUIPMENT REGISTRATION,
AND VISUAL RENDERING VERIFIED LIVE 2026-07-12. Forced spawns using clean Human,
Rodian, Wookiee, and Ithorian bodies rendered the correct rank robes. Dedicated
ranked-only clean-body groups are implemented; ordinary Jedi templates retain
their existing groups.**

The NPC appearance investigation found two distinct, compatible systems:

1. `Creature.templates = { "dark_jedi" }` / `{ "light_jedi" }` resolves a
   dress group to an `object/mobile/dressed_*.iff`. The base body and clothing
   for these mobiles are described by client/TRE `clientDataFile` CDF data;
   Core3 randomly chooses one mobile IFF from the dress group at spawn.
2. `Creature.outfit = "group_name"` is a separate server system. In
   `AiAgentImplementation::loadTemplateData`, Core3 creates each tangible in a
   `MobileOutfitGroup` and transfers it to the NPC's wearable slots. This is
   usable for future non-Jedi uniforms and composite armor without editing
   TREs; examples already exist under `bin/scripts/mobile/outfits/` (including
   multi-piece armor). The wearable changes appearance only for an AiAgent:
   player-only equipment handling is what applies template/attachment skill
   mods, so an NPC does not inherit the player Jedi robe's +force mods.

The five dark and five light FRS robe server templates already exist under
`object/tangible/wearables/robe/`. Their certification requirements and the
live `frs_manager.lua` rank table provide the authoritative stock Pre-CU rank
thresholds. The supplied SWG Restoration chart may use Pre-CU art assets, but
it does not establish that each pictured composite outfit is the direct visual
of the correspondingly numbered single tangible IFF:

| FRS rank | Dark robe/title band | Light robe/title band |
|---|---|---|
| 0 | `robe_jedi_dark_s01` / Knight | `robe_jedi_light_s01` / Knight |
| 1–4 | `s02` / Enforcer | `s02` / Sentinel |
| 5–7 | `s03` / Templar | `s03` / Consular |
| 8–9 | `s04` / Oppressor | `s04` / Arbiter |
| 10–11 | `s05` / Overlord/leader | `s05` / Council/leader |

Implementation details:

- `bin/scripts/mobile/frs_rank_outfits.lua` owns the two 12-entry Lua mappings
  and is loaded before creature templates.
- Creature templates can opt in with `frsRankOutfits =
  darkJediFrsRankOutfits` (or light). All nine currently ranked Jedi templates
  opt in.
- `CreatureTemplate` parses the optional list. After
  `initializeJediArchetype` rolls the actual rank, the AiAgent creates and
  equips exactly that rank's tangible robe. This ordering is necessary because
  the current imperial dark master rolls ranks 8–10, crossing the `s04` →
  `s05` appearance boundary; a fixed `outfit` field would be wrong for one of
  those results.
- The feature remains under the existing `enableRankedJedi` gate because rank
  initialization and robe equip share the same gated block. Templates without
  `frsRankOutfits` are unchanged.

Restart verification: inspect one NPC from each available band and confirm its
robe matches the dashboard `pvpActivity.jediRanks[].rank`. In particular,
imperial dark-master rolls at rank 8/9 must wear `s04`, while rank 10 wears
`s05`. Also check a non-human dress-group roll to confirm the client has an
appearance mapping for that species; the robe templates advertise all player
races, but final rendering is client-side.

### 6.8.1 Spawn/equip diagnostics (2026-07-10)

The robe path now writes structured lines to the main server console/log with
the prefix **`[RankedJediOutfit]`**. This deliberately bypasses the AiAgent's
normally file-only logger so failed object creation is visible during a normal
startup/test run. Expected event order for an enabled ranked Jedi is:

1. `template_mapping_loaded` during Lua creature-template loading;
2. `spawn_config`, including the chosen base mobile IFF, rank range, council,
   mapping size, feature gate, and base container component;
3. `rank_selected` and `create_attempt`, including the requested robe IFF and
   CRC;
4. `create_success`, including the resolved server IFF, `isWearable`,
   `isRobe`, NPC-race-list match, arrangement descriptor count, and slots;
5. `equip_success`, including resulting parent and the decisive
   `wearablesTracked`/`wearablesCount` client-baseline state.

Failures use `create_failed`, `equip_failed`, `mapping_invalid`, `rank_invalid`,
or `skip` with a machine-readable `reason`. A successful containment transfer
that never reaches the client-facing vector also emits
`client_visibility_warning`. Search a captured server log with:

```sh
rg '\[RankedJediOutfit\]' core3.log
```

Interpretation: `create_failed` points to a missing/unregistered server robe
IFF; `equip_failed` points to containment or slot rejection; and
`equip_success ... wearablesTracked=0` means containment succeeded but the
robe was not added to the CREO6 wearables vector, explaining why clients do
not render it. `raceListed=0` identifies a robe whose allowed-race list does
not contain the randomly selected dressed mobile IFF and must be evaluated
alongside the transfer result.

Live output on 2026-07-10 proved that the robe IFFs and containment were valid:
every tested robe reported `create_success`, `isWearable=true`, `isRobe=true`,
the expected arrangement slots, and `parentMatchesAgent=true`. All failed at
the same final boundary with `wearablesTracked=false` and
`wearablesCount=0`. Root cause: ranked NPCs use `ContainerComponent`, while
only `PlayerContainerComponent::notifyObjectInserted` automatically calls
`addWearableObject`; CREO6 serializes visible equipment exclusively from that
wearables vector.

The ranked-AiAgent path now calls `addWearableObject(wearable, false)` only
after a successful transfer whose resulting parent is that agent. `false` is
intentional because this occurs during spawn initialization: the robe belongs
in the initial CREO6 baseline and does not need a redundant live delta. The
fix does not invoke or modify `PlayerContainerComponent`, does not change
player race/certification validation, and does not apply the robe's player-only
skill mods to the NPC. At this robe-only stage, expected verification output was
`parentMatchesAgent=true wearablesTracked=true wearablesCount=1` with no
`client_visibility_warning`; §6.8.4 documents the later count when randomized
hair is also tracked.

The same live capture also showed `dark_jedi_sentinel outfitEntries=0`; the
repository version already contains `frsRankOutfits = darkJediFrsRankOutfits`,
so that specific Lua file must be included in the next manual deployment.

### 6.8.2 Live visual result and dressed-mobile CDF boundary (2026-07-11)

The first post-fix server capture verified four enabled NPC cases (two dark
Knights, one dark Enforcer, and one light Knight). A larger manual-spawn capture
then verified ranks 0, 3/4, 5, 9/10, and 11 across human, Chiss, Twi'lek, Nikto,
and several dark-side humanoid mobile IFFs. Every enabled case completed with
`parentMatchesAgent=true wearablesTracked=true wearablesCount=1`; no client
visibility warning remained. One representative light Knight selected:

```
baseMobile=object/mobile/dressed_jedi_trainer_chiss_male_01.iff
rank=0
requestedIff=object/tangible/wearables/robe/robe_jedi_light_s01.iff
wearablesTracked=true wearablesCount=1
```

The screenshot and manual inspection did not show a reliable visual replacement.
That does not contradict the packet diagnostics: all selected base templates
were still dressed mobiles (`dressed_jedi_trainer_*`, `dressed_dark_jedi_*`,
`dressed_forsaken_force_drifter`, etc.). Even the observed human was
`dressed_jedi_trainer_old_human_male_01.iff`, not an undressed player-style
body. These mobile TRE templates reference `clientdata/npc/*.cdf` files that
bake clothing appearances into the creature at construction. Those baked
appearances are not tangible child objects in Core3, so `transferObject`, slot
occupancy, and the CREO6 wearables vector cannot remove them.

Creation time itself is not the issue. `AiAgent::loadTemplateData` creates and
registers the robe before `CreatureManager::placeCreature` transfers the agent
into the zone, so its initial baseline already contains the robe. The controlled
next step is a dedicated ranked-Jedi body/dress group using an existing human
mobile IFF whose `clientDataFile` is empty or the generic undressed
`client_shared_npc_human_m.cdf`. Equip the rank robe on that body and compare
`s01` with `s05`. If those render distinctly, the permanent fix is to use
ranked-only blank body groups rather than the existing dressed Jedi groups. If
they still do not render, the next diagnostic is the client CRC/appearance-map
selection rather than more containment changes.

This can be tested live without another build because `CreateCreatureCommand`
accepts an optional object-template override. `huff_darklighter.iff` uses
`appearance/hum_m.sat` with an empty `clientDataFile`; its normal creature
definition supplies clothing separately through Core3's `outfit` system. Run
these side-by-side:

```
/createCreature light_jedi_knight object/mobile/huff_darklighter.iff
/createCreature light_jedi_master object/mobile/huff_darklighter.iff
```

`object/mobile/mos_taike_guard_young.iff` is a second established test body;
it uses the generic `client_shared_npc_human_m.cdf` and also receives its normal
clothing through an `outfit` group.

The corresponding diagnostics must show the forced base IFF and rank 0/s01
versus rank 11/s05. A visible difference proves the ranked robe path and
isolates the existing Jedi dress-group CDFs as the masking layer. No visible
robe on either forced body means the next patch should log the tangible's
client CRC and inspect the client's wearable appearance-table resolution.

During this diagnostics change, two independent repository Lua blockers found
in the 2026-07-10 startup log were also repaired: unresolved merge markers in
`drink_spiced_tea.lua`, and an extra closing table delimiter in
`terminal_character_builder.lua`. The latter prevented the Blue Frog server
template from registering and left ground-zone startup waiting for managers;
it was unrelated to the robe IFF mapping but prevented a clean test run.

### 6.8.3 Clean-body proof and ranked dress-group constraints (2026-07-12)

The owner ran both controlled commands from §6.8.2 and confirmed that the NPCs
rendered the correct robes:

```
/createCreature light_jedi_knight object/mobile/huff_darklighter.iff
/createCreature light_jedi_master object/mobile/huff_darklighter.iff
```

This closes the wearable pipeline investigation. Rank selection, robe creation,
containment, CREO6 registration, and client appearance-table resolution all
work. The old `dark_jedi` and `light_jedi` dress groups are the only masking
layer because every entry in those groups uses a dressed client CDF.

The permanent server-side shape is two new groups (for example,
`ranked_dark_jedi` and `ranked_light_jedi`) referenced only by the nine
`frsRankOutfits` creature templates. Do not modify the ordinary Jedi groups;
quest, trainer, and non-ranked spawns also use them.

Repository/TRE audit constraints:

- Dress groups contain only mobile IFF strings. They cannot assign skin, face,
  hair, or other customization values. Weighted selection is possible only by
  deliberately repeating an IFF in the Lua list.
- Clean empty-CDF humanoids exist for Human male (`huff_darklighter`, plus
  several equivalent theme-park bodies), Human female (`kaja_orzee`), and
  Rodian male (`kardeer`, `reelo_baruk`, `wald`). Generic body CDFs also exist
  for Zabrak male (`dressed_ruffian_zabrak_male_01`), Wookiee male/female, and
  Ithorian male. Each non-Human candidate still requires a live forced-IFF
  render check before it enters the production group.
- The stock base mobiles for Bothan, Mon Calamari, Sullustan, Trandoshan,
  Twi'lek, Zabrak female, and most other gender/species combinations use
  `client_shared_npc_dressed_*.cdf`. They can recreate clipping or masking and
  are not clean-body candidates merely because their server IFF name is generic.
- No clean or generic-undressed Chiss mobile IFF exists in the checked Pre-CU
  object set. Every Chiss mobile references a dressed/special client CDF. A
  Chiss that always displays the selected rank robe therefore requires a new
  client shared-mobile IFF/CDF (and client TRE distribution); server Lua cannot
  remove CDF-baked clothing after creation.
- The later implementation found a better stock source than the ordinary base
  mobiles: `object/mobile/vendor/shared_*.iff` provides clean, empty-CDF client
  bodies for every playable species/gender. Ranked NPC server aliases can
  inherit those shared bodies without inheriting `VENDORCREATURE` or any vendor
  components.

### 6.8.4 Ranked clean-body groups and randomized appearance (implemented 2026-07-12)

Live forced-IFF tests confirmed correct robe rendering on:

- `huff_darklighter.iff` (Human male);
- `kaja_orzee.iff` (Human female);
- `kardeer.iff` (Rodian male);
- `wookiee_male.iff` and `wookiee_female.iff`;
- `ithorian_male.iff`.

`dressed_ruffian_zabrak_male_01.iff` rendered the selected robe but appeared as
a bald Human without Zabrak horns or facial markings. Its generic CDF therefore
does not provide a complete usable Zabrak presentation for an AiAgent. The final
implementation does not use that body: it uses the clean stock vendor shared
Zabrak body plus the stock Zabrak vendor customization/hair profile. The Chiss
constraint from §6.8.3 is unchanged because Core3 has no Chiss vendor body or
appearance profile.

`object/mobile/ranked_jedi_bodies.lua` registers 20 ordinary mobile aliases over
the stock clean vendor shared client bodies: male and female Human, Bothan,
Ithorian, Mon Calamari, Rodian, Sullustan, Trandoshan, Twi'lek, Wookiee, and
Zabrak. The file is loaded immediately after `mobile/vendor/serverobjects.lua`,
so all referenced shared body objects already exist. These aliases inherit only
the shared client body; they do not set `templateType = VENDORCREATURE` and do
not copy vendor menu/data/container/zone components.

Two new dress groups are loaded from `mobile/dressgroup/serverobjects.lua`:

- `ranked_dark_jedi`: 10 entries weighted by deliberate repetition to 80%
  Human and 20% Zabrak, evenly split by gender inside each species.
- `ranked_light_jedi`: all 20 stock playable species/gender aliases with equal
  selection probability.

Clean shared bodies have no CDF-provided instance customization, so without a
second step they all use default skin, face, body, and hair values. Ranked Jedi
initialization now resolves the selected alias to the corresponding stock
`object/mobile/vendor/<species>_<gender>.iff` `VendorCreatureTemplate` and
reuses its appearance data as a read-only profile:

- every configured palette, texture, face, and body morph is randomized from
  the profile's valid `customizationValues`;
- comma-separated mutually-exclusive morphs such as fat/skinny use the same
  one-side-nonzero handling as `VendorManager::randomizeVendorFeatures`;
- height is selected from the profile's stock min/max scale range;
- one stock species/gender hair tangible is selected when the profile has hair,
  transferred to the AiAgent's `hair` slot, and explicitly added to the CREO6
  wearables vector just like the ranked robe. Species without separate hair
  objects still receive their full body/fur/head customization.

The new structured log prefix is `[RankedJediAppearance]`. A normal customized
spawn reports `event=randomized`, the base body, resolved vendor profile,
number of applied customization variables, chosen height, selected hair IFF,
and `hairTracked`. Failures report `profile_missing` or `hair_failed` without
preventing the rank robe from being equipped.

Only the nine creature templates that already opt into `frsRankOutfits` now
reference these groups: four imperial dark templates, the thug dark sentinel,
and four rebel light templates. The existing `dark_jedi` and `light_jedi`
groups remain unchanged for ordinary/quest/trainer NPCs. Because this change
is called only inside enabled ranked-Jedi initialization and reads vendor
profiles without modifying them, it does not touch player creature templates,
player creation, player containers, ordinary vendors/NPCs, or player equipment.

Post-deployment verification: spawn several creatures from each ranked template
without an object-IFF override. The `[RankedJediOutfit] event=spawn_config`
`baseMobile` field must contain only an entry from the appropriate ranked group,
and each must have a matching `[RankedJediAppearance] event=randomized` line.
Human/Zabrak profiles with hair should report `hairTracked=true`; species whose
profile has no hair correctly report `<none>`/`false`. The robe's
`wearablesTracked` must remain true. `wearablesCount` is normally 2 when both
hair and robe are equipped, or 1 for species without a separate hair object.
Visually verify varied skin/fur, faces, builds, heights, and hair as well as the
correct rank robe.

### 6.8.5 Ranked light-Jedi saber colors (implemented 2026-07-12)

NPC sabers previously retained their weapon template's default red blade because
none of the ranked creature templates set the existing single
`lightsaberColor` field. The stock client color IDs are named by
`jedi_spam:saber_color_*`: 0/1 red, 2/3 green, 4/5 blue, 6/7 yellow, and 8/9
purple (followed by orange/brown and named special crystals).

`CreatureTemplate` now supports an optional integer list named
`lightsaberColors`. When an AiAgent creates a Jedi weapon, it chooses one list
entry and applies that ID to both `WeaponObject::bladeColor` and
`/private/index_color_blade`. The existing singular `lightsaberColor` remains
the fallback, so every creature template without the new list is unchanged.
Lists can deliberately repeat IDs later if weighted colors are desired.

`lightJediSaberColors = { 2, 4, 6, 8 }` is defined beside the ranked robe maps
and assigned only to the four ranked rebel/light creature templates. They
therefore choose equally between Light Green, Blue, Yellow, and Light Purple.
The five ranked dark templates do not set a pool and retain their current red
weapon-template blade.

This is entirely inside AiAgent weapon creation. It does not create or insert a
Force color crystal, mutate the shared weapon group, or change player saber and
crystal handling. Startup logs report `[RankedJediSaber]
event=color_pool_loaded`; each generated light saber reports
`event=color_selected` with the agent, creature template, weapon IFF, and color
ID. Post-build verification should spawn several light Jedi and observe all
four colors over a sufficient sample while dark Jedi remain red.


## 7. Owner decisions (ANSWERED 2026-07-07)

1. Tier→rank map: **knight first, named council bands next, master final**.
   The as-built templates are authoritative: light 0 / 1–4 / 5–7 / 11 and
   dark 0 / 1–4 / 5–7 / 8–10 (§6.5). These ranges match the side-specific FRS
   title ladders in the owner-supplied tables.
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
