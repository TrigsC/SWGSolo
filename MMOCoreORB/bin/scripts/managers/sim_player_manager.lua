-- scripts/managers/sim_player_manager.lua

--print("LUA DEBUG: SimPlayerManagerConfig Loaded")
SimPlayerManagerConfig = {
    -- MASTER SWITCH
    enabled = true,

    -- Recommended testing modes:
    -- observe: resource/demand/market logs only; no targeting or activation.
    -- shadow: assignment and would-activate diagnostics; actualActivation=false.
    -- limited: capped miners may move to verified assignments; yield remains conceptual.
    -- soak: limited mode plus health summaries, cooldowns, caps, and emergency latch.
    --
    -- D.6.6 demand-weighted planning is the canonical planner for intelligent
    -- SimMiner targeting. D.3 recommendations and D.4 round-robin simulation
    -- are retained as legacy/resource-intelligence diagnostics.

    -- Read-only resource intelligence observability. Disabled by default.
    resourceIntelligenceConfig = {
        enabled = true,
        logTopResources = false,
        summaryIntervalSeconds = 300,
        topN = 10,
    },

    -- Read-only curated resource scoring profiles. Requires resourceIntelligenceConfig.enabled.
    resourceScoringProfiles = {
        enabled = false,
        profiles = {
            {
                key = "weaponsmith_dl44",
                category = "weaponsmith",
                description = "DL44-style weapon profile",
            },
            {
                key = "chef_ahrisa",
                category = "chef",
                description = "Ahrisa food profile",
            },
            {
                key = "architect_mining_unit",
                category = "architect",
                description = "Mining unit component profile",
            },
        },
    },

    -- D.3 legacy diagnostic: per-profile recommendations for resource-intelligence debugging.
    -- Disabled by default and does not change miner behavior.
    minerTargetRecommendationConfig = {
        enabled = false,
        intervalSeconds = 30,
        topN = 1,
        profiles = {
            "weaponsmith_dl44",
            "chef_ahrisa",
            "architect_mining_unit",
        },
        includeAllActiveMiners = false,
    },

    -- D.4 legacy diagnostic: round-robin single target plans.
    -- D.6.6 is the canonical planner for intelligent targeting.
    -- Disabled by default and never changes miner state.
    minerTargetSimulationConfig = {
        enabled = false,
        intervalSeconds = 30,
        profileWeights = {
            weaponsmith_dl44 = 1.0,
            chef_ahrisa = 1.0,
            architect_mining_unit = 1.0,
        },
        preferSamePlanet = false,
        samePlanetBonus = 150,
        travelPenalty = 100,
        assignmentMode = "round_robin",
    },

    -- Simulation-only density pocket search. Uses the D.6.6 demand-weighted
    -- plan when available, with the older D.4 plan only as a diagnostic fallback.
    minerDensityTargetSimulationConfig = {
        enabled = true,
        intervalSeconds = 60,
        searchRadii = { 250, 500, 1000, 2000 },
        samplesPerRadius = 48,
        minAcceptableDensity = 0.65,
        preferredDensity = 0.80,
        requireNavmesh = false,
        maxPathCheckAttempts = 8,
        distancePenaltyPerMeter = 0.02,
    },

    -- P.2/D.8.2 shadow-mode NavArea-backed pathable density staging.
    -- This never changes miner targets while enableNavAreaDensityShadowMode=true.
    navAreaDensitySelectionConfig = {
        enableNavAreaDensitySelection = false,
        enableNavAreaDensityShadowMode = true,
        navAreaSampleCacheTtlSeconds = 900,
        navAreaMaxSamplesPerArea = 8,
        navAreaMaxSampleAttemptsPerCycle = 16,
        navAreaMaxPathValidationsPerCycle = 0,
        navAreaAvoidGenericInteriors = true,
        navAreaPreferCityAndPoiRegions = true,
    },

    -- P.2.6 runtime-only reachability memory. Memory collection is enabled so
    -- the dashboard can learn which buckets validate; candidate preference is
    -- shadow-only until explicitly enabled.
    reachabilityMemoryConfig = {
        enableReachabilityMemory = true,
        enableReachabilityCandidatePreference = true,
        reachabilityMemoryTtlSeconds = 1800,
        reachabilityBucketSizeMeters = 128,
        minAttemptsBeforePenalty = 3,
        verifiedPathScoreBonus = 0.15,
        sampleCompleteScoreBonus = 0.25,
        repeatedFailurePenalty = 0.25,
        longDistancePenalty512Plus = 0.15,
        planetPenaltyEnabled = true,
        resourcePenaltyEnabled = true,
        maxReachabilityMemoryRows = 5000,
    },

    -- Simulation-only route validation for D.6.6-aligned density coordinates.
    minerPathValidationSimulationConfig = {
        enabled = true,
        intervalSeconds = 60,
        validateOnlyAcceptedDensityTargets = true,
        maxPathDistance = 2500,
        maxPathNodes = 256,
    },

    -- P.4 overland travel. In SWG, NPCs/vehicles climb near-vertical cliffs and
    -- cross water freely, so terrain slope and mid-route water never block
    -- travel. The only overland blockers are the target being out of bounds, or
    -- the resource pocket itself sitting over open water (the miner would sample
    -- the outskirts or the planner picks another pocket).
    --
    -- P.4.2: enableOverlandActivation lets an overland-reachable off-navmesh
    -- target validate under the directOverland trust tier and ACTIVATE, so
    -- miners actually walk to wilderness resources (walk speed; vehicles are a
    -- later phase). rejectWaterTargets also makes the density-target search skip
    -- water pockets so the planner stops assigning them. Still no economy/
    -- inventory/persistence mutation. See docs/ai-miner-navigation-design.md.
    travelConfig = {
        enableOverlandDiagnostics = true,
        enableOverlandActivation = true,  -- P.4.2: allow activation to overland-reachable targets
        rejectWaterTargets = true,  -- flag + skip targets sitting over open water
        waterMarginMeters = 1.0,    -- water depth over terrain to count as "in water"
        -- P.4.5a station/shuttle travel: at activation, if a shuttle/starport is
        -- much closer to the target than the miner, "take the shuttle" -- teleport
        -- (switchZone) to the station's OUTDOOR arrival point, then the miner only
        -- walks the short last leg overland (never traverses the un-navmeshed
        -- starport interior). Same-planet only, safe reposition. Set false to hold.
        enableStationTravel = true,
        stationMinSavingMeters = 400,  -- only ride if it cuts at least this much overland distance
        -- P.4.5b cross-planet dispatch (proportional rebalance, player-mimetic).
        -- When a super-valuable resource is only reachable on an under-covered
        -- planet (e.g. Looveaveyl on Dathomir), pick one idle miner per interval,
        -- have it RUN to its nearest starport's ticket collector, then board =
        -- teleport (switchZone) to the destination starport's OUTDOOR arrival, and
        -- gather there. Proportional to per-planet demand, with a home-planet floor
        -- so Naboo/Tatooine/Corellia are never stripped. Ships INERT: master flag
        -- off + dryRun on. Rollout: set enablePlanetDispatch=true first and watch
        -- planetDispatch.byPlanet on the dashboard; then set dryRun=false to travel.
        enablePlanetDispatch = true,          -- master gate (default off)
        planetDispatchDryRun = false,           -- true = compute+expose only, no travel
        planetDispatchIntervalSeconds = 60,    -- decision cadence
        planetDispatchMinDemandScore = 750,    -- only dispatch for high-value demand
        planetDispatchMinMinersPerHomePlanet = 2,   -- floor kept on each spawn planet
        planetDispatchMaxMinersPerRemotePlanet = 3,  -- cap per remote planet
        planetDispatchPerMinerCooldownSeconds = 900, -- don't re-dispatch a miner too often
        planetDispatchPerPlanetCooldownSeconds = 300,-- don't thrash a destination planet
        planetDispatchBoardRadiusMeters = 20,  -- "reached the ticket collector" radius
    },

    -- P.4.4a real vehicle mechanics. NPCs spawn a real speeder + control device,
    -- mount it, (drive later), dismount, and store/destroy it -- the same object
    -- lifecycle a player uses, no fake speed. This phase only proves the
    -- spawn/mount/dismount/store plumbing via a gated self-test on a stationed
    -- miner. Simulation-only object lifecycle: no inventory/economy/persistence
    -- mutation, transient (persistence 0) vehicles. Driving is P.4.4b.
    -- HISTORY: disabled after P.4.4a first run -- transferObject into the RIDER
    -- slot failed because NPC mobile templates had no arrangement descriptors
    -- (players get "rider" from abstract/slot/arrangement/player.iff). Fixed by
    -- adding arrangementDescriptorFilename to the six artisan dressed_* object
    -- templates (see docs/npc-mount-and-player-dot-plan.md). The hardened
    -- teardown (miner always pulled back to world before vehicle destruction)
    -- remains in place, so a failed mount can no longer orphan a miner.
    vehicleConfig = {
        enableVehicleMechanics = true,   -- master switch
        vehicleObjectTemplate = "object/mobile/vehicle/speederbike_swoop.iff",
        controlDeviceTemplate = "object/intangible/vehicle/speederbike_swoop_pcd.iff",
        -- P.4.4a self-test proved deploy/mount/dismount/store (3/3 cycles clean
        -- on 2026-07-02); OFF now that mounted travel exercises the same
        -- plumbing on every long leg.
        selfTestEnabled = false,
        selfTestIntervalSeconds = 120,
        selfTestHoldSeconds = 10,
        -- P.4.4b mounted travel: miners deploy+mount the swoop for any movement
        -- leg longer than mountedTravelMinLegMeters, ride it at the vehicle's
        -- real run speed (client sees the swoop move via its own transform
        -- updates, rider seated), and dismount+store at arrival/station, path
        -- failure, recovery, shuttle boarding, or sampling.
        enableMountedTravel = true,
        mountedTravelMinLegMeters = 150,
    },

    -- Client presentation only. When true, every sim NPC spawned by this manager
    -- (miners and PvP bots) gets ObjectFlag::PLAYER (0x10) added to its
    -- pvpStatusBitmask so game clients render it like a player: player-style
    -- radar/map dot (blue neutral, purple ally, red attackable) and player
    -- con-color rules. Cosmetic -- no server gameplay logic reads this bit on
    -- AiAgents. Applied at spawn; changing it needs a restart (NPCs respawn).
    presentationConfig = {
        showSimNpcsAsPlayerDots = true,
    },

    -- Intelligent targeting switch. "off" does nothing, "shadow" logs decisions,
    -- "limited" may move capped miners only when limitedActivationConfig.enabled=true,
    -- and "soak" is a C++ alias for limited mode with these conservative soak controls.
    -- requireValidPath requires pathTrustStatus=verifiedPath before activation.
    minerIntelligentTargetingConfig = {
        enabled = true,
        mode = "soak",
        intervalSeconds = 60,
        -- Number of miners evaluated by D.5.3/D.5.5 switch-decision and
        -- assignment logic per interval. This is not the active mover cap.
        maxActiveMiners = 11,
        requireDemandWeightedPlan = true,
        requireAcceptedDensityTarget = true,
        requireValidPath = true,
        -- P.4.5c final approach: how close (m) a miner must get to the true target
        -- before it stations. Long off-navmesh walks can terminate short; the miner
        -- re-paths (bounded) to close the gap. The pocket is a planet-wide spawn so
        -- ~10-15 m short is fine; keep this below the recovery farFromStation
        -- threshold (32) so a completed approach is never flagged as stuck.
        arrivalRadiusMeters = 15,
        fallbackToConceptualLoop = false,
        rollbackOnFailureCount = 3,
        logDecisionSummary = true,
        -- Full per-miner switch lines are otherwise emitted only for useful
        -- transitions/failures/activation-capable decisions.
        logVerboseSwitchDecisions = false,
        assignmentConfig = {
            enabled = true,
	            -- Retained target lifetime. Keep this comfortably above the
	            -- 60s planning/validation cadence so verified assignments do
	            -- not expire while waiting for the limited activation lane.
	            ttlSeconds = 600,
	            candidateAssignmentTtlSeconds = 600,
	            validatedAssignmentTtlSeconds = 600,
	            queuedActivationTtlSeconds = 240,
	            movementArrivalTimeoutSeconds = 600,
	            movementArrivalTimeoutMinSeconds = 240,
	            movementArrivalTimeoutMaxSeconds = 1200,
	            movementArrivalSecondsPerMeter = 0.75,
	            sampleStartedTimeoutSeconds = 180,
	            -- Active movement uses lifecycle-specific timeouts instead of
	            -- being cleared by stale candidate/validated assignment TTL.
	            preventNormalTtlForActiveMovement = true,
	            replaceOnlyWhenExpiredOrInvalid = true,
            -- Only assignment-aware intelligent samples clear retained assignments;
            -- the normal conceptual sample loop does not touch this cache.
            clearOnSampleComplete = true,
            clearOnCombat = true,
            clearOnIncapOrDeath = true,
            clearOnZoneChange = true,
            -- Lifecycle logs keep creation/update/failure/clear events visible.
            logAssignmentLifecycle = true,
            -- Retained logs are noisy during soak; leave false unless tracing TTL drift.
            logRetainedAssignments = false,
            -- P.2.2/P.2.3 read-only readiness check for future forced movement.
            movementReadinessDiagnosticsEnabled = true,
        },
        limitedActivationConfig = {
            enabled = true,
            -- Number of miners currently queued, moving, or sampling through
            -- the intelligent assignment path.
            maxActiveIntelligentMiners = 10,
            -- Number of new intelligent activations accepted in one manager interval.
            maxActivationsPerInterval = 2,
            -- Per-miner cooldown after accepted activation. Zero preserves current behavior.
            cooldownSecondsPerMiner = 0,
            -- Empty means all zones are allowed. Non-empty entries are zone names.
            allowedZones = {},
            requireSamePlanet = true,
            -- Stops additional activation attempts for the current interval after
            -- a real activation failure. Controlled skips do not trip this.
            disableOnFirstActivationFailure = true,
            -- Emergency latch for activation failures; reset by disabling/changing config.
            disableOnActivationFailure = false,
            logActivationLifecycle = true,
            -- Primary D.5 limited-activation soak summary.
            logHealthSummary = true,
        },
    },

    legacyMinerLoopConfig = {
        enableLegacyConceptualLoop = false,
        allowLegacyFallbackWhenNoIntelligentAssignment = false,
        allowLegacyFallbackAfterIntelligentFailure = false,
        logLegacySuppression = false,
    },

    -- Log-only hot-item demand pressure. This does not feed miner target selection.
    demandProfileSimulationConfig = {
        enabled = true,
        intervalSeconds = 30,
        serverPhase = "mature_server",
        logTopN = 3,
        profiles = {
            composite_armor_supply = {
                enabled = true,
                weight = 1.0,
                priority = 100,
            },
            master_weaponsmith_staples = {
                enabled = true,
                weight = 1.0,
                priority = 80,
            },
            high_damage_weapon_components = {
                enabled = true,
                weight = 1.0,
                priority = 90,
            },
            chef_buff_foods = {
                enabled = true,
                weight = 1.0,
                priority = 70,
            },
            chef_high_value_consumables = {
                enabled = true,
                weight = 1.0,
                priority = 70,
            },
            production_infrastructure = {
                enabled = true,
                weight = 1.0,
                priority = 85,
            },
        },
    },

    -- Log-only reserve pressure using conceptual miner totals and active resource opportunities.
    demandStateSimulationConfig = {
        enabled = true,
        intervalSeconds = 30,
        logTopN = 6,
        supplyMode = "conceptual_totals",
        activeOpportunityWeight = 1.0,
        shortageWeight = 1.0,
        surplusDampening = 0.5,
        profiles = {
            composite_armor_supply = {
                enabled = true,
                desiredReserve = 100000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            master_weaponsmith_staples = {
                enabled = true,
                desiredReserve = 100000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            high_damage_weapon_components = {
                enabled = true,
                desiredReserve = 100000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            chef_buff_foods = {
                enabled = true,
                desiredReserve = 100000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            chef_high_value_consumables = {
                enabled = true,
                desiredReserve = 75000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            production_infrastructure = {
                enabled = true,
                desiredReserve = 200000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
        },
    },

    -- Read-only observation of public resource listings on bazaars and player vendors.
    marketSupplyObservationConfig = {
        enabled = false,
        intervalSeconds = 300,
        maxListingsScanned = 5000,
        includeBazaar = true,
        includePlayerVendors = true,
        includeVendorStockrooms = false,
        includePlayerInventory = false,
        includePrivateContainers = false,
        resolveResourceContainers = false,
        startupDelaySeconds = 900,
        minQuantity = 1,
        logTopN = 5,
    },

    -- Memory-only stockpile-shaped diagnostics. Never imports market supply or persists rows.
    stockpileSnapshotSimulationConfig = {
        enabled = true,
        intervalSeconds = 300,
        logTopN = 10,
        includeConceptualMinerTotals = true,
        includeMarketObservation = false,
    },

    -- Hive stockpile persistence (galaxy-scoped, ownerScope="galaxy").
    -- persistConceptualMinerTotals: coarse one-lot-per-resource-type rollup.
    -- persistSpawnIdentifiedLots (P.5.1): crafting-grade exact lots keyed by
    --   resource-spawn object id, carrying the 10 resource stats. Writes only to
    --   the isolated aieconomy/aieconomylots databases (no game-state mutation).
    aiEconomyPersistenceConfig = {
        persistConceptualMinerTotals = false,
        persistSpawnIdentifiedLots = true,
        intervalSeconds = 300,
        logSummary = true,
    },

    -- P.5.2 hive reservation ledger self-test. Non-destructive: reserves a small
    -- quantity from any eligible exact lot then releases it (never consumes), so
    -- the reserve/release accounting can be verified before a real crafter
    -- consumer (P.5.3) exists. Simulation-only; mutates no game state. Retired
    -- now that the P.5.3 crafter consumer drives real reservations.
    hiveReservationSelfTestConfig = {
        enabled = false,
        intervalSeconds = 120,
        reserveQuantity = 5,
    },

    -- P.5.3 first crafter consumer + P.5.4a/b type-correct crafting into the
    -- finished-goods ledger. Each tick it picks the highest-pressure demand
    -- profile with an active resource opportunity, reserves+CONSUMES a batch of
    -- profession-correct raw stock, and deposits the recipe's finished-good
    -- output as a finished_good hive lot.
    -- Simulation-only: consume/produce touch the private aieconomy/aieconomylots
    -- ledger only -- no ResourceContainer, market, or credit state is touched.
    --   craftBatchQuantity     : legacy flat batch (used when a recipe is absent
    --                            or produceFinishedGoods is off).
    --   minOq                  : minimum resource overall-quality (0 = any).
    --   preferShortageProfiles : prioritise critical/low profiles on first pass.
    --   allowAnyLotFallback    : RETIRED by P.5.4a (false): a profile without
    --                            type-correct stock now skips instead of drawing
    --                            wrong stock (e.g. chef consuming metal).
    --   useFamilyMatching      : P.5.4a -- reserve via the profile's exact-type/
    --                            family candidate list (class-chain matching).
    --   produceFinishedGoods   : P.5.4b -- consume raw -> produce finished-good
    --                            lots per the recipes below.
    --   recipes                : per-profile single-input recipe. Fields:
    --                            goodKey/goodName/goodClassChain,
    --                            inputUnitsPerCraft, outputUnitsPerCraft,
    --                            finishedGoodTargetUnits (enforced by the
    --                            P.5.4d output governor; surfaced now).
    hiveCrafterConsumerConfig = {
        enabled = true,
        intervalSeconds = 90,
        craftBatchQuantity = 25,
        minOq = 0,
        preferShortageProfiles = true,
        allowAnyLotFallback = false,
        useFamilyMatching = true,
        produceFinishedGoods = true,
        recipes = {
            composite_armor_supply = {
                goodKey = "composite_armor_segment",
                goodName = "Composite Armor Segment",
                goodClassChain = "crafted.armor",
                inputUnitsPerCraft = 25,
                outputUnitsPerCraft = 1,
                finishedGoodTargetUnits = 200,
            },
            master_weaponsmith_staples = {
                goodKey = "weapon_staple_stock",
                goodName = "Weapon Component Stock",
                goodClassChain = "crafted.weapon",
                inputUnitsPerCraft = 25,
                outputUnitsPerCraft = 1,
                finishedGoodTargetUnits = 200,
            },
            high_damage_weapon_components = {
                goodKey = "high_damage_weapon",
                goodName = "High-Damage Weapon",
                goodClassChain = "crafted.weapon",
                inputUnitsPerCraft = 25,
                outputUnitsPerCraft = 1,
                finishedGoodTargetUnits = 100,
            },
            chef_buff_foods = {
                goodKey = "buff_food",
                goodName = "Buff Food",
                goodClassChain = "crafted.food",
                inputUnitsPerCraft = 25,
                outputUnitsPerCraft = 1,
                finishedGoodTargetUnits = 200,
            },
            chef_high_value_consumables = {
                goodKey = "high_value_consumable",
                goodName = "High-Value Consumable",
                goodClassChain = "crafted.food",
                inputUnitsPerCraft = 25,
                outputUnitsPerCraft = 1,
                finishedGoodTargetUnits = 100,
            },
            production_infrastructure = {
                goodKey = "factory_component",
                goodName = "Factory Component",
                goodClassChain = "crafted.structure",
                inputUnitsPerCraft = 25,
                outputUnitsPerCraft = 1,
                finishedGoodTargetUnits = 300,
            },
        },
    },

    -- Read-only D.6.2 consumer for validated durable conceptual stockpile baseline.
    persistentStockpileDemandConfig = {
        enabled = true,
        includeConceptualMinerLots = true,
        logSummary = true,
    },

    -- Log-only D.6 demand-pressure plans. Never assigns targets or changes miner behavior.
    demandWeightedMinerPlanSimulationConfig = {
        enabled = true,
        intervalSeconds = 60,
        logTopN = 20,
        samePlanetBonus = 150,
        travelPenalty = 100,
        maxMinersPerProfile = 2,
        minimumPressureThreshold = 1.0,
        strongPressureRatio = 1.5,
    },

    -- Dashboard-only future travel planning. This never moves, despawns,
    -- respawns, sells, or changes miner behavior.
    aiTravelSimulationConfig = {
        enabled = true,
        maxPlans = 20,
        includeResourceRushPlans = true,
        includeHubReturnPlans = true,
        homeHub = {
            enabled = true,
            key = "coronet_resource_hub",
            zone = "corellia",
            city = "coronet",
            -- Same approximate staging point as shuttleports.corellia.coronet.hangout.
            x = -155.0,
            y = -4722.0,
            purpose = "sell_resources",
        },
    },

    -- P.3.1-P.3.4.2 coverage-oriented miner lifecycle.
    -- Stationed miners retain useful exact-resource assignments. Repeated
    -- samples remain separately gated, simulation-only, and use Core3 player
    -- sampling timing internally instead of Lua tuning knobs.
    stationedMinerConfig = {
        enableStationedLifecycle = true,
        enableStationedRepeatedSampling = true,
        stationedRequireDemandStillValid = true,
        stationedRequireResourceStillActive = true,
        stationedRequireSamePlanet = true,
        stationedClearWhenReserveSatisfied = true,
    },

    -- P.3.3 acquisition readiness diagnostics only. Real acquisition remains
    -- disabled and has no resource/container/inventory/economy mutation path.
    -- P.3.4 simulated acquisition records exact spawned resources into a
    -- runtime-only ledger; it still never creates ResourceContainers or
    -- mutates inventory, vendors, market, crafting, credits, or persistence.
    realResourceAcquisitionConfig = {
        enableRealResourceAcquisition = false,
        acquisitionReadinessDiagnosticsEnabled = true,
        enableSimulatedAcquisitionTransactions = true,
        simulatedAcquisitionLogTransactions = true,
        simulatedAcquisitionMaxLedgerEvents = 200,
        requireStationedLifecycle = true,
        requireVerifiedActivationPath = true,
        requireKnownResourceSpawnIdentity = true,
        requireDemandStillValid = true,
        requireReserveBelowTarget = true,
        maxAcquisitionsPerInterval = 0,
    },

    -- P.3.4.4 / P.4.3 miner self-healing. Scoped live recovery: when dryRun is
    -- false, the only action taken is clearAssignment (nudge/teleport/respawn
    -- stay off), which also resets the miner's controller so it un-stations and
    -- re-acquires a fresh reachable target. Rate-limited per interval and per
    -- miner/hour. No inventory/economy/persistence mutation. The main case it
    -- fixes is a "stationed far from target" miner that was reassigned to a far
    -- target while stationed and could not be pulled back into the move loop.
    minerRecoveryConfig = {
        enabled = true,
        dryRun = false,
        allowClearAssignment = true,
        allowNudgeToSafeNearbyPoint = false,
        allowTeleportToStationTarget = false,
        allowRespawnReplacement = false,
        adminActionsEnabled = false,
        stuckCheckIntervalSeconds = 60,
        movingStuckSeconds = 180,
        stationedSamplingGraceSeconds = 90,
        farFromStationDistanceMeters = 32,
        maxAutomaticRecoveriesPerInterval = 2,
        maxRecoveriesPerMinerPerHour = 3,
        logRecoveryDecisions = true,
    },

    -- Configured SimPlayers spawn immediately, but in small batches. This keeps
    -- miners available for planner/acquisition soak while avoiding one large
    -- AiAgent creation burst.
    spawnStartupConfig = {
        startupDelaySeconds = 0,
        batchSize = 5,
        batchDelayMs = 1000,
    },

    -- P.6.1 SimPvP squads: persistent faction squads (leader + followers)
    -- that shuttle into a city, run to the starport hangout, loiter looking
    -- for attackable enemies, then shuttle to the next city (switchZone
    -- travel - the legacy destroy+respawn pvp_solo loop is retired).
    -- All C++ defaults are OFF; this table is refreshed every
    -- maintenanceIntervalSeconds, so every knob (including enablePvpBots)
    -- applies at runtime without a restart.
    pvpConfig = {
        enablePvpBots = true,           -- master gate
        squadsPerFaction = 3,           -- owner-approved starting population
        squadSize = 4,                  -- leader + 3 followers
        scanRadiusMeters = 64,          -- how close before a bot ENGAGES
        -- Disengage distance: a bot in combat whose target flees beyond this
        -- (just above effective ranged weapon range) drops combat instead of
        -- chasing/attacking across the map at 100m+. Also caps stalemates.
        combatLeashMeters = 72,
        loiterMinSeconds = 60,
        loiterMaxSeconds = 180,
        -- Owner-approved to enable AFTER the P.6.1 soak: opposing sim squads
        -- fight each other (visible PvP with zero players online). Flip at
        -- runtime; no restart needed.
        allowBotVsBotCombat = true,
        logStateTransitions = true,     -- verbose while we verify P.6.1
        respawnDelaySeconds = 120,      -- full-wipe squad re-form delay
        maintenanceIntervalSeconds = 30,
        shuttleWaitIntervalSeconds = 5,
        shuttleWaitMaxAttempts = 24,    -- ~2min, then board anyway
        corpseCleanupDelaySeconds = 20,
        recovery = {
            enabled = true,
            dryRun = true,              -- observe first; then allow teleports
            memberFarMeters = 64,
            stateTtlSeconds = 600,
            maxActionsPerInterval = 2,
        },
        -- P.6.5d break-off cohesion: two deaths in a rolling window send the
        -- squad back to the shuttle and steer its next route away from the
        -- killzone. C++ defaults remain off until this block enables it.
        cohesion = {
            breakOff = true,
            breakOffDeaths = 2,
            breakOffWindowSeconds = 120,
            avoidCitySeconds = 600,
        },
        -- P.6.2 scouts + gank convergence: small scout squads run the same
        -- city loop but REPORT enemy contacts instead of engaging; the
        -- nearest eligible patrol squad of that faction breaks off, runs to
        -- its shuttle, and travels to the contact's city. Any squad that
        -- engages a real PLAYER also calls it in. Cooldowns per squad and
        -- per city stop ping-pong; contacts expire on their own.
        scouts = {
            enabled = false,
            squadsPerFaction = 1,
            squadSize = 1,              -- lone scout (players use 1-2)
            scanRadiusMeters = 64,      -- scouts watch a wider bubble
            reportOnly = true,          -- scouts observe + call it in
            reportIntervalSeconds = 30,
            contactTtlSeconds = 300,
            convergeCooldownSeconds = 600,
        },
        -- P.6.3a player-facing comms: squad LEADERS speak in spatial chat on
        -- key events (post up at a starport, area clear, contact!, moving to
        -- reinforce) so nearby players hear the PvP happening. Rate-limited
        -- per squad + globally. No new client commands. Later sub-phases add
        -- faction chat rooms (6.3b) and join-a-squad + group-chat keywords
        -- (6.3c, the two gated core patches).
        comms = {
            spatialAnnouncements = true,
            announceCooldownSeconds = 45,  -- per-squad min gap between shouts
            globalMinGapSeconds = 4,       -- any-squad min gap (anti-spam)
            -- P.6.3b faction chat rooms: creates "GCWRebel" / "GCWImperial"
            -- channels (chat browser path SWG.<galaxy>.GCWRebel / .GCWImperial)
            -- that leaders post arrivals/contacts/reinforcements to with city
            -- context. Entry is gated to the room's faction so the enemy can't
            -- read your channel (an alt of the SAME faction still can - that's
            -- inherent). factionRoomRequireOvert additionally requires the
            -- joiner be OVERT. Rooms are moderated (players read only; only the
            -- squad command feed posts).
            factionRooms = true,
            factionRoomRequireOvert = false,
            -- P.6.3c player grouping: a player can join a squad's own group.
            -- Say "join pvp group" in spatial chat near a same-faction squad
            -- (or "join group with <name>" naming one of its members), and the
            -- squad leader invites you. The group is the NPC leader + players
            -- only. If the leader dies the squad's new NPC leader takes over
            -- the group (a player never becomes leader); if the whole squad
            -- wipes the group disbands. Leave with the normal /leavegroup.
            -- Once grouped, type "status" or "where" in group chat for a reply.
            playerGrouping = true,
            maxPlayersPerSquad = 5,
            joinRangeMeters = 48,
            -- P.7.4c: NPCs have no client, so their Force Armor/Shield/Absorb
            -- mitigation was invisible to players ("is it even working?").
            -- When true, the attacking PLAYER receives the mitigation combat
            -- spam instead — you SEE the bot's barrier absorbing your hits.
            showNpcMitigation = false,
        },
        -- P.7.4 ranked NPC Jedi. Template entries accept either legacy strings
        -- or weighted tables; P.7.4b enables player FRS XP from ranked NPC kills.
        jediRanks = {
            -- P.7.4c: stock combat code skipped ALL jedi buff mitigation for
            -- NPC defenders (early return after template armor) — Force
            -- Armor/Shield casts were dead buffs. This applies them to
            -- AiAgent defenders exactly like players (armor vs non-force
            -- damage, shield vs force damage), before template armor.
            npcMitigation = true,
            enableRankedJedi = true,
            frsFromNpcJedi = true,
            npcXpFactor = 1.0,
            npcFrsXpDailyCap = 0,
            minContributionPct = 0.10,
        },
        templates = {
            imperial = {
                { template = "stormtrooper", weight = 35 },
                { template = "imperial_dark_jedi_knight", weight = 28 },
                { template = "imperial_dark_jedi_enforcer", weight = 19 },
                { template = "imperial_dark_jedi_templar", weight = 12 },
                { template = "imperial_dark_jedi_master", weight = 6 },
            },
            rebel = {
                { template = "rebel_trooper", weight = 35 },
                { template = "light_jedi_knight", weight = 28 },
                { template = "light_jedi_sentinel", weight = 19 },
                { template = "light_jedi_consular", weight = 12 },
                { template = "light_jedi_master", weight = 6 },
            },
        },
        -- P.6.5 player-mimetic routed travel (design doc
        -- ai-pvp-mimetic-travel-design.md). Departures can run to the
        -- configured starport's ticket collector, including Theed's
        -- pathable in-cell collector. The resolver caches its world and
        -- cell-local coordinates and falls back to the city pad when no
        -- collector is usable. This remains simulation-only: no ticket,
        -- credit, inventory, or player-state mutation occurs.
        travel = {
            -- P.6.5a routed travel (owner-approved): squads travel like
            -- players. Intra-planet legs are open; cross-planet legs need
            -- both cities' starports AND a fare-matrix route (naboo/tatooine/
            -- corellia interconnect; restuss requires the naboo hop). Routes
            -- are BFS-planned over the city pool below; connection stops wait
            -- briefly at the pad for the next ship. Leaders announce the full
            -- route on boarding (spatial + faction room + group chat) so
            -- players can buy the same tickets and follow.
            enableRoutedTravel = true,
            mainPlanets = { "naboo", "corellia", "tatooine" },
            offMainPlanetChancePct = 25,  -- restuss etc. accepted this often
            maxLegsPerRoute = 3,
            transitDwellSecondsMin = 20,
            transitDwellSecondsMax = 45,
            -- P.6.5b: interplanetary departures run to the actual ticket
            -- collector. Set false to restore pad-only departures at runtime.
            useCollectorBoarding = true,
            -- Formalizes the existing real-ship wait. Set false as an escape
            -- hatch to board on the first wait tick without a ship present.
            boardOnActualShuttle = true,
            -- 0.2.1: squads hold at the pad at least this long after the
            -- MOVEOUT route callout before jumping, even if a ship is already
            -- in - gives grouped players time to buy the same ticket. The
            -- board-anyway cap still applies (never wedges a port).
            minDepartureNoticeSeconds = 30,
            -- Random patrols arrive one city out when the destination is hot;
            -- convergence responses still take the direct route.
            avoidHotArrival = true,
            collectorScanRadiusMeters = 175,
            collectorZSanityMeters = 10,
            -- P.6.5d: city-loop hangouts default to the nearest validated
            -- cantina exterior; Theed keeps its verified hand-placed spot.
            useCantinaHangouts = true,
            cantinaScanRadiusMeters = 400,
            -- Squads form up (spawn + full-wipe reform) at faction staging.
            staging = {
                rebel = { planet = "naboo", city = "moenia" },
                imperial = { planet = "tatooine", city = "bestine" },
            },
            diagnostics = {
                dumpTravelGraph = true,
                testStarportInteriorPaths = true,
                -- Exact point names from scripts/managers/planet/planet_manager.lua
                -- (note: "Theed Spaceport", and Moenia's starport is "Moenia").
                interiorPathPoints = {
                    { zone = "naboo", point = "Theed Spaceport" },
                    { zone = "naboo", point = "Moenia" },
                    { zone = "naboo", point = "Keren Starport" },
                    { zone = "naboo", point = "Kaadara Starport" },
                    { zone = "corellia", point = "Coronet Starport" },
                    { zone = "corellia", point = "Kor Vella Starport" },
                    { zone = "corellia", point = "Tyrena Starport" },
                    { zone = "tatooine", point = "Mos Eisley Starport" },
                    { zone = "tatooine", point = "Bestine Starport" },
                    { zone = "tatooine", point = "Mos Entha Starport" },
                    { zone = "rori", point = "Restuss Starport" },
                },
            },
        },
    },

    -- 1. LOCATIONS
    -- P.6.5a/P.6.5d: `starport` and `shuttlePoint` are the EXACT
    -- PlanetTravelPoint names from scripts/managers/planet/planet_manager.lua.
    -- Starports serve cross-planet travel; shuttlePoints serve intra-planet
    -- city departures/arrivals. The resolver supplies live positions and
    -- derives exterior cantina hangouts unless hangoutManual is true.
    -- NOTE: kaadara is EXCLUDED - its travel point z=-192 in the planet data
    -- (under-the-world quirk); revisit if we ever want it in the pool.
    -- NOTE: miners also spawn spread across this list, so miners now appear
    -- in the new cities (incl. restuss on rori) - wider gather coverage.
    shuttleports = {
        naboo = {
            { name = "moenia", spawn = {4963.0, -4892.0, 3.0}, hangout = {4807.0, -4700.0, 4.0}, starport = "Moenia", shuttlePoint = "Moenia Shuttleport" },
            { name = "theed", spawn = {-5410, 4325.0, 6.0}, hangout = {-4880.0, 4140.0, 6.0}, starport = "Theed Spaceport", shuttlePoint = "Theed Shuttle C", hangoutManual = true },
            { name = "keren", spawn = {1371.6, 2747.9, 13.0}, hangout = {1343.0, 2758.0, 13.0}, starport = "Keren Starport", shuttlePoint = "Keren Shuttleport" },
        },
        corellia = {
            { name = "coronet", spawn = {-328.0, -4600.0, 28.0}, hangout = {-155.0, -4722.0, 28.0}, starport = "Coronet Starport", shuttlePoint = "Coronet Shuttle B" },
            { name = "kor_vella", spawn = {-3157.3, 2876.2, 31.0}, hangout = {-3145.0, 2905.0, 31.0}, starport = "Kor Vella Starport", shuttlePoint = "Kor Vella Shuttleport" },
            { name = "tyrena", spawn = {-5003.1, -2228.4, 21.0}, hangout = {-4975.0, -2216.0, 21.0}, starport = "Tyrena Starport", shuttlePoint = "Tyrena Shuttle A" },
        },
        tatooine = {
            { name = "mos_eisley", spawn = {3416.0, -4645.0, 5.0}, hangout = {3467.0, -4890.0, 5.0}, starport = "Mos Eisley Starport", shuttlePoint = "Mos Eisley Shuttleport" },
            { name = "bestine", spawn = {-1361.2, -3600.0, 12.0}, hangout = {-1388.0, -3584.0, 12.0}, starport = "Bestine Starport", shuttlePoint = "Bestine Shuttleport" },
            { name = "mos_entha", spawn = {1266.1, 3065.1, 7.0}, hangout = {1241.0, 3048.0, 7.0}, starport = "Mos Entha Starport", shuttlePoint = "Mos Entha Shuttle A" },
        },
        rori = {
            -- Off-main destination: reachable ONLY via a naboo hop (fare
            -- matrix), so routes here exercise real multi-leg journeys.
            { name = "restuss", spawn = {5340.0, 5734.0, 80.0}, hangout = {5354.0, 5762.0, 80.0}, starport = "Restuss Starport", shuttlePoint = "Restuss Shuttleport" },
        },
    },

    -- 2. SPAWN RULES
    spawnGroups = {
        -- MINERS: Spread randomly across ALL defined shuttleports
        {
            type = "miner",
            totalCount = 10,
            templates = { "artisan" }, -- Randomly picks appearance
            behavior = "gather_resources",
            minerConfig = {
                resources = { "iron", "gas", "water", "copper" },
                surveyDurationMs = 4000,
                sampleDurationMs = 15000,
                minSearchRadius = 100,
                maxSearchRadius = 200,
                fallbackRadius = 100,
                logStateTransitions = false,
                yieldConfig = {
                    enabled = true,
                    minAmount = 5,
                    maxAmount = 25,
                    logYield = true,
                },
                summaryConfig = {
                    enabled = true,
                    intervalSeconds = 30,
                },
            }
        }
        -- (P.6.1) The legacy pvp_solo spawn group is retired; PvP squads are
        -- configured in pvpConfig above.
    }
}

-- DEBUG CHECK: Verifies the table exists before passing to C++
--print("LUA DEBUG: SimPlayerManagerConfig.spawnGroups type=", type(SimPlayerManagerConfig.spawnGroups))

-- return SimPlayerManagerConfig
