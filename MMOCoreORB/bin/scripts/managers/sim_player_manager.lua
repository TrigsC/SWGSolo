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
        planetDispatchBoardRadiusMeters = 10,  -- "reached the ticket collector" radius
        -- F.0.4.11: when enabled, interplanetary trips walk through the
        -- starport interior to the ticket collector and walk out of the
        -- destination hollow after boarding. Disabled keeps the existing
        -- dispatch and PvP travel choreography unchanged, including the
        -- 10-meter planetDispatchBoardRadiusMeters behavior.
        ticketCollectorTravel = {
            enabled = true,
            boardRadiusMeters = 8,
            approachAttempts = 3,
            approachTtlSeconds = 60,
            fallbackToBoardFromNear = false,
            -- Deterministic fixture: force the shared collector resolver to
            -- return none on every starport to exercise fallback/cancel paths.
            testForceNoCollector = false,
            -- Enclosed-hollow starports bake the ticket collector ~10m outside
            -- the starport's collision bounding box, so a strict point-in-AABB
            -- test fails to associate the collector with its own starport and
            -- the bot walks straight at the walled hollow. This horizontal slack
            -- (meters) on the containment test rescues that case; the next
            -- cell-bearing building is always 100m+ away so there is no risk of
            -- selecting an adjacent structure. Clamped 0..40 in C++.
            interiorContainmentMarginMeters = 15,
        },
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
                -- Raised 500000 -> 900000 (owner decision 2026-09-21). At
                -- 500000 the hunter families sat ABOVE their ceiling (meat
                -- 587107 / hide 531093 / bone 504621), so every family read as
                -- surplus, pressure was 0, the matchmaker issued no orders and
                -- no mission ever ran -- the roadmap 5c.1 deadlock. This is a
                -- stopgap: the real fix is an in-session consumer that drains
                -- supply, at which point these should come back down.
                desiredReserve = 900000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            master_weaponsmith_staples = {
                enabled = true,
                -- Raised 500000 -> 900000 (owner decision 2026-09-21). At
                -- 500000 the hunter families sat ABOVE their ceiling (meat
                -- 587107 / hide 531093 / bone 504621), so every family read as
                -- surplus, pressure was 0, the matchmaker issued no orders and
                -- no mission ever ran -- the roadmap 5c.1 deadlock. This is a
                -- stopgap: the real fix is an in-session consumer that drains
                -- supply, at which point these should come back down.
                desiredReserve = 900000,
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
                -- Raised 500000 -> 900000 (owner decision 2026-09-21). At
                -- 500000 the hunter families sat ABOVE their ceiling (meat
                -- 587107 / hide 531093 / bone 504621), so every family read as
                -- surplus, pressure was 0, the matchmaker issued no orders and
                -- no mission ever ran -- the roadmap 5c.1 deadlock. This is a
                -- stopgap: the real fix is an in-session consumer that drains
                -- supply, at which point these should come back down.
                desiredReserve = 900000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            chef_high_value_consumables = {
                enabled = true,
                -- Raised 500000 -> 900000 (owner decision 2026-09-21). At
                -- 500000 the hunter families sat ABOVE their ceiling (meat
                -- 587107 / hide 531093 / bone 504621), so every family read as
                -- surplus, pressure was 0, the matchmaker issued no orders and
                -- no mission ever ran -- the roadmap 5c.1 deadlock. This is a
                -- stopgap: the real fix is an in-session consumer that drains
                -- supply, at which point these should come back down.
                desiredReserve = 900000,
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

    -- P.8.7 interplanetary/ticket-collector travel diagnostics -> bin/log/traveldiag.log.
    -- Its OWN file so a wedged departure leg is greppable without zone spam.
    -- Every line is key=value: t=<ms> ev=<EVENT> oid=<bodyOid> ...
    -- Compiled in and dormant when false; no rebuild needed to flip.
    travelDiag = {
        -- Investigation complete (F_0.5.0 shipped); off in production. The
        -- instrumentation stays compiled in and free when off; flip true to
        -- re-capture a relocation stall (no rebuild needed).
        logging = false,
        -- Seconds between "still in this phase" heartbeats for a bot that is
        -- travelling. This is the line that catches a STALL, since a stuck leg
        -- emits no transitions at all.
        heartbeatSeconds = 10,
    },

    -- F_0.9.6-D1 sim-bot combat observability -> bin/log/simcombat.log.
    -- This is read-only instrumentation for the AI decision path. It is
    -- compiled in and free when false; flipping it needs no rebuild.
    -- Every line is key=value: t=<ms> run=<id> ev=<EVENT> oid=<oid> ...
    combatDiag = {
        logging = false,
        -- Include the parity harness kill target so the creature's own AI
        -- scheduling path is visible alongside the sim bot.
        traceSpawnedTargets = true,
        -- Sized for the real 400 ms behaviour cadence plus transition and
        -- heartbeat lines during a bounded diagnostic run.
        maxLinesPerAgent = 2000,
        -- The emitting lane runs at pveConfig.maintenanceIntervalSeconds;
        -- effective cadence is clamped up to that interval.
        heartbeatSeconds = 30,
    },

    -- P.8.7 destroy-mission BOARD diagnostics -> bin/log/missiondiag.log.
    -- Offer generation rejects with bare `continue`/`return false` and the
    -- abandon reason is then flattened to the generic "abandoned" that
    -- completeOrder() writes from the TRAVEL_HOME epilogue, so a board that
    -- produces zero offers looks identical to one that was never asked. These
    -- lines name the exact gate that rejected.
    -- Every line is key=value: t=<ms> ev=<EVENT> id=<identityId> ...
    -- Compiled in and dormant when false; no rebuild needed to flip.
    missionDiag = {
        -- Investigation complete (F_0.5.0 shipped): the rejecting gate was the
        -- empty-board all-or-nothing discard plus offer saturation, both fixed.
        -- Off in production; compiled in and free, flip true to re-capture.
        logging = false,
    },

    -- Pass-1 POB cell-entry movement diagnostic. Disabled by default.
    cellNavDiag = {
        -- Spawns the one-shot diagnostic artisan test bot. OFF in production; flip
        -- true only when actively diagnosing cell-nav movement.
        enabled = false,
        -- Master gate for ALL cell-nav diagnostic file output (bin/log/cellnav.log:
        -- ENGINE_*, STARPORT_WP_*, TICKET_COLLECTOR_RESOLVED, PVP_COLLECTOR_APPROACH,
        -- ENGINE_DIRECTION, ...). OFF in production -- the full instrumentation stays
        -- compiled in, dormant and free. Flip true to re-capture (no rebuild needed).
        logging = false,
        planet = "tatooine",
        -- Mos Eisley STARPORT test (building oid 1106368, template
        -- starport_tatooine.iff; interior = cell 4; outdoor landing pad ~3619,-4790).
        -- Spawn out front (outdoor), target the interior main hall (resolver finds
        -- the cell), then exit back out to the world (wilderness) to see where
        -- getEjectionPoint() sends the bot and whether the egress works here.
        spawnX = 3527, spawnY = -4803, spawnZ = 5,
        targetX = 3563, targetY = -4799,   -- starport interior main hall
        targetZ = 0,
        doorwayX = 3527, doorwayY = -4803,  -- reference (front)
        -- Exit to a flat, nearby, GROUND-LEVEL city point (the cantina front) so the
        -- round-trip completes cleanly. The old 5000,-5500 target is a distant mesa
        -- (z~155) that made the non-hybrid diag bot ramp up a long overland climb --
        -- an artifact of the target, not the egress (which now exits the front fine).
        exitX = 3626, exitY = -4791,
        logEverySimLoopTick = true,
    },

    -- P.9 structure traversal foundation. The production API and its
    -- diagnostics are compiled in but dormant until this master gate is
    -- explicitly enabled.
    structureTraversal = {
        -- P.9 gates SHIPPED ON as of v0.8.2 (owner decision 2026-09-01).
        -- Live-verified: the v0.8.1 run matched 23 PASS / 3 FAIL of the
        -- 26-scenario matrix with 61 production agents on ordered phases, and
        -- the F_0.7.5 run confirmed Theed hunters completing the full
        -- cantina -> med-center buff loop against real providers.
        --
        -- Deliberately still FALSE below, each for a measured reason:
        --   hollowEscalationDirectFallback - clips (crossesGeometry=1)
        --   useNavmeshHybrid               - measured harmful, 11 -> 9 PASS
        --   requireCompletePath            - instrument; costs the
        --                                    cantina -> corellia hospital route
        --   hollowScan                     - refuted as an exit finder, 72/72
        --   zeroClip.*                     - the probe costs ~19ms p95 per path
        --                                    request and writes per-path files
        --   logging / structureTraversalTest - diagnostics only
        enabled = true,
        logging = false,
        hollowEscalationEnabled = true,
        hollowEscalationAttemptCap = 1,
        hollowEscalationPreferTravelPoint = true,
        -- Option C is known to clip (crossesGeometry=1, hitAt=0.069) and is
        -- superseded by D7.
        hollowEscalationDirectFallback = false,
        resumeSettleMs = 2000,
        resumeAttemptCap = 3,
        egressAttemptCap = 2,
        teleportAnomalyMeters = 25,
        zSanityMeters = 5,
        -- Horizontal slack for "still inside the owning building's enclosed
        -- hollow" (starport landing pad is cell 0 but walled in).
        hollowContainmentMarginMeters = 15,
        zeroClip = {
            -- Committed defaults are OFF. An observation run flips these as a
            -- deployment-local override and restores them afterwards; the probe
            -- measured p95 ~19ms per path request plus per-path file logging,
            -- which is not something to ship enabled.
            enabled = false,
            logging = false,
            enforce = false,
            -- D7 Phase 2. Conclusively-obstructed paths refused before the
            -- bot gives up and walks one anyway. Bounded so a pathfinder that
            -- deterministically returns the same clipping route cannot freeze
            -- a bot; `zeroClip.capExhausted` on the dashboard is the residual
            -- clip rate once enforcement is on.
            rejectionCap = 2,
            -- The appearance ray tests the straight chord between two path
            -- nodes, which dives through the solid mass under a staircase or
            -- bridge deck -- geometry a bot is meant to walk ON. MEASURED
            -- 2026-08-28: 53% of raw mesh hits were this false positive.
            -- Confirm a flagged chord against the navmesh (the authority on
            -- what an agent can stand on) before believing the ray.
            -- `zeroClip.walkableReclassified` counts the overrules.
            walkableConfirm = false,
            walkableToleranceRatio = 1.25,
            -- Objects taken to the narrow phase per PATH (the world query is
            -- hoisted out of the segment loop, so this is no longer per-segment).
            maxCandidates = 256,
            maxSegmentMeters = 512,
            -- Segments probed per path. The first observe run capped at 16 and
            -- truncated 88 of 89 long routes (median 58 nodes), so the block
            -- rate could only be measured over 59% of traffic.
            maxProbedSegments = 128,
            -- getInRangeObjects matches on object ORIGIN; pad the query so a
            -- large structure centred off to one side is still considered.
            broadPhasePadMeters = 192,
            exitSetEnabled = true,
            egressCandidateAttemptCap = 2,
            egressTotalAttemptCeiling = 8,
            -- A CellPortal exit set contains doors on EVERY level of the POB.
            -- Measured 2026-08-23: candidate heights of -19.997, 0.003, 6.04,
            -- 10.335 and 77.6163 in one run, and a 2D-ordered set put the 77m
            -- door first -> 240 zSanityViolations. A door that far off the
            -- bot's own floor is not an exit it can walk to, so it is not a
            -- candidate. Roughly one to two storeys. 0 disables the filter.
            -- DERIVED, not guessed (2026-08-25). Measured starport door
            -- heights above a bot on the pad: +5.96, +10.25 (a real ground
            -- door at the far end of the band) and +77.53 (roof). The previous
            -- hand-picked 10 rejected the +10.25 door by 0.25m and silently
            -- hid half the door set. 20 keeps both real doors and still
            -- rejects the roof by a wide margin.
            exitCandidateMaxVerticalMeters = 20,
        },
        -- D8 radial hollow scan. REFUTED as an exit-finder on 2026-08-24
        -- (blocked=72/72 on starport_tatooine) and retained for diagnostics
        -- only; see docs/1-plans/F_0.8.0-D8_hollow-radial-exit.proposal.md.
        -- MEASURED HARMFUL for traversal bots, 2026-08-25: flipping this true
        -- took the matrix 11 PASS -> 9 PASS and turned 11 exit_not_outdoors into
        -- 13 controller_path_failed. Hybrid movement already carries three
        -- hand-added exclusions (interiorApproachLeg, cellEgressActive,
        -- ticketTravelPhase) and structure-traversal egress needs a fourth --
        -- which is the symptom, not the fix. Leave false.
        useNavmeshHybrid = false,
        -- Diagnostic gate. Turns a silently-accepted partial path into an
        -- explicit rejection + bounded retry. MEASURED 2026-08-25: works (15
        -- rejections, med-centre ingress retried and still PASSED) but costs
        -- cantina_to_corellia_hospital, whose route legitimately ends short.
        -- Keep OFF as a default; it is a measuring instrument, not a fix.
        requireCompletePath = false,
        completePathToleranceMeters = 10,
        -- D2b far-side egress. When a found path ends back inside the
        -- owning building's hollow while the DESTINATION is outside it, the
        -- route made no progress (it left by the nearest portal). Reject it and
        -- retarget to the nearest reachable non-hollow exit CELL, which the
        -- pathfinder routes through the interior correctly.
        -- LIVE-VERIFIED 2026-08-27 (run 20260827-200923-d2b-farside): matrix
        -- 11 PASS/15 FAIL -> 22 PASS/4 FAIL of 26, 11 improvements, zero
        -- regressions, assertions non-vacuous. Default OFF pending release.
        farSideEgress = true,
        hollowScan = {
            enabled = false,
            rays = 72,
            rayMarginMeters = 40,
            minOpeningDeg = 5,
        },
        hollowDoorEgress = {
            observe = true,
            walk = true,
            useCellPortals = true,
        },
    },

    -- Phase 3 scenario harness.  This remains disabled by default.  Explicit
    -- cell OIDs are used where repository/live traces provide them (Mos Eisley
    -- cantina/starport and the medical-center cells); Theed Spaceport and the
    -- Coronet cantina use concrete world points, with the latter also pinned
    -- by its known building OID for the resolver diagnostics.
    structureTraversalTest = {
        enabled = false,
        planet = "tatooine",
        botCount = 1,
        -- Must be a REGISTERED creature template name; spawnCreature() hashes
        -- this string and returns null for an unknown one, which fails the
        -- scenario with attacker_spawn_failed. Verified registered in
        -- scripts/mobile/naboo/hermit_spider.lua (level 7, pvpBitmask
        -- ATTACKABLE). Creature templates are global, not planet-scoped.
        attackerTemplate = "hermit_spider",
        dwellScaling = 1.0,
        scenarios = {
            -- A. Core ingress/egress
            { name="cantina_enter_exit", planet="tatooine", bots=1,
              spawn={{x=3467,y=-4890,z=5}},
              budgets={enterMs=90000,exitMs=90000,totalMs=300000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="dwell",ms=10000},
                {op="exit",dest={x=3500,y=-4700,z=5}} } },

            { name="naboo_hospital_enter_exit", planet="naboo", bots=1,
              spawn={{x=-5410,y=4325,z=6}},
              budgets={enterMs=120000,exitMs=120000,totalMs=360000},
              steps={
                -- Theed medical-center cell/local point from
                -- custom_scripts/smart_doctor_config.lua.
                {op="enter",target={x=-18.46,y=3.39,z=0.26},cellOid=1697364},
                {op="dwell",ms=10000},
                {op="exit",dest={x=-5480,y=4380,z=6}} } },

            { name="mos_eisley_starport_front", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=360000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            { name="mos_eisley_starport_deep_foyer4", planet="tatooine", bots=1,
              spawn={{x=3563,y=-4799,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=360000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            { name="cantina_immediate_exit", planet="tatooine", bots=1,
              spawn={{x=3467,y=-4890,z=5}},
              budgets={enterMs=90000,exitMs=90000,totalMs=240000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}} } },

            { name="cantina_long_dwell", planet="tatooine", bots=1,
              spawn={{x=3467,y=-4890,z=5}},
              budgets={enterMs=90000,exitMs=120000,totalMs=900000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="dwell",ms=600000},
                {op="exit",dest={x=3500,y=-4700,z=5}} } },

            { name="theed_starport_hangar", planet="naboo", bots=1,
              -- The only scenario that exercises the world-point resolver.
              -- The target is the Theed Spaceport PlanetTravelPoint from
              -- planet_manager.lua:287 -- an OUTDOOR anchor, not a
              -- destination: resolveStarportInteriorWaypoint() uses it to
              -- select the building and then returns an interior path-graph
              -- node. The building/cell OIDs are resolved at runtime.
              spawn={{x=-5005,y=4072,z=6}},
              budgets={enterMs=90000,exitMs=90000,totalMs=300000},
              steps={
                {op="enter",target={x=-4858.834,y=4164.0679,z=5.9483199}},
                {op="exit",dest={x=-5005,y=4072,z=6}} } },

            { name="starport_upper_floor", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=360000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106374},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            -- B. Cross-building. Explicit hospital cell/local points exercise
            -- the asymmetric graph; world-only entries use the C++ resolver's
            -- <=3m cell/world round-trip guard.
            { name="hospital_to_cantina", planet="tatooine", bots=1,
              spawn={{x=3467,y=-4890,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},cellOid=9655496},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}} } },

            { name="cantina_to_corellia_hospital", planet="corellia", bots=1,
              spawn={{x=-328,y=-4600,z=28}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                -- Coronet cantina is pinned by building OID and resolves to the
                -- building's first interior cell, so the target here is
                -- CELL-LOCAL (like the Mos Eisley cantina steps), never the
                -- world point it used to carry. The hospital cell/local point
                -- below is from smart_doctor_config.lua.
                {op="enter",target={x=0,y=0,z=0},buildingOid=8105493},
                {op="enter",target={x=-18.54,y=3.33,z=0.26},cellOid=1855535},
                {op="exit",dest={x=-400,y=-4520,z=28}} } },

            { name="cell_to_enclosed_hollow", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=480000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="moveTo",dest={x=3628.79,y=-4790.9,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            -- Multi-hop TRANSIT shape (owner, 2026-08-27). Planets are not
            -- all directly connected, so a bot routinely lands somewhere it is
            -- only passing through: it arrives by shuttle INTO THE HOLLOW,
            -- walks into the starport cell to the ticket TERMINAL (where a
            -- player buys a ticket), then walks BACK OUT INTO THE HOLLOW to the
            -- ticket COLLECTOR, which is what actually initiates travel.
            --
            -- The bot ENDS ON THE PAD, and that is CORRECT. This scenario
            -- therefore finishes with moveTo, never exit: an exit step asserts
            -- !inCell && !inHollow and would fail a correctly-transiting bot.
            -- Its whole purpose is to catch "always leave the hollow" logic,
            -- which every other scenario here would happily pass because they
            -- all specify an outdoor destination away from the building.
            --
            -- Name must NOT contain "enclosed_hollow": that substring makes the
            -- runner call setHarnessEgressSuppressed() on moveTo steps, which
            -- would skip the very cell->hollow egress this is testing.
            --
            -- VERIFIED LIVE 2026-08-27 (run 20260827-090754-d8-transit): PASS,
            -- 84.1s total -- enter 54.0s, dwell 5.0s, moveTo 24.5s. Log shows a
            -- real 47-node route from mid-pad, entered_structure at +10s,
            -- target_cell_arrived at +54s, then a genuine cell->hollow return.
            { name="starport_transit_terminal_to_collector", planet="tatooine", bots=1,
              spawn={{x=3575,y=-4813,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106383},
                {op="dwell",ms=5000},
                {op="moveTo",dest={x=3575,y=-4813,z=5}} } },

            -- C. Combat-wins. Interrupts are attached to movement steps, so
            -- the runner fires them after the named phase is observed.
            { name="combat_approach_door", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372,
                 interrupt={phase="ApproachDoor",afterMs=500,durationMs=8000}},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            { name="combat_interior_route", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372,
                 interrupt={phase="InteriorRoute",afterMs=500,durationMs=8000}},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            { name="combat_egress", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="exit",dest={x=3527,y=-4803,z=5},
                 interrupt={phase="Egress",afterMs=500,durationMs=8000}} } },

            { name="combat_drag_different_cell", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="exit",dest={x=3527,y=-4803,z=5},
                 interrupt={phase="Egress",afterMs=500,durationMs=8000,
                   displace={x=0,y=0,z=0,cellOid=1106374}}} } },

            { name="combat_ends_outdoors", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="exit",dest={x=3527,y=-4803,z=5},
                 interrupt={phase="Egress",afterMs=500,durationMs=8000,
                   displace={x=3527,y=-4803,z=5}}} } },

            { name="combat_reentry_cross_building", planet="tatooine", bots=1,
              spawn={{x=3467,y=-4890,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=600000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372,
                 interrupt={phase="Reentry",afterMs=500,durationMs=8000}},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            { name="attacker_dies_instantly", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=120000,exitMs=120000,totalMs=420000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372,
                 interrupt={phase="ApproachDoor",afterMs=100,durationMs=1}},
                {op="exit",dest={x=3527,y=-4803,z=5}} } },

            -- D. Failure and hygiene.
            -- Bounded failure. The target is an ORDINARY resolvable starport
            -- cell and the traversal API is driven for real; the harness forces
            -- the path RESULT to fail (setHarnessForcePathFailure), which is
            -- how a genuinely unroutable target surfaces. Map data cannot be
            -- used to produce this: an off-mesh cell point falls back to the
            -- nearest floor triangle, an out-of-bounds world point is rejected
            -- before the path task is scheduled, and a cross-planet cell is
            -- zone-checked by neither the resolver nor the pathfinder (the bot
            -- would just time out chasing it). Assertion: bounded onPathFailed,
            -- traversal state cleared, bot recovers and completes the moveTo.
            { name="unreachable_target_bounded_failure", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=30000,exitMs=30000,totalMs=120000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="moveTo",dest={x=3527,y=-4803,z=5}} } },

            { name="external_preemption", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=90000,exitMs=90000,totalMs=300000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="moveTo",dest={x=3500,y=-4700,z=5}} } },

            { name="prepare_for_relocation", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=90000,exitMs=90000,totalMs=300000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="moveTo",dest={x=3527,y=-4803,z=5}} } },

            -- Death must land MID-TRAVERSAL: the enter step is still in flight
            -- (the runner kills the body while the leg is active), which is the
            -- state that must leave no dangling task and no wedge.
            { name="death_or_incapacity_recovery", planet="tatooine", bots=1,
              spawn={{x=3527,y=-4803,z=5}},
              budgets={enterMs=90000,exitMs=90000,totalMs=300000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                {op="moveTo",dest={x=3527,y=-4803,z=5}} } },

            -- E. Ten-cycle stress scenario.
            { name="ten_sequential_cycles", planet="tatooine", bots=1,
              spawn={{x=3467,y=-4890,z=5}},
              budgets={enterMs=900000,exitMs=900000,totalMs=3600000},
              steps={
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}},
                {op="enter",target={x=0,y=0,z=0},buildingOid=1082874,cellOid=1082877},
                {op="exit",dest={x=3500,y=-4700,z=5}} } },

            -- Both bots share ONE building (the multi-door starport) — that is
            -- what makes it an interference test. They approach from opposite
            -- sides and leave toward opposite outdoor destinations.
            { name="two_bots_opposite_directions", planet="tatooine", bots=2,
              spawn={{x=3527,y=-4803,z=5},{x=3563,y=-4799,z=5}},
              budgets={enterMs=180000,exitMs=180000,totalMs=600000},
              steps={
                {
                  {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                  {op="exit",dest={x=3563,y=-4799,z=5}} },
                {
                  {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106374},
                  {op="exit",dest={x=3527,y=-4803,z=5}} } } },

            -- Bot A dwells INSIDE the same building bot B enters and exits.
            { name="bot_a_dwell_bot_b_traverse", planet="tatooine", bots=2,
              spawn={{x=3527,y=-4803,z=5},{x=3563,y=-4799,z=5}},
              budgets={enterMs=180000,exitMs=180000,totalMs=600000},
              steps={
                {
                  {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106374},
                  {op="dwell",ms=30000},
                  {op="exit",dest={x=3527,y=-4803,z=5}} },
                {
                  {op="enter",target={x=0,y=0,z=0},buildingOid=1106368,cellOid=1106372},
                  {op="exit",dest={x=3563,y=-4799,z=5}} } } },
        },
    },

    -- P.10a live progression parity harness. It is completely default-off;
    -- enable only on a disposable verification profile. The retained probe
    -- is written in phase A and adopted by the next process boot as phase B.
    playerBotParityTest = {
        enabled = false,
        planet = "tatooine",
        spawn = {x=3467, y=-4890, z=5},
        identityCount = 2,
        killTargetTemplate = "dwarf_nuna",
        startupDelaySeconds = 60,
        scenarios = {
            {name="store_boot_invariant", budgetMs=300000, steps={
                {op="assertStore", records=-1, orphanRecords=0, rosterWithoutRecord=0}}},
            {name="harness_identity_minted_with_record", budgetMs=300000, steps={
                {op="assertXp", identityIndex=0, xpType="harness_zero", expect="0"},
                {op="assertXp", identityIndex=1, xpType="harness_zero", expect="0"},
                {op="assertCredits", identityIndex=0, bank=true, expect="0"},
                {op="assertCredits", identityIndex=1, bank=true, expect="0"}}},
            {name="training_scale_2000_budget", budgetMs=300000, steps={
                {op="assertTrainingScale", amount=2000}}},
            {name="grant_xp_accepted", budgetMs=300000, steps={
                {op="grantXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", amount=250},
                {op="assertXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", expect="250"},
                {op="assertCounterDelta", counter="awards.accepted", expect="1"},
                {op="assertSource", identityIndex=0, expect="harness"}}},
            {name="grant_xp_unknown_identity_rejected", budgetMs=300000, steps={
                {op="awardUnknownIdentity", identityId=999999999, xpType="harness_unknown", amount=1},
                {op="assertCounterDelta", counter="rejectedNoRecord", expect="1"},
                {op="assertRecords", delta=0}}},
            {name="award_to_non_roster_body_rejected", budgetMs=300000, steps={
                {op="awardNonRosterBody", xpType="harness_leaked_flag", amount=1},
                {op="assertCounterDelta", counter="rejectedNoIdentity", expect="1"},
                {op="assertRecords", delta=0}}},
            {name="credits_grant_and_spend", budgetMs=300000, steps={
                {op="grantCredits", identityIndex=0, bank=true, amount=1000},
                {op="spendCredits", identityIndex=0, bank=true, amount=400},
                {op="assertCredits", identityIndex=0, bank=true, expect="600"},
                {op="spendCredits", identityIndex=0, bank=true, amount=1000, expectReject=true},
                {op="assertCounterDelta", counter="rejectedInsufficient", expect="1"},
                {op="assertCredits", identityIndex=0, bank=true, expect="600"}}},
            {name="record_skill", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="recordSkill", identityIndex=0, xpType="combat_marksman_novice", freeGrant=true},
                {op="recordSkill", identityIndex=0, xpType="combat_marksman_novice", expectReject=true},
                {op="assertSkill", identityIndex=0, xpType="combat_marksman_novice"}}},
            {name="training_gate_off_refuses", budgetMs=300000,
                -- Asserts the gate-OFF refusal branch and arms nothing itself,
                -- so it requires the shipped default. Arming trainingEnabled
                -- in configuration inverts what this scenario tests.
                requiresGatesOff={"trainingEnabled"}, steps={
                {op="trainSkill", identityIndex=1, skillName="combat_brawler_novice", freeGrant=true, expectReject=true},
                {op="assertTrainingCounter", counter="training.refusedGate", expect="1"}}},
            {name="training_derived_spend_and_tier", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="trainSkill", identityIndex=1, skillName="combat_brawler_novice", freeGrant=true},
                {op="assertSkill", identityIndex=1, skillName="combat_brawler_novice"},
                {op="assertSkillPoints", identityIndex=1, expect="derived"},
                {op="assertAvailableXp", identityIndex=1, xpType="combat_general", expect="0"},
                {op="assertTier", identityIndex=1, expect="1"}}},
            {name="training_xp_cap", budgetMs=300000, steps={
                {op="grantXp", identityIndex=1, xpType="harness_cap", amount=2147483647},
                {op="assertAvailableXp", identityIndex=1, xpType="harness_cap", expect="cap"}}},
            {name="body_destroy_respawn_keeps_scalars", budgetMs=300000, steps={
                {op="destroyBody", identityIndex=0},
                {op="respawnBody", identityIndex=0},
                {op="assertXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", expect="250"},
                {op="assertCredits", identityIndex=0, bank=true, expect="600"}}},
            {name="body_respawn_retains_skill_and_mod", budgetMs=300000, steps={
                {op="destroyBody", identityIndex=0},
                {op="respawnBody", identityIndex=0},
                {op="assertSkill", identityIndex=0, skillName="combat_marksman_novice"},
                {op="assertSkillMod", identityIndex=0, mod="rifle_accuracy", expect="atLeastTemplate"}}},
            {name="body_mid_life_reapplication", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="trainSkill", identityIndex=0, skillName="combat_marksman_rifle_01", freeGrant=true},
                {op="assertSkill", identityIndex=0, skillName="combat_marksman_rifle_01"},
                {op="assertSkillMod", identityIndex=0, mod="rifle_accuracy", expect="atLeastTemplate"}}},
            {name="body_mod_never_below_template_baseline", budgetMs=300000, steps={
                {op="assertSkillMod", identityIndex=0, mod="rifle_accuracy", expect="atLeastTemplate"}}},
            {name="body_mid_life_training_updates_live_body", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="trainSkill", identityIndex=0, skillName="combat_marksman_rifle_02", freeGrant=true},
                {op="assertSkill", identityIndex=0, skillName="combat_marksman_rifle_02"},
                {op="assertSkillMod", identityIndex=0, mod="rifle_accuracy", expect="atLeastTemplate"}}},
            {name="training_scale_2000_live_roster", budgetMs=300000, steps={
                {op="seedSyntheticRoster", amount=2000},
                {op="assertTrainingScale", amount=2000}}},
            {name="unarmed_novice_kills_nuna", budgetMs=600000,
                -- Needs the kill-XP capability gate: awaitKillTargetDeath polls
                -- for exact XP rows, so with awardKillXp off it can only ever
                -- time out, which reads as broken combat rather than as the
                -- misconfiguration it is.
                requiresGates={"awardKillXp"}, steps={
                {op="setNoviceGate", expect="true"},
                {op="mintIdentity", profession="brawler", trainingPlan="brawler", identityRef="noviceBot"},
                {op="assertPlanOrder", identityRef="noviceBot", skillName="combat_brawler_novice"},
                {op="assertSkill", identityRef="noviceBot", skillName="combat_brawler_novice"},
                {op="respawnBody", identityRef="noviceBot"},
                {op="spawnKillTarget", identityRef="noviceBot"},
                -- XP TYPE, not the skill-tree name. The Teras Kasi tree is
                -- combat_unarmed_*, but ThreatMap keys damage off the equipped
                -- weapon's getXpType(), and creature_default_weapon.lua:62
                -- declares combat_meleespecialize_unarmed. Asserting the tree
                -- name here would wait on a row that never appears.
                {op="awaitKillTargetDeath", identityRef="noviceBot", xpType="combat_meleespecialize_unarmed", budgetMs=180000},
                {op="assertXp", identityRef="noviceBot", xpType="combat_meleespecialize_unarmed", expect="killTargetDirect"},
                -- Delete this scenario's progression row FIRST so no orphan is
                -- ever created. A global runReaper here would rescan every
                -- record - including the 2000-identity synthetic roster
                -- seeded by training_scale_2000_live_roster - and starve
                -- the maintenance lane the harness itself runs on.
                {op="deleteProgressionRow", identityRef="noviceBot"},
                {op="deleteIdentity", identityRef="noviceBot"},
                {op="setNoviceGate", expect="false"}}},
            {name="novice_grant_repair_across_restart", budgetMs=300000, steps={
                {op="setNoviceGate", expect="true"},
                {op="mintIdentity", profession="brawler", trainingPlan="brawler", identityRef="repairBot"},
                {op="assertSkill", identityRef="repairBot", skillName="combat_brawler_novice"},
                {op="flushNow", force=true},
                {op="reloadStore"},
                {op="assertSkill", identityRef="repairBot", skillName="combat_brawler_novice"},
                -- Delete this scenario's progression row FIRST so no orphan is
                -- ever created. A global runReaper here would rescan every
                -- record - including the 2000-identity synthetic roster
                -- seeded by training_scale_2000_live_roster - and starve
                -- the maintenance lane the harness itself runs on.
                {op="deleteProgressionRow", identityRef="repairBot"},
                {op="deleteIdentity", identityRef="repairBot"},
                {op="setNoviceGate", expect="false"}}},
            {name="retirement_completeness_resumability", budgetMs=300000, steps={
                {op="mintIdentity", identityRef="retireBot"},
                {op="retireIdentity", identityRef="retireBot"},
                {op="assertCounterDelta", counter="retirements.completed", expect="1"},
                -- DELTA, not absolute: orphanRecords is a global gauge and
                -- deleteIdentity (scenarios 18/19) deliberately leaves rows for
                -- the gated reaper, so an absolute 0 asserts something this
                -- scenario does not control. What retirement must guarantee is
                -- that IT adds no orphan of its own.
                {op="assertCounterDelta", counter="orphanRecords", expect="0"}}},
            {name="retirement_dirty_flush_safety", budgetMs=300000, steps={
                {op="injectFault", flushDelayMs=1000},
                {op="grantXp", identityIndex=0, xpType="harness_retire_race", amount=10},
                {op="flushNow", force=true},
                {op="assertPersisted", identityIndex=0, xpType="harness_retire_race", expect="10"},
                {op="assertCounterDelta", counter="orphanRecords", expect="0"}}},
            {name="max_hunters_contention_and_displacement", budgetMs=300000, steps={
                {op="assertBodyMaxBound"}}},
            {name="multi_replacement_convergence", budgetMs=300000, steps={
                {op="assertBodyMaxBound"}}},
            {name="effective_xp_cap_rises_after_training", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                -- Assert the CAP directly, not availableXp. available is
                -- earned-minus-spend, so once a box of this xpType is trained it
                -- settles at cap-minus-cost and can never equal the cap - the
                -- earlier form was unsatisfiable by construction. The per-type
                -- effective cap was previously unobservable, which is why this
                -- needed the assertXpCap op.
                {op="assertXpCap", identityIndex=1, xpType="combat_meleespecialize_unarmed", expect="default"},
                {op="trainSkill", identityIndex=1, skillName="combat_brawler_unarmed_01", freeGrant=true},
                {op="assertXpCap", identityIndex=1, xpType="combat_meleespecialize_unarmed", expect="raised"}}},
            {name="derived_tier_raises_level_cap_and_species_eligibility", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="trainSkill", identityIndex=1, skillName="combat_brawler_unarmed_02", freeGrant=true},
                {op="assertTier", identityIndex=1, expect="1"}}},
            {name="tier_parity", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="trainSkill", identityIndex=1, skillName="combat_brawler_unarmed_03", freeGrant=true},
                {op="assertTier", identityIndex=1, expect="1"}}},
            {name="threshold_correctness_lifetime_earned", budgetMs=300000, steps={
                {op="assertTrainingScale", amount=2000}}},
            {name="chained_affordability_budgeted", budgetMs=300000, steps={
                {op="assertTrainingScale", amount=2000}}},
            {name="retirement_memory_cleanup", budgetMs=300000, steps={
                {op="mintIdentity", identityRef="memBot"},
                {op="retireIdentity", identityRef="memBot"},
                -- +1 record at mint, -1 at retirement: a net zero delta proves
                -- progressionRecords was dropped, which is the in-memory half
                -- the SQL deletes cannot show.
                {op="assertRecords", delta=0},
                {op="assertCounterDelta", counter="orphanRecords", expect="0"}}},
            {name="dirty_suppression_race", budgetMs=300000, steps={
                {op="grantXp", identityIndex=0, xpType="harness_dirty_race", amount=1},
                {op="flushNow", force=true},
                {op="assertCounterDelta", counter="orphanRecords", expect="0"}}},
            {name="tier_persistence_and_self_heal", budgetMs=300000, steps={
                {op="flushNow", force=true},
                {op="reloadStore"},
                {op="assertTier", identityIndex=1, expect="1"}}},
            {name="plan_reassignment_refuses_unskilled_body", budgetMs=300000, steps={
                {op="mintIdentity", identityRef="reassignedBot"},
                {op="reassignPlan", identityRef="reassignedBot", trainingPlan="brawler"},
                {op="assertBodyLive", identityRef="reassignedBot", expect="0"},
                -- Delete this scenario's progression row FIRST so no orphan is
                -- ever created. A global runReaper here would rescan every
                -- record - including the 2000-identity synthetic roster
                -- seeded by training_scale_2000_live_roster - and starve
                -- the maintenance lane the harness itself runs on.
                {op="deleteProgressionRow", identityRef="reassignedBot"},
                {op="deleteIdentity", identityRef="reassignedBot"}}},
            {name="invalid_weapon_type_is_visible", budgetMs=300000, steps={
                {op="assertPlanLoadError", trainingPlan="invalid_weapon_probe", expect="invalid_weapon_type"}}},
            {name="novice_box_precondition_refuses_body", budgetMs=300000, steps={
                {op="setNoviceGate", expect="true"},
                -- governed=true: this scenario tests the governor's plan-ready
                -- refusal, so its identity must stay under governor management.
                {op="mintIdentity", profession="brawler", trainingPlan="brawler", identityRef="notReadyBot", governed=true},
                {op="deleteProgressionRow", identityRef="notReadyBot"},
                {op="assertBodyLive", identityRef="notReadyBot", expect="0"},
                -- Delete this scenario's progression row FIRST so no orphan is
                -- ever created. A global runReaper here would rescan every
                -- record - including the 2000-identity synthetic roster
                -- seeded by training_scale_2000_live_roster - and starve
                -- the maintenance lane the harness itself runs on.
                {op="deleteProgressionRow", identityRef="notReadyBot"},
                {op="deleteIdentity", identityRef="notReadyBot"},
                {op="setNoviceGate", expect="false"}}},
            {name="flush_and_reload_roundtrip", budgetMs=300000, steps={
                {op="flushNow", force=true},
                {op="reloadStore"},
                {op="assertXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", expect="250"},
                {op="assertCredits", identityIndex=0, bank=true, expect="600"}}},
            {name="natural_flush_cadence", budgetMs=300000, steps={
                {op="grantXp", identityIndex=0, xpType="harness_cadence", amount=10},
                {op="waitForFlush", budgetMs=180000},
                {op="reloadStore"},
                {op="assertXp", identityIndex=0, xpType="harness_cadence", expect="10"}}},
            {name="retained_record_survives_restart", budgetMs=300000, steps={
                {op="grantXp", identityIndex=1, xpType="harness_restart", amount=1234, restartPhase="A"},
                {op="grantCredits", identityIndex=1, bank=true, amount=777, restartPhase="A"},
                {op="setLootGate", identityIndex=1, lootGate=true, restartPhase="A"},
                {op="spawnKillTarget", identityIndex=1, restartPhase="A"},
                {op="awaitKillTargetDeath", identityIndex=1, keepCorpse=true, restartPhase="A"},
                {op="seedCorpseLoot", identityIndex=1, lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", restartPhase="A"},
                {op="lootCorpse", identityIndex=1, restartPhase="A"},
                {op="assertVaultItems", identityIndex=1, itemCount=1, restartPhase="A"},
                {op="assertItemStats", identityIndex=1, restartPhase="A"},
                {op="writeRestartProbe", identityIndex=1, restartPhase="A"},
                {op="flushNow", force=true, restartPhase="A"},
                {op="assertXp", identityIndex=0, xpType="harness_restart", expect="1234", restartPhase="B"},
                {op="assertCredits", identityIndex=0, bank=true, expect="777", restartPhase="B"},
                {op="assertVaultItems", identityIndex=0, itemCount=1, restartPhase="B"},
                {op="assertItemRow", identityIndex=0, itemState="held", itemCount=1, restartPhase="B"},
                {op="assertItemStats", identityIndex=0, restartPhase="B"},
                {op="deleteIdentity", identityIndex=0, restartPhase="B"},
                {op="runReaper", force=true, restartPhase="B"}}},
            {name="orphan_counted_not_reaped_gate_off", budgetMs=300000, steps={
                -- Establish a zero-orphan baseline first. This pair asserts an
                -- EXACT reap delta of 1, which only holds if exactly one orphan
                -- exists; any identity churn earlier in the matrix (novice
                -- minting creates a real production brawler once the gate is
                -- toggled) would otherwise inflate it. Scenarios must not
                -- inherit global state they did not establish.
                {op="runReaper", force=true},
                {op="injectOrphan"},
                -- assertCounterVALUE, not Delta. Counter baselines are captured
                -- before a scenario's FIRST step, so the forced reap above does
                -- not rebase them: with N inherited orphans the delta would read
                -- 1-N even though the absolute value is correctly 1. The forced
                -- reap guarantees a zero baseline, which is exactly what makes
                -- the absolute assertion valid here.
                {op="assertCounterValue", counter="orphanRecords", expect="1"},
                {op="runReaper", force=false},
                {op="assertCounterValue", counter="orphanRecords", expect="1"}}},
            -- The injected orphan is inherited from the previous scenario, so
            -- "back to baseline" is an ABSOLUTE zero here, not a zero delta
            -- (the delta across this scenario is -1).
            {name="orphan_reaped_gate_on", budgetMs=300000, steps={
                {op="runReaper", force=true},
                {op="assertCounterDelta", counter="orphansReaped", expect="1"},
                {op="assertCounterValue", counter="orphanRecords", expect="0"}}},
            {name="gate_off_rejects_awards", budgetMs=300000, steps={
                {op="setGate", expect="false"},
                {op="grantXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", amount=1, expectReject=true},
                {op="assertCounterDelta", counter="rejectedDisabled", expect="1"},
                {op="assertXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", expect="250"}}},
            {name="harness_cleanup_reaps", budgetMs=300000, steps={
                -- identityRef, not identityIndex: EVERY mint appends to the
                -- positional list, so any scenario added earlier shifts these
                -- indices. Refs bind to the identity this scenario created.
                {op="mintIdentity", identityRef="cleanupBot"},
                {op="deleteIdentity", identityRef="cleanupBot"},
                {op="assertCounterDelta", counter="orphanRecords", expect="1"},
                {op="runReaper", force=true},
                {op="assertStore", records=-1, orphanRecords=0, rosterWithoutRecord=0}}},
            {name="award_during_flush_persists", budgetMs=300000, steps={
                {op="injectFault", flushDelayMs=3000},
                {op="grantXp", identityIndex=0, xpType="harness_flush_rifle", amount=100},
                {op="flushNow", force=true, async=true, requestRef="flush14"},
                {op="waitForRequest", requestRef="flush14", phase="started", budgetMs=30000},
                {op="grantXp", identityIndex=0, xpType="harness_flush_rifle", amount=50},
                {op="waitForRequest", requestRef="flush14", phase="completed", budgetMs=30000},
                {op="assertPersisted", identityIndex=0, xpType="harness_flush_rifle", expect="100"},
                {op="assertDirty", identityIndex=0, expect="true"},
                {op="flushNow", force=true},
                {op="assertPersisted", identityIndex=0, xpType="harness_flush_rifle", expect="150"},
                {op="reloadStore"},
                {op="assertXp", identityIndex=0, xpType="harness_flush_rifle", expect="150"}}},
            {name="flush_failure_merge_back_and_recover", budgetMs=300000, steps={
                {op="injectFault", failNextFlush=true},
                {op="grantXp", identityIndex=0, xpType="harness_flush_rifle", amount=60},
                {op="flushNow", force=true},
                {op="assertCounterDelta", counter="flushFailures", expect="1"},
                -- Deliberately NOT asserting dbAvailable==false here. That flag is a
                -- self-healing transient: the re-probe restores it within ~5s by
                -- design, while the harness observes the flush request through a
                -- poll that can lag a full maintenance interval, so the false state
                -- is not reliably observable. Recovery is proven durably below by
                -- flushFailures+1, the record staying dirty, dbAvailable returning
                -- true, and the merged value actually reaching SQL.
                -- assertDirty is omitted here for the same reason: the retained
                -- dirty id is cleared by the very recovery this scenario waits for.
                -- Merge-back is proven decisively below instead - if the failed
                -- flush had DROPPED the +60 rather than re-dirtying it, the
                -- recovered row would read 150, not 210.
                {op="waitForFlush", budgetMs=60000},
                {op="assertCounterValue", counter="dbAvailable", expect="true"},
                {op="assertPersisted", identityIndex=0, xpType="harness_flush_rifle", expect="210"},
                {op="reloadStore"},
                {op="assertXp", identityIndex=0, xpType="harness_flush_rifle", expect="210"}}},
            {name="partial_mint_repair", budgetMs=300000, steps={
                -- See harness_cleanup_reaps: positional indices drift whenever a
                -- scenario is inserted ahead of this one.
                {op="mintIdentity", identityRef="partialBot"},
                {op="deleteProgressionRow", identityRef="partialBot"},
                {op="assertStore", rosterWithoutRecord=1},
                {op="flushNow", force=true},
                {op="assertStore", records=-1, rosterWithoutRecord=0},
                {op="assertCounterDelta", counter="orphanRecords", expect="0"},
                {op="assertXp", identityRef="partialBot", xpType="harness_partial", expect="0"},
                {op="reloadStore"},
                {op="assertXp", identityRef="partialBot", xpType="harness_partial", expect="0"},
                {op="assertCounterDelta", counter="createRefusedNotInRoster", expect="0"},
                {op="deleteIdentity", identityRef="partialBot"},
                {op="runReaper", force=true}}},
            {name="kill_xp_gate_off_no_award", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=85, totalDamage=85,
                    xpType="harness_kill_gate", damage=85, gateEnabled=false},
                {op="assertXp", identityIndex=0, xpType="harness_kill_gate", expect="0"},
                {op="assertCounterDelta", counter="killXp.skippedGateOff", expect="1"},
                {op="assertRecords", delta=0}}},
            {name="kill_xp_formula_solo", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=85, totalDamage=85,
                    xpType="harness_kill_solo", damage=85, gateEnabled=true, rate=1.0},
                {op="assertXp", identityIndex=0, xpType="harness_kill_solo", expect="85"},
                {op="assertXp", identityIndex=0, xpType="combat_general", expect="8"}}},
            {name="kill_xp_per_kill_level_cap", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_level_cap", damage=1000, gateEnabled=true},
                {op="assertXp", identityIndex=0, xpType="harness_kill_level_cap", expect="300"},
                {op="assertCounterDelta", counter="killXp.cappedByLevel", expect="1"}}},
            {name="kill_xp_group_and_gcw_multipliers", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=100, totalDamage=100,
                    xpType="harness_kill_group", damage=100, groupMultiplier=1.2,
                    gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=100, totalDamage=100,
                    xpType="harness_kill_gcw", damage=100, gcwMultiplier=1.5,
                    gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=100, totalDamage=100,
                    xpType="harness_kill_group_gcw", damage=100,
                    groupMultiplier=1.2, gcwMultiplier=1.5, gateEnabled=true},
                {op="assertXp", identityIndex=0, xpType="harness_kill_group", expect="120"},
                {op="assertXp", identityIndex=0, xpType="harness_kill_gcw", expect="150"},
                {op="assertXp", identityIndex=0, xpType="harness_kill_group_gcw", expect="180"},
                {op="assertXp", identityIndex=0, xpType="combat_general", expect="83"}}},
            {name="kill_xp_combat_general_and_dot", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=100, totalDamage=100,
                    damageByType={{xpType="harness_kill_multi", damage=60},
                        {xpType="dotDMG", damage=40}}, gateEnabled=true},
                {op="assertXp", identityIndex=0, xpType="harness_kill_multi", expect="60"},
                {op="assertXp", identityIndex=0, xpType="dotDMG", expect="0"},
                {op="assertXp", identityIndex=0, xpType="combat_general", expect="93"}}},
            {name="kill_xp_rate_knob", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=85, totalDamage=85,
                    xpType="harness_kill_rate", damage=85, gateEnabled=true, rate=2.0},
                {op="assertXp", identityIndex=0, xpType="harness_kill_rate", expect="170"},
                {op="assertXp", identityIndex=0, xpType="combat_general", expect="110"}}},
            {name="kill_xp_lifetime_ceiling", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=1000, totalDamage=1000,
                    xpType="harness_kill_ceiling", damage=1000, gateEnabled=true},
                {op="assertXp", identityIndex=0, xpType="harness_kill_ceiling", expect="2000"},
                {op="assertCounterDelta", counter="killXp.cappedByCeiling", expect="1"}}},
            {name="kill_xp_no_identity_no_award", budgetMs=300000, steps={
                {op="simulateKillXp", bodyOid=999999999, baseXp=85, totalDamage=85,
                    xpType="harness_kill_no_identity", damage=85, gateEnabled=true},
                {op="assertCounterDelta", counter="killXp.skippedNoIdentity", expect="1"},
                {op="assertRecords", delta=0}}},
            {name="kill_xp_zero_and_global_multiplier", budgetMs=300000, steps={
                {op="simulateKillXp", identityIndex=0, baseXp=85, totalDamage=85,
                    xpType="harness_kill_zero_rate", damage=85, gateEnabled=true, rate=0.0},
                {op="simulateKillXp", identityIndex=0, baseXp=1, totalDamage=3,
                    xpType="harness_kill_fractional", damage=1, gateEnabled=true},
                {op="simulateKillXp", identityIndex=0, baseXp=85, totalDamage=85,
                    xpType="harness_kill_global", damage=85, globalMultiplier=2.0,
                    gateEnabled=true},
                {op="assertXp", identityIndex=0, xpType="harness_kill_zero_rate", expect="0"},
                {op="assertXp", identityIndex=0, xpType="harness_kill_fractional", expect="0"},
                {op="assertXp", identityIndex=0, xpType="harness_kill_global", expect="170"},
                {op="assertCounterDelta", counter="killXp.skippedZero", expect="4"},
                {op="assertCounterDelta", counter="rejectedInvalidAmount", expect="0"}}},
            {name="harness_bot_kills_nuna_and_earns_xp", budgetMs=600000,
                requiresGates={"awardKillXp"}, steps={
                {op="mintIdentity", identityRef="killBot"},
                {op="respawnBody", identityRef="killBot"},
                {op="spawnKillTarget", identityRef="killBot"},
                {op="awaitKillTargetDeath", xpType="combat_rangedspecialize_rifle",
                    budgetMs=180000},
                -- expect= is the oracle awaitKillTargetDeath computed from the
                -- victim's own baseXp and the live globalExpMultiplier/killXpRate
                -- captured before combat, NOT a literal: dwarf_nuna's baseXp 85
                -- with a sole attacker and skillTier 1 yields 85 direct and 8
                -- combat_general at the shipped defaults, but a tuned server
                -- must still assert exactly rather than fail a correct award.
                {op="assertXp", identityRef="killBot",
                    xpType="combat_rangedspecialize_rifle", expect="killTargetDirect"},
                {op="assertXp", identityRef="killBot", xpType="combat_general",
                    expect="killTargetCombatGeneral"},
                {op="flushNow", force=true},
                {op="reloadStore"},
                {op="assertXp", identityRef="killBot",
                    xpType="combat_rangedspecialize_rifle", expect="killTargetDirect"},
                {op="assertXp", identityRef="killBot", xpType="combat_general",
                    expect="killTargetCombatGeneral"},
                {op="deleteIdentity", identityRef="killBot"},
                {op="runReaper", force=true}}},
            {name="multi_goal_plan_topology_and_scale", budgetMs=300000, steps={
                -- assertPlanScale independently walks every goal's prerequisite
                -- closure, checks the Kahn order has no duplicates, and pins
                -- the measured 63-box / 250-point result.
                {op="assertPlanScale", trainingPlan="pve_melee_medic",
                    boxCount=63, totalPoints=250}}},
            {name="scalar_goal_skill_backward_compatibility", budgetMs=300000, steps={
                {op="mintIdentity", profession="brawler", trainingPlan="brawler",
                    identityRef="scalarGoalBot"},
                {op="assertPlanOrder", identityRef="scalarGoalBot",
                    skillName="combat_brawler_novice"},
                {op="deleteProgressionRow", identityRef="scalarGoalBot"},
                {op="deleteIdentity", identityRef="scalarGoalBot"}}},
            {name="deferred_box_blocks_when_early_game_is_off", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="mintIdentity", profession="brawler",
                    identityRef="earlyBlockedBot"},
                {op="reassignPlan", identityRef="earlyBlockedBot",
                    trainingPlan="test_defer_medic"},
                {op="trainSkill", identityRef="earlyBlockedBot",
                    skillName="science_medic_novice", freeGrant=true},
                {op="setEarlyGameGate", expect="false"},
                {op="flushNow", force=true},
                -- The explicit freeGrant bootstraps science_medic_novice,
                -- whose prerequisite-free box is not medical-funded. The
                -- remaining injury box is still the next step when the master
                -- gate is off: a deferred type is BLOCKED, never skipped.
                {op="assertNextSkill", identityRef="earlyBlockedBot",
                    skillName="science_medic_injury_01", xpType="medical"},
                {op="deleteProgressionRow", identityRef="earlyBlockedBot"},
                {op="deleteIdentity", identityRef="earlyBlockedBot"}}},
            {name="deferred_box_skips_and_mobile_damage_when_on", budgetMs=600000, steps={
                {op="setTrainingGate", expect="true"},
                {op="setEarlyGameGate", expect="true"},
                {op="setRealWeaponDamageGate", expect="false"},
                {op="mintIdentity", profession="brawler",
                    identityRef="earlyStepBot"},
                {op="reassignPlan", identityRef="earlyStepBot",
                    trainingPlan="test_onehand_weapon"},
                {op="trainSkill", identityRef="earlyStepBot",
                    skillName="combat_brawler_novice", freeGrant=true},
                {op="flushNow", force=true},
                {op="assertNextSkill", identityRef="earlyStepBot",
                    skillName="combat_brawler_1handmelee_01", xpType="combat_meleespecialize_onehand"},
                {op="respawnBody", identityRef="earlyStepBot"},
                {op="assertWeaponXpType", identityRef="earlyStepBot",
                    xpType="combat_meleespecialize_onehand", minDamage=25, maxDamage=40},
                {op="mintIdentity", profession="brawler",
                    identityRef="earlyStepMedicBot"},
                {op="reassignPlan", identityRef="earlyStepMedicBot",
                    trainingPlan="test_defer_medic"},
                {op="trainSkill", identityRef="earlyStepMedicBot",
                    skillName="science_medic_novice", freeGrant=true},
                {op="flushNow", force=true},
                -- With early-game deferral on, the only remaining box is
                -- explicitly deferred medical, so the scorer graduates but
                -- reports it.
                {op="assertActivity", identityRef="earlyStepMedicBot", expect="hunt",
                    graduated=true, stalledBox="science_medic_injury_01"},
                {op="destroyBody", identityRef="earlyStepBot"},
                {op="deleteProgressionRow", identityRef="earlyStepBot"},
                {op="deleteIdentity", identityRef="earlyStepBot"},
                {op="deleteProgressionRow", identityRef="earlyStepMedicBot"},
                {op="deleteIdentity", identityRef="earlyStepMedicBot"}}},
            {name="real_weapon_damage_template_value_when_on", budgetMs=300000, steps={
                {op="setTrainingGate", expect="true"},
                {op="setEarlyGameGate", expect="true"},
                {op="setRealWeaponDamageGate", expect="true"},
                {op="mintIdentity", profession="brawler",
                    identityRef="realDamageBot"},
                {op="reassignPlan", identityRef="realDamageBot",
                    trainingPlan="test_onehand_weapon"},
                {op="trainSkill", identityRef="realDamageBot",
                    skillName="combat_brawler_novice", freeGrant=true},
                {op="flushNow", force=true},
                {op="respawnBody", identityRef="realDamageBot"},
                {op="assertWeaponXpType", identityRef="realDamageBot",
                    xpType="combat_meleespecialize_onehand", minDamage=20, maxDamage=30},
                {op="destroyBody", identityRef="realDamageBot"},
                {op="deleteProgressionRow", identityRef="realDamageBot"},
                {op="deleteIdentity", identityRef="realDamageBot"}}},
            {name="fundable_graduation_and_grind_order_guard", budgetMs=900000, steps={
                {op="setTrainingGate", expect="true"},
                {op="setEarlyGameGate", expect="true"},
                {op="mintIdentity", profession="brawler",
                    identityRef="midLadderBot"},
                {op="reassignPlan", identityRef="midLadderBot",
                    trainingPlan="test_graduation_tiny"},
                {op="trainSkill", identityRef="midLadderBot",
                    skillName="combat_brawler_novice", freeGrant=true},
                {op="mintIdentity", profession="brawler",
                    identityRef="graduatedBot"},
                {op="reassignPlan", identityRef="graduatedBot",
                    trainingPlan="test_graduation_tiny"},
                -- Free-grant all three fundable boxes. The four-box union then
                -- leaves only science_medic_injury_01 deferred.
                {op="trainSkill", identityRef="graduatedBot",
                    skillName="combat_brawler_novice", freeGrant=true},
                {op="assertSkill", identityRef="graduatedBot",
                    skillName="combat_brawler_novice"},
                {op="trainSkill", identityRef="graduatedBot",
                    skillName="combat_brawler_1handmelee_01", freeGrant=true},
                {op="trainSkill", identityRef="graduatedBot",
                    skillName="science_medic_novice", freeGrant=true},
                {op="flushNow", force=true},
                {op="assertActivity", identityRef="graduatedBot", expect="hunt",
                    graduated=true, stalledBox="science_medic_injury_01"},
                {op="assertActivity", identityRef="midLadderBot", expect="grind",
                    graduated=false},
                {op="respawnBody", identityRef="midLadderBot"},
                {op="flushNow", force=true},
                -- The same scorer predicate is applied before demand dispatch.
                {op="assertHuntOrder", identityRef="midLadderBot", expect="0"},
                {op="destroyBody", identityRef="midLadderBot"},
                {op="deleteProgressionRow", identityRef="midLadderBot"},
                {op="deleteIdentity", identityRef="midLadderBot"},
                {op="deleteProgressionRow", identityRef="graduatedBot"},
                {op="deleteIdentity", identityRef="graduatedBot"}}},
            {name="abilities_from_trained_box", budgetMs=300000, steps={
                {op="setNoviceGate", expect="true"},
                {op="mintIdentity", profession="brawler", trainingPlan="brawler",
                    identityRef="abilityNovice"},
                {op="setCombatAbilitiesGate", identityRef="abilityNovice",
                    expect="true", harnessOptIn=true},
                {op="respawnBody", identityRef="abilityNovice"},
                {op="assertAbility", identityRef="abilityNovice",
                    command="intimidate1", expect="present"},
                {op="assertAbility", identityRef="abilityNovice",
                    command="intimidate2", expect="absent"},
                {op="deleteIdentity", identityRef="abilityNovice"},
                {op="setNoviceGate", expect="false"}}},
            {name="abilities_highest_tier_only", budgetMs=600000, steps={
                {op="setNoviceGate", expect="true"},
                {op="mintIdentity", profession="brawler", trainingPlan="brawler",
                    identityRef="abilityTiers"},
                {op="setCombatAbilitiesGate", identityRef="abilityTiers",
                    expect="true", harnessOptIn=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_unarmed_01", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_unarmed_02", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_unarmed_03", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_unarmed_04", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_1handmelee_01", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_1handmelee_02", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_1handmelee_03", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_1handmelee_04", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_2handmelee_01", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_2handmelee_02", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_2handmelee_03", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_2handmelee_04", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_polearm_01", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_polearm_02", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_polearm_03", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_polearm_04", freeGrant=true},
                {op="trainSkill", identityRef="abilityTiers",
                    skillName="combat_brawler_master", freeGrant=true},
                {op="respawnBody", identityRef="abilityTiers"},
                {op="assertAbility", identityRef="abilityTiers",
                    command="intimidate2", expect="present"},
                {op="assertAbilitySuperseded", identityRef="abilityTiers",
                    command="intimidate1", supersededBy="intimidate2"},
                {op="assertAbility", identityRef="abilityTiers",
                    command="intimidate1", expect="absent"},
                {op="deleteIdentity", identityRef="abilityTiers"},
                {op="setNoviceGate", expect="false"}}},
            {name="abilities_gate_off_template", budgetMs=300000, steps={
                {op="setNoviceGate", expect="true"},
                {op="mintIdentity", profession="brawler", trainingPlan="brawler",
                    identityRef="abilityGateOff"},
                {op="setCombatAbilitiesGate", identityRef="abilityGateOff",
                    expect="true", harnessOptIn=true},
                {op="respawnBody", identityRef="abilityGateOff"},
                {op="assertAttackSource", identityRef="abilityGateOff",
                    expect="skill"},
                {op="setCombatAbilitiesGate", identityRef="abilityGateOff",
                    expect="false"},
                {op="assertAttackSource", identityRef="abilityGateOff",
                    expect="template"},
                {op="deleteIdentity", identityRef="abilityGateOff"},
                {op="setNoviceGate", expect="false"}}},
            {name="abilities_used_in_kill", budgetMs=900000,
                requiresGates={"awardKillXp"}, steps={
                {op="setNoviceGate", expect="true"},
                {op="mintIdentity", profession="brawler", trainingPlan="brawler",
                    identityRef="abilityCombat", governed=false},
                {op="setCombatAbilitiesGate", identityRef="abilityCombat",
                    expect="true", harnessOptIn=true, specialAttackChancePercent=100},
                {op="respawnBody", identityRef="abilityCombat"},
                {op="spawnKillTarget", identityRef="abilityCombat"},
                {op="awaitKillTargetDeath", identityRef="abilityCombat",
                    budgetMs=600000},
                {op="assertAbilityUse", identityRef="abilityCombat",
                    command="intimidate1", expect="special"},
                {op="assertAbilityUse", identityRef="abilityCombat",
                    targetState="intimidated"},
                {op="deleteIdentity", identityRef="abilityCombat"},
                {op="setNoviceGate", expect="false"}}},
            {name="mission_credit_awarded", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="missionAward"},
                {op="respawnBody", identityRef="missionAward"},
                {op="setMissionCreditGate", identityRef="missionAward", expect="true"},
                {op="completeMission", identityRef="missionAward",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="assertCreditDeltaInBounds", identityRef="missionAward",
                    difficultyLevel=10, missions=1},
                {op="deleteIdentity", identityRef="missionAward"}}},
            {name="mission_credit_once_per_offer", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="missionOnce"},
                {op="respawnBody", identityRef="missionOnce"},
                {op="setMissionCreditGate", identityRef="missionOnce", expect="true"},
                {op="completeMission", identityRef="missionOnce",
                    difficultyLevel=10, repeatCall=true,
                    expectDisposition="AWARDED",
                    expectRepeatDisposition="NOT_COMPLETED"},
                {op="assertCreditDeltaInBounds", identityRef="missionOnce",
                    difficultyLevel=10, missions=1},
                {op="deleteIdentity", identityRef="missionOnce"}}},
            {name="mission_credit_gate_off", budgetMs=300000,
                requiresGatesOff={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="missionOff"},
                {op="respawnBody", identityRef="missionOff"},
                {op="setMissionCreditGate", identityRef="missionOff", expect="false"},
                {op="completeMission", identityRef="missionOff",
                    difficultyLevel=10, expectDisposition="GATE_OFF"},
                {op="assertCredits", identityRef="missionOff", bank=true,
                    expect="0"},
                {op="deleteIdentity", identityRef="missionOff"}}},
            {name="mission_credit_no_record", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="missionNoRecord"},
                {op="respawnBody", identityRef="missionNoRecord"},
                {op="setMissionCreditGate", identityRef="missionNoRecord", expect="true"},
                {op="deleteProgressionRow", identityRef="missionNoRecord"},
                {op="completeMission", identityRef="missionNoRecord",
                    difficultyLevel=10, expectDisposition="REJECTED_NO_RECORD"},
                {op="assertPersisted", identityRef="missionNoRecord",
                    xpType="bankCredits", expect="missing"},
                {op="deleteIdentity", identityRef="missionNoRecord"}}},
            {name="mission_credit_persisted", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="missionPersisted"},
                {op="respawnBody", identityRef="missionPersisted"},
                {op="setMissionCreditGate", identityRef="missionPersisted", expect="true"},
                {op="completeMission", identityRef="missionPersisted",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="flushNow", force=true},
                {op="assertPersisted", identityRef="missionPersisted",
                    xpType="bankCredits", expect="dynamic"},
                {op="deleteIdentity", identityRef="missionPersisted"}}},
            {name="buff_payment_charges", budgetMs=300000,
                requiresGates={"buffPayment"}, steps={
                {op="mintIdentity", identityRef="buffCharge"},
                {op="respawnBody", identityRef="buffCharge"},
                {op="grantCredits", identityRef="buffCharge", bank=true,
                    amount=1000},
                {op="setBuffPaymentGate", identityRef="buffCharge",
                    expect="true", feeCredits=400},
                {op="reserveDoctorBuffFee", identityRef="buffCharge",
                    expect="RESERVED"},
                {op="commitDoctorBuffFee", identityRef="buffCharge"},
                {op="assertCredits", identityRef="buffCharge", bank=true,
                    expect="600"},
                {op="deleteIdentity", identityRef="buffCharge"}}},
            {name="buff_payment_unaffordable", budgetMs=300000,
                requiresGates={"buffPayment"}, steps={
                {op="mintIdentity", identityRef="buffPoor"},
                {op="respawnBody", identityRef="buffPoor"},
                {op="grantCredits", identityRef="buffPoor", bank=true,
                    amount=100},
                {op="setBuffPaymentGate", identityRef="buffPoor",
                    expect="true", feeCredits=400},
                {op="reserveDoctorBuffFee", identityRef="buffPoor",
                    expect="UNAFFORDABLE"},
                {op="commitDoctorBuffFee", identityRef="buffPoor"},
                {op="assertCredits", identityRef="buffPoor", bank=true,
                    expect="100"},
                {op="deleteIdentity", identityRef="buffPoor"}}},
            {name="buff_payment_released", budgetMs=300000,
                requiresGates={"buffPayment"}, steps={
                {op="mintIdentity", identityRef="buffRelease"},
                {op="respawnBody", identityRef="buffRelease"},
                {op="grantCredits", identityRef="buffRelease", bank=true,
                    amount=1000},
                {op="setBuffPaymentGate", identityRef="buffRelease",
                    expect="true", feeCredits=400},
                {op="reserveDoctorBuffFee", identityRef="buffRelease",
                    expect="RESERVED"},
                {op="releaseDoctorBuffFee", identityRef="buffRelease"},
                {op="reserveDoctorBuffFee", identityRef="buffRelease",
                    expect="RESERVED"},
                {op="releaseDoctorBuffFee", identityRef="buffRelease"},
                {op="assertCredits", identityRef="buffRelease", bank=true,
                    expect="1000"},
                {op="deleteIdentity", identityRef="buffRelease"}}},
            {name="buff_payment_fee_snapshot", budgetMs=300000,
                requiresGates={"buffPayment"}, steps={
                {op="mintIdentity", identityRef="buffSnapshot"},
                {op="respawnBody", identityRef="buffSnapshot"},
                {op="grantCredits", identityRef="buffSnapshot", bank=true,
                    amount=1000},
                {op="setBuffPaymentGate", identityRef="buffSnapshot",
                    expect="true", feeCredits=400},
                {op="reserveDoctorBuffFee", identityRef="buffSnapshot",
                    expect="RESERVED"},
                {op="setBuffPaymentGate", identityRef="buffSnapshot",
                    expect="true", feeCredits=900},
                {op="commitDoctorBuffFee", identityRef="buffSnapshot"},
                {op="assertCredits", identityRef="buffSnapshot", bank=true,
                    expect="600"},
                {op="deleteIdentity", identityRef="buffSnapshot"}}},
            {name="buff_payment_gate_off_or_zero_fee", budgetMs=300000,
                requiresGates={"buffPayment"}, steps={
                {op="mintIdentity", identityRef="buffZero"},
                {op="respawnBody", identityRef="buffZero"},
                {op="setBuffPaymentGate", identityRef="buffZero",
                    expect="true", feeCredits=0},
                {op="reserveDoctorBuffFee", identityRef="buffZero",
                    expect="NOT_CHARGED"},
                {op="assertCredits", identityRef="buffZero", bank=true,
                    expect="0"},
                {op="deleteIdentity", identityRef="buffZero"}}},
            {name="buff_payment_gate_off_mid_trip", budgetMs=300000,
                requiresGates={"buffPayment"}, steps={
                {op="mintIdentity", identityRef="buffMidTrip"},
                {op="respawnBody", identityRef="buffMidTrip"},
                {op="grantCredits", identityRef="buffMidTrip", bank=true,
                    amount=1000},
                {op="setBuffPaymentGate", identityRef="buffMidTrip",
                    expect="true", feeCredits=400},
                {op="reserveDoctorBuffFee", identityRef="buffMidTrip",
                    expect="RESERVED"},
                {op="setBuffPaymentGate", identityRef="buffMidTrip",
                    expect="false", feeCredits=400},
                {op="commitDoctorBuffFee", identityRef="buffMidTrip"},
                {op="assertCredits", identityRef="buffMidTrip", bank=true,
                    expect="1000"},
                {op="deleteIdentity", identityRef="buffMidTrip"}}},
            {name="ledger_records_grant_and_spend", budgetMs=300000,
                requiresGates={"awardMissionCredits", "buffPayment"}, steps={
                {op="mintIdentity", identityRef="ledgerGrantSpend"},
                {op="respawnBody", identityRef="ledgerGrantSpend"},
                {op="setMissionCreditGate", identityRef="ledgerGrantSpend",
                    expect="true"},
                {op="setBuffPaymentGate", identityRef="ledgerGrantSpend",
                    expect="true", feeCredits=400},
                {op="completeMission", identityRef="ledgerGrantSpend",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="reserveDoctorBuffFee", identityRef="ledgerGrantSpend",
                    expect="RESERVED"},
                {op="commitDoctorBuffFee", identityRef="ledgerGrantSpend"},
                {op="flushNow", force=true},
                {op="assertLedger", identityRef="ledgerGrantSpend",
                    source="mission", count=1},
                {op="assertLedger", identityRef="ledgerGrantSpend",
                    source="doctor_buff", delta=-400, count=1},
                {op="deleteIdentity", identityRef="ledgerGrantSpend"}}},
            {name="ledger_not_duplicated_after_failed_flush", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="ledgerNoDuplicate"},
                {op="respawnBody", identityRef="ledgerNoDuplicate"},
                {op="setMissionCreditGate", identityRef="ledgerNoDuplicate",
                    expect="true"},
                {op="completeMission", identityRef="ledgerNoDuplicate",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="injectFault", failNextFlush=true},
                {op="flushNow", force=true},
                {op="flushNow", force=true},
                {op="assertLedger", identityRef="ledgerNoDuplicate",
                    source="mission", count=1},
                {op="deleteIdentity", identityRef="ledgerNoDuplicate"}}},
            {name="ledger_requeued_after_failed_insert", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="ledgerRequeued"},
                {op="respawnBody", identityRef="ledgerRequeued"},
                {op="setMissionCreditGate", identityRef="ledgerRequeued",
                    expect="true"},
                {op="completeMission", identityRef="ledgerRequeued",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="injectFault", failLedgerInsert=true},
                {op="flushNow", force=true},
                -- The failed insert is retried by the maintenance lane before a
                -- queued SQL assertion can observe the gap (live run
                -- 20261013-225200: the row was already written), so the oracle
                -- is the fault having fired plus the row existing exactly once.
                {op="assertCounterDelta", counter="flushFailures", expect="1"},
                {op="flushNow", force=true},
                {op="assertLedger", identityRef="ledgerRequeued",
                    source="mission", count=1},
                {op="assertLedgerPending", identityRef="ledgerRequeued",
                    expect="0"},
                {op="deleteIdentity", identityRef="ledgerRequeued"}}},
            {name="ledger_removed_with_identity", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="ledgerRemoved"},
                {op="respawnBody", identityRef="ledgerRemoved"},
                {op="setMissionCreditGate", identityRef="ledgerRemoved",
                    expect="true"},
                {op="completeMission", identityRef="ledgerRemoved",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="flushNow", force=true},
                {op="assertLedger", identityRef="ledgerRemoved",
                    source="mission", count=1},
                {op="deleteIdentity", identityRef="ledgerRemoved"},
                {op="runReaper", force=true},
                {op="assertLedger", identityRef="ledgerRemoved",
                    source="mission", count=0}}},
            {name="ledger_reconciles_mismatch", budgetMs=300000,
                requiresGates={"awardMissionCredits"}, steps={
                {op="mintIdentity", identityRef="ledgerMismatch"},
                {op="respawnBody", identityRef="ledgerMismatch"},
                {op="setMissionCreditGate", identityRef="ledgerMismatch",
                    expect="true"},
                {op="completeMission", identityRef="ledgerMismatch",
                    difficultyLevel=10, expectDisposition="AWARDED"},
                {op="flushNow", force=true},
                {op="writeBankCredits", identityRef="ledgerMismatch",
                    amount=9999},
                {op="reloadStore"},
                {op="flushNow", force=true},
                {op="assertLedger", identityRef="ledgerMismatch",
                    source="reconcile", count=1},
                {op="assertPersisted", identityRef="ledgerMismatch",
                    xpType="bankCredits", expect="dynamic"},
                {op="deleteIdentity", identityRef="ledgerMismatch"},
                {op="runReaper", force=true}}},
            -- P.10d loot/vault parity scenarios. These remain inert with the
            -- harness disabled and use only non-keyword field names so the
            -- hot-reload parser stays compatible with Lua 5.1.
            {name="loot_items_to_vault", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootItems"},
                {op="respawnBody", identityRef="lootItems"},
                {op="setLootGate", identityRef="lootItems", lootGate=true},
                {op="spawnKillTarget", identityRef="lootItems"},
                {op="awaitKillTargetDeath", identityRef="lootItems", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="lootItems", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="seedCorpseLoot", identityRef="lootItems", lootTemplate="object/weapon/ranged/rifle/rifle_dlt20.iff"},
                {op="lootCorpse", identityRef="lootItems", expect="LOOTED"},
                {op="assertVaultItems", identityRef="lootItems", itemCount=2},
                {op="assertItemRow", identityRef="lootItems", itemState="held", itemCount=2},
                {op="assertItemStats", identityRef="lootItems"}}},
            {name="loot_cash_to_store", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootCash"},
                {op="respawnBody", identityRef="lootCash"},
                {op="setLootGate", identityRef="lootCash", lootGate=true},
                {op="spawnKillTarget", identityRef="lootCash"},
                {op="awaitKillTargetDeath", identityRef="lootCash", keepCorpse=true},
                {op="seedCorpseCash", identityRef="lootCash", amount=123},
                {op="lootCorpse", identityRef="lootCash", expect="LOOTED"},
                {op="assertCredits", identityRef="lootCash", bank=false, expect="123"},
                {op="flushNow", force=true}}},
            {name="loot_gate_off_legacy", budgetMs=900000,
                requiresGatesOff={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootOff"},
                {op="respawnBody", identityRef="lootOff"},
                {op="setLootGate", identityRef="lootOff", lootGate=false},
                {op="spawnKillTarget", identityRef="lootOff"},
                {op="awaitKillTargetDeath", identityRef="lootOff", keepCorpse=true},
                {op="assertCorpseDespawn", identityRef="lootOff", despawnSeconds=10}}},
            {name="loot_corpse_owner_is_bot", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootOwner"},
                {op="respawnBody", identityRef="lootOwner"},
                {op="setLootGate", identityRef="lootOwner", lootGate=true},
                {op="spawnKillTarget", identityRef="lootOwner"},
                {op="awaitKillTargetDeath", identityRef="lootOwner", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="lootOwner", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="assertCorpseOwner", identityRef="lootOwner"},
                {op="assertCorpseItems", identityRef="lootOwner", corpseItemCount=1}}},
            {name="loot_vault_cap", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootCap"},
                {op="respawnBody", identityRef="lootCap"},
                {op="setLootGate", identityRef="lootCap", lootGate=true},
                {op="setLootCap", identityRef="lootCap", lootCap=1},
                {op="spawnKillTarget", identityRef="lootCap"},
                {op="awaitKillTargetDeath", identityRef="lootCap", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="lootCap", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="seedCorpseLoot", identityRef="lootCap", lootTemplate="object/weapon/ranged/rifle/rifle_dlt20.iff"},
                {op="lootCorpse", identityRef="lootCap"},
                {op="assertVaultItems", identityRef="lootCap", itemCount=1},
                {op="assertCorpseItems", identityRef="lootCap", corpseItemCount=1},
                {op="assertCounterDelta", counter="loot.skippedVaultFull", expect="1"}}},
            {name="loot_death_before_loot", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootDeath"},
                {op="respawnBody", identityRef="lootDeath"},
                {op="setLootGate", identityRef="lootDeath", lootGate=true},
                {op="spawnKillTarget", identityRef="lootDeath"},
                {op="awaitKillTargetDeath", identityRef="lootDeath", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="lootDeath", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="killBodyBeforeLoot", identityRef="lootDeath"},
                {op="lootCorpse", identityRef="lootDeath", expect="NOT_CREATED"},
                {op="assertVaultItems", identityRef="lootDeath", itemCount=0},
                {op="assertCorpseItems", identityRef="lootDeath", corpseItemCount=1}}},
            {name="loot_transfer_fault", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootFault"},
                {op="respawnBody", identityRef="lootFault"},
                {op="setLootGate", identityRef="lootFault", lootGate=true},
                {op="spawnKillTarget", identityRef="lootFault"},
                {op="awaitKillTargetDeath", identityRef="lootFault", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="lootFault", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="failItemTransfer"},
                {op="lootCorpse", identityRef="lootFault"},
                {op="assertCorpseItems", identityRef="lootFault", corpseItemCount=1},
                {op="assertItemRow", identityRef="lootFault", itemState="held", itemCount=0}}},
            {name="loot_no_record", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootNoRecord"},
                {op="respawnBody", identityRef="lootNoRecord"},
                {op="setLootGate", identityRef="lootNoRecord", lootGate=true},
                {op="assertLootOwner", identityRef="lootNoRecord", expect="self"},
                {op="deleteProgressionRow", identityRef="lootNoRecord"},
                {op="assertLootOwner", identityRef="lootNoRecord", expect="none"},
                {op="assertCounterDelta", counter="loot.notCreated.noRecord", expect="1"}}},
            {name="vault_reconcile_states", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="reconcileItems"},
                {op="respawnBody", identityRef="reconcileItems"},
                {op="setLootGate", identityRef="reconcileItems", lootGate=true},
                {op="spawnKillTarget", identityRef="reconcileItems"},
                {op="awaitKillTargetDeath", identityRef="reconcileItems", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="reconcileItems", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="lootCorpse", identityRef="reconcileItems"},
                {op="forgeItemRow", identityRef="reconcileItems", itemState="consumed", inVault=true},
                {op="runItemReconcile"},
                {op="assertItemRow", identityRef="reconcileItems", itemState="orphaned", itemCount=1, bySeededItem=true},
                {op="assertVaultItems", identityRef="reconcileItems", itemCount=0}}},
            {name="reaper_tears_down_items", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="reapItems"},
                {op="respawnBody", identityRef="reapItems"},
                {op="setLootGate", identityRef="reapItems", lootGate=true},
                {op="spawnKillTarget", identityRef="reapItems"},
                {op="awaitKillTargetDeath", identityRef="reapItems", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="reapItems", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="lootCorpse", identityRef="reapItems", expect="LOOTED"},
                {op="assertVaultItems", identityRef="reapItems", itemCount=1},
                {op="deleteIdentity", identityRef="reapItems"},
                {op="runReaper", force=true},
                {op="assertItemDestroyed", identityRef="reapItems"}}},
            -- The production loot walk: a real roster hunter (not a harness
            -- body) is attacked on a travel leg by a creature with loot, kills
            -- it, walks to the corpse, loots it and resumes its leg.
            {name="loot_production_walk", budgetMs=1500000,
                requiresGates={"lootEnabled"}, steps={
                {op="provokeLootKill", template="jawa", budgetMs=1200000},
                {op="awaitBotLooted", budgetMs=300000},
                {op="assertHunterResumed", budgetMs=120000}}},
            {name="loot_two_boot_snapshot", budgetMs=900000,
                requiresGates={"lootEnabled"}, steps={
                {op="mintIdentity", identityRef="lootProbe"},
                {op="respawnBody", identityRef="lootProbe"},
                {op="setLootGate", identityRef="lootProbe", lootGate=true},
                {op="spawnKillTarget", identityRef="lootProbe"},
                {op="awaitKillTargetDeath", identityRef="lootProbe", keepCorpse=true},
                {op="seedCorpseLoot", identityRef="lootProbe", lootTemplate="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff"},
                {op="lootCorpse", identityRef="lootProbe"},
                {op="flushNow", force=true},
                {op="reloadStore"},
                {op="assertVaultItems", identityRef="lootProbe", itemCount=1},
                {op="assertItemRow", identityRef="lootProbe", itemState="held", itemCount=1},
                {op="assertItemStats", identityRef="lootProbe"},
                {op="deleteIdentity", identityRef="lootProbe"},
                {op="runReaper", force=true}}},
            -- P.10j bazaar SELL matrix.  The whole parity harness remains
            -- disabled by default; these entries are intentionally additive
            -- so the live-only progression settings above are untouched.
            {name="bazaar_gate_off", budgetMs=900000,
                requiresGatesOff={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarOff"},
                {op="respawnBody", identityRef="bazaarOff"},
                {op="setBazaarSellGate", identityRef="bazaarOff", bazaarSellGate=false},
                {op="seedVaultItem", identityRef="bazaarOff", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarOff"},
                {op="assertListing", identityRef="bazaarOff", expect="missing"},
                {op="deleteIdentity", identityRef="bazaarOff"}}},
            {name="bazaar_list_from_vault", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarList"},
                {op="respawnBody", identityRef="bazaarList"},
                {op="setBazaarSellGate", identityRef="bazaarList", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarList", bank=true, amount=1000},
                {op="seedVaultItem", identityRef="bazaarList", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarList"},
                {op="assertListing", identityRef="bazaarList", itemState="listed", ownerIsBag=true},
                {op="assertItemRow", identityRef="bazaarList", itemState="listed", itemCount=1},
                {op="deleteIdentity", identityRef="bazaarList"}}},
            {name="bazaar_skip_generic_junk", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarJunk"},
                {op="respawnBody", identityRef="bazaarJunk"},
                {op="setBazaarSellGate", identityRef="bazaarJunk", bazaarSellGate=true},
                {op="seedVaultItem", identityRef="bazaarJunk", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100, junkMask=1},
                {op="bazaarListAt", identityRef="bazaarJunk"},
                {op="assertCounterDelta", counter="bazaar.skipped.junk", expect="1"},
                {op="deleteIdentity", identityRef="bazaarJunk"}}},
            {name="bazaar_never_undercut", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarFloor"},
                {op="respawnBody", identityRef="bazaarFloor"},
                {op="setBazaarSellGate", identityRef="bazaarFloor", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarFloor", bank=true, amount=5000},
                {op="createHumanListing", planet="tatooine", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", price=400},
                {op="seedVaultItem", identityRef="bazaarFloor", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarFloor"},
                {op="assertListing", identityRef="bazaarFloor", itemState="listed", price=400},
                {op="deleteIdentity", identityRef="bazaarFloor"}}},
            -- Stock caps a human ask at MAXBAZAARPRICE, so a human floor can at
            -- most equal the cap: the bot matches it (equal is not undercut).
            -- The strictly-above-cap skip is covered by SimBotBazaarTest.
            {name="bazaar_human_above_cap", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarAboveCap"},
                {op="respawnBody", identityRef="bazaarAboveCap"},
                {op="setBazaarSellGate", identityRef="bazaarAboveCap", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarAboveCap", bank=true, amount=5000},
                {op="createHumanListing", planet="tatooine", template="object/weapon/ranged/rifle/rifle_dlt20.iff", price=20000},
                {op="seedVaultItem", identityRef="bazaarAboveCap", template="object/weapon/ranged/rifle/rifle_dlt20.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarAboveCap"},
                {op="assertListing", identityRef="bazaarAboveCap", itemState="listed", price=20000},
                {op="deleteIdentity", identityRef="bazaarAboveCap"}}},
            {name="bazaar_sale_settles", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarSale"},
                {op="respawnBody", identityRef="bazaarSale"},
                {op="setBazaarSellGate", identityRef="bazaarSale", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarSale", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarSale", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarSale"},
                {op="buyListing"},
                {op="runBazaarSweep", identityRef="bazaarSale"},
                {op="assertListing", identityRef="bazaarSale", itemState="sold"},
                {op="assertItemRow", identityRef="bazaarSale", itemState="consumed", itemCount=0},
                {op="flushNow", force=true},
                {op="deleteIdentity", identityRef="bazaarSale"}}},
            {name="bazaar_name_collision", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarCollision"},
                {op="respawnBody", identityRef="bazaarCollision"},
                {op="setBazaarSellGate", identityRef="bazaarCollision", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarCollision", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarCollision", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarCollision"},
                {op="buyListing", identityRef="bazaarCollision", ownerName="@seller"},
                {op="runBazaarSweep", identityRef="bazaarCollision"},
                {op="flushNow", force=true},
                {op="runBazaarSweep", identityRef="bazaarCollision"},
                {op="assertListing", identityRef="bazaarCollision", itemState="sold", ownerIsBag=false},
                {op="deleteIdentity", identityRef="bazaarCollision"}}},
            {name="bazaar_expiry_relist", budgetMs=1200000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarRelist"},
                {op="respawnBody", identityRef="bazaarRelist"},
                {op="setBazaarSellGate", identityRef="bazaarRelist", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarRelist", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarRelist", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarRelist"},
                {op="expireListing", identityRef="bazaarRelist"},
                {op="runBazaarSweep", identityRef="bazaarRelist"},
                {op="setBazaarMarkup", markup=10.0},
                {op="bazaarListAt", identityRef="bazaarRelist"},
                {op="assertListing", identityRef="bazaarRelist", itemState="listed", price=300},
                {op="expireListing", identityRef="bazaarRelist"},
                {op="runBazaarSweep", identityRef="bazaarRelist"},
                {op="createHumanListing", planet="tatooine", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", price=400},
                {op="bazaarListAt", identityRef="bazaarRelist"},
                {op="assertListing", identityRef="bazaarRelist", itemState="listed", price=400},
                {op="deleteIdentity", identityRef="bazaarRelist"}}},
            {name="bazaar_sale_during_publish", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarPublish"},
                {op="respawnBody", identityRef="bazaarPublish"},
                {op="setBazaarSellGate", identityRef="bazaarPublish", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarPublish", bank=true, amount=5000},
                {op="bazaarPublishFault"},
                {op="seedVaultItem", identityRef="bazaarPublish", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarPublish"},
                {op="runBazaarSweep", identityRef="bazaarPublish"},
                {op="assertListing", identityRef="bazaarPublish", itemState="sold"},
                {op="assertItemRow", identityRef="bazaarPublish", itemState="consumed", itemCount=0},
                {op="flushNow", force=true},
                {op="deleteIdentity", identityRef="bazaarPublish"}}},
            {name="bazaar_settlement_mode", budgetMs=1200000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarSettlement"},
                {op="respawnBody", identityRef="bazaarSettlement"},
                {op="setBazaarSellGate", identityRef="bazaarSettlement", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarSettlement", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarSettlement", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarSettlement"},
                {op="setProgressionEnabled", expect="false"},
                {op="reloadStore"},
                {op="buyListing"},
                {op="runBazaarSweep", identityRef="bazaarSettlement"},
                {op="flushNow", force=true},
                {op="runBazaarSweep", identityRef="bazaarSettlement"},
                {op="assertListing", identityRef="bazaarSettlement", itemState="sold"},
                {op="setProgressionEnabled", expect="true"},
                {op="deleteIdentity", identityRef="bazaarSettlement"}}},
            {name="bazaar_settle_replay", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarReplay"},
                {op="respawnBody", identityRef="bazaarReplay"},
                {op="setBazaarSellGate", identityRef="bazaarReplay", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarReplay", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarReplay", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarReplay"},
                {op="buyListing"},
                {op="forgeListingRow", identityRef="bazaarReplay", amount=1},
                {op="forgeListingRow", identityRef="bazaarReplay", ledgerCount=-1},
                {op="runBazaarSweep", identityRef="bazaarReplay"},
                {op="runBazaarSweep", identityRef="bazaarReplay"},
                {op="assertListing", identityRef="bazaarReplay", itemState="sold"},
                {op="assertItemRow", identityRef="bazaarReplay", itemState="consumed", itemCount=0},
                {op="flushNow", force=true},
                {op="deleteIdentity", identityRef="bazaarReplay"}}},
            -- Instant pickup (owner decision 2026-10-15): the buyer owns the item
            -- the moment it is bought, before any sweep or flush.
            {name="bazaar_instant_pickup", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarPickup"},
                {op="respawnBody", identityRef="bazaarPickup"},
                {op="setBazaarSellGate", identityRef="bazaarPickup", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarPickup", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarPickup", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarPickup"},
                {op="buyListing"},
                {op="assertListing", identityRef="bazaarPickup", ownerIsBag=false},
                {op="runBazaarSweep", identityRef="bazaarPickup"},
                {op="assertListing", identityRef="bazaarPickup", itemState="sold", ownerIsBag=false},
                {op="flushNow", force=true},
                {op="deleteIdentity", identityRef="bazaarPickup"}}},
            -- A sale whose settlement POD was lost: the buyer already owns the
            -- item, and boot reconciliation pays the bot from the buyer-owned
            -- SOLD record.
            {name="bazaar_lost_sale_recovered", budgetMs=1200000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarLost"},
                {op="respawnBody", identityRef="bazaarLost"},
                {op="setBazaarSellGate", identityRef="bazaarLost", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarLost", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarLost", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarLost"},
                {op="flushNow", force=true},
                {op="suppressBazaarSalePod", suppressBazaarSalePod=true},
                {op="buyListing"},
                {op="assertListing", identityRef="bazaarLost", itemState="listed", ownerIsBag=false},
                {op="reloadStore"},
                {op="waitForFlush", budgetMs=180000},
                {op="assertListing", identityRef="bazaarLost", itemState="sold"},
                {op="deleteIdentity", identityRef="bazaarLost"}}},
            {name="bazaar_unresolvable_seller", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarUnresolved"},
                {op="respawnBody", identityRef="bazaarUnresolved"},
                {op="setBazaarSellGate", identityRef="bazaarUnresolved", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarUnresolved", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarUnresolved", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarUnresolved"},
                -- Instant pickup settles at once, so the seller record is
                -- removed BEFORE the buy: settlement then finds nobody to pay.
                {op="deleteProgressionRow", identityRef="bazaarUnresolved"},
                {op="buyListing"},
                {op="runBazaarSweep", identityRef="bazaarUnresolved"},
                {op="assertCounterDelta", counter="bazaar.saleUnresolved", expect="1"},
                {op="deleteIdentity", identityRef="bazaarUnresolved"}}},
            {name="bazaar_flush_fault_mid_batch", budgetMs=1200000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarFlushFault"},
                {op="respawnBody", identityRef="bazaarFlushFault"},
                {op="setBazaarSellGate", identityRef="bazaarFlushFault", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarFlushFault", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarFlushFault", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarFlushFault"},
                {op="buyListing"},
                {op="runBazaarSweep", identityRef="bazaarFlushFault"},
                {op="injectFault", failLedgerInsert=true},
                {op="flushNow", force=true},
                {op="flushNow", force=true},
                {op="assertListing", identityRef="bazaarFlushFault", itemState="sold"},
                {op="deleteIdentity", identityRef="bazaarFlushFault"}}},
            {name="bazaar_void_row_not_resurrected", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarVoid"},
                {op="respawnBody", identityRef="bazaarVoid"},
                {op="setBazaarSellGate", identityRef="bazaarVoid", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarVoid", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarVoid", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarVoid"},
                {op="forgeListingRow", identityRef="bazaarVoid", amount=1},
                {op="reloadStore"},
                {op="runBazaarSweep", identityRef="bazaarVoid"},
                {op="deleteIdentity", identityRef="bazaarVoid"}}},
            {name="bazaar_drain_on_disable", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarDrain"},
                {op="respawnBody", identityRef="bazaarDrain"},
                {op="setBazaarSellGate", identityRef="bazaarDrain", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarDrain", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarDrain", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarDrain"},
                {op="setBazaarSellGate", identityRef="bazaarDrain", bazaarSellGate=false},
                {op="runBazaarSweep", identityRef="bazaarDrain"},
                {op="assertCounterDelta", counter="bazaar.drained", expect="1"},
                {op="deleteIdentity", identityRef="bazaarDrain"}}},
            {name="bazaar_listing_cap", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarCap"},
                {op="respawnBody", identityRef="bazaarCap"},
                {op="setBazaarSellGate", identityRef="bazaarCap", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarCap", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarCap", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="seedVaultItem", identityRef="bazaarCap", template="object/weapon/ranged/rifle/rifle_dlt20.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarCap"},
                {op="assertCounterDelta", counter="bazaar.skipped.cap", expect="0"},
                {op="deleteIdentity", identityRef="bazaarCap"},
                {op="mintIdentity", identityRef="bazaarFee"},
                {op="respawnBody", identityRef="bazaarFee"},
                {op="setBazaarSellGate", identityRef="bazaarFee", bazaarSellGate=true},
                {op="seedVaultItem", identityRef="bazaarFee", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarFee"},
                {op="assertCounterDelta", counter="bazaar.skipped.unaffordable", expect="1"},
                {op="deleteIdentity", identityRef="bazaarFee"}}},
            {name="bazaar_delete_with_listing", budgetMs=900000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarDelete"},
                {op="respawnBody", identityRef="bazaarDelete"},
                {op="setBazaarSellGate", identityRef="bazaarDelete", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarDelete", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarDelete", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarDelete"},
                {op="deleteIdentity", identityRef="bazaarDelete"},
                {op="runReaper", force=true}}},
            {name="bazaar_reconcile_states", budgetMs=1200000,
                requiresGates={"bazaarSell"}, steps={
                {op="mintIdentity", identityRef="bazaarReconcile"},
                {op="respawnBody", identityRef="bazaarReconcile"},
                {op="setBazaarSellGate", identityRef="bazaarReconcile", bazaarSellGate=true},
                {op="grantCredits", identityRef="bazaarReconcile", bank=true, amount=5000},
                {op="seedVaultItem", identityRef="bazaarReconcile", template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100},
                {op="bazaarListAt", identityRef="bazaarReconcile"},
                {op="forgeListingRow", identityRef="bazaarReconcile", itemState="listed"},
                {op="runBazaarSweep", identityRef="bazaarReconcile"},
                {op="deleteIdentity", identityRef="bazaarReconcile"}}},
            -- Same probe pattern as retained_record_survives_restart: a harness
            -- POOL identity (index 1 in phase A, the preserved probe is index 0
            -- in phase B), never a minted named ref - named refs are deleted
            -- by the end-of-run cleanup and would not survive to phase B.
            {name="bazaar_restart_probe", budgetMs=1200000, steps={
                {op="setBazaarSellGate", identityIndex=1, bazaarSellGate=true, restartPhase="A"},
                {op="grantCredits", identityIndex=1, bank=true, amount=5000, restartPhase="A"},
                {op="seedVaultItem", identityIndex=1, template="object/tangible/component/weapon/acklay_bone_reinforcement_core.iff", amount=100, restartPhase="A"},
                {op="bazaarListAt", identityIndex=1, restartPhase="A"},
                {op="assertListing", identityIndex=1, itemState="listed", ownerIsBag=true, restartPhase="A"},
                {op="writeRestartProbe", identityIndex=1, restartPhase="A"},
                {op="flushNow", force=true, restartPhase="A"},
                {op="assertListing", identityIndex=0, itemState="listed", ownerIsBag=true, restartPhase="B"},
                {op="deleteIdentity", identityIndex=0, restartPhase="B"},
                {op="runReaper", force=true, restartPhase="B"}}},
            {name="bazaar_production_visit", budgetMs=3900000,
                requiresGates={"bazaarSell"}, steps={
                {op="resetBazaarVisitCooldowns"},
                -- awaitBazaarVisit itself requires a new visit, a listing from
                -- it and the hunter back on its job. Several roster bots may
                -- visit inside the window, so no exact counter delta here.
                {op="awaitBazaarVisit", budgetMs=3600000}}},
        },
    },

    -- P.10a: manager-owned progression ledger. Capability gates added by
    -- later chunks belong in this block: awardKillXp, awardMissionCredits,
    -- lootEnabled, groupLoot, trainingEnabled, bazaarSell, and bazaarBuy.
    -- The foundation is intentionally inert until its live receipt is accepted.
    playerBotProgression = {
        enabled = false,
        awardKillXp = false,
        -- F_0.9.2 (P.10c): mission credits and the doctor-buff fee (a fee of 0
        -- charges nothing). ledgerRetentionDays = 0 keeps the journal forever.
        awardMissionCredits = false,
        -- F_0.9.3 (P.10d): loot from kills into the hive item vault. A bot at
        -- maxItemsPerBot takes the cash and leaves the items on the corpse.
        lootEnabled = false,
        loot = {
            maxItemsPerBot = 80,
            arriveMeters = 4,
            maxDetourMeters = 64,
            claimTtlSeconds = 280,
            maxClaimsPerBot = 8,
            stepTimeoutSeconds = 90,
        },
        bazaarSell = false,
        bazaarSaleBacklogMax = 256,
        bazaar = {
            markup = 3.0,
            minPrice = 50,
            maxPrice = 20000,
            maxListingsPerBot = 10,
            listingsPerVisit = 3,
            durationHours = 168,
            cooldownSeconds = 120,
            detourSeconds = 300,
            arriveMeters = 4,
            timeoutSeconds = 180,
            sweepIntervalSeconds = 300,
            sweepBatch = 20,
            saleBacklogMax = 256,
            drainOnDisable = true,
            -- F_0.9.9 bazaar visit names; legacy aliases above remain
            -- readable during hot reloads of live profiles.
            maxListingsPerVisit = 3,
            listingDurationHours = 168,
            visitCooldownMinutes = 120,
            maxDetourMeters = 300,
            visitTimeoutSeconds = 180,
        },
        buffPayment = {
            enabled = false,
            doctorFeeCredits = 0,
        },
        ledgerRetentionDays = 30,
        trainingEnabled = false,
        earlyGameEnabled = false,
        combatAbilitiesEnabled = false,
        -- Phase 0 lair diagnosis/tactics gate. Keep all new tactic behaviour
        -- reversible until the live hold point is cleared.
        lairTacticsEnabled = false,
        combatAbilities = {
            specialAttackChancePercent = 35,
            preferFreeStateAbilities = true,
            hamReserveFraction = 0.40,
            -- After forcing a free control ability (Intimidate) on a target, wait
            -- this long before forcing it again on the same target.
            freeStateRetrySeconds = 10,
            -- Area-aggro commands pull nearby creatures onto a solo grinder; the
            -- live repro loop saw a forced-special novice fight bystanders for
            -- its whole kill budget. Remove entries to re-enable them.
            excludedCommands = {"taunt", "warcry1", "warcry2"},
            -- F_0.9.8d (owner 2026-10-11). A bot fighting its mission lair's
            -- creatures must not clip the lair with an area or cone special:
            -- one stray hit spawns the next wave on top of the one it is still
            -- clearing. Nordihi's unarmedSpinAttack2 (16m area) made all 25 of
            -- its lair hits that way and it went 0 for 6. Skips such a command
            -- while the lair is within its reach (+8m) and the bot is not
            -- deliberately attacking the lair; the ability stays available
            -- everywhere else.
            avoidAreaAttacksNearLair = true,
            usageHistorySize = 12,
        },
        earlyGame = {
            realWeaponDamage = false,
            -- P.10g grind-loop defaults. The master gate above remains OFF;
            -- these values only become active when earlyGameEnabled is on.
            grindAnchorMinMeters = 400,
            grindAnchorMaxMeters = 900,
            anchorMaxAttempts = 8,
            anchorArriveMeters = 32,
            scanMaxAttempts = 3,
            scanRetrySeconds = 5,
            -- No floor for novices: the weak creatures around a city (worrts,
            -- lesser womp rats, mynocks, kreetles) die in ~5-13 s unarmed, and a
            -- 20 s floor left no eligible target inside the level band (quick
            -- check 20260915-162145). The band and maxTtkSeconds still guard.
            minTtkSeconds = 0,
            maxTtkSeconds = 900,
            -- Level band around the novice's skill tier (its level, rising as it
            -- trains): targets from tier - levelBandBelow to tier + levelBandAbove,
            -- preferring the closest level and taking the band edges only when
            -- nothing closer is in range. Self-defence uses only the upper bound.
            -- The TTK window above still rejects fights that cannot be won.
            levelBandBelow = 3,
            levelBandAbove = 3,
            -- Seek targets that do not pull their group into the fight: a novice
            -- dies to anything that swarms it. Only PACK creatures assist their
            -- social group (AiAgent::notifyPackMobs), so herds such as kreetles and
            -- minor worrts are fair game. Self-defence still fights back.
            avoidGroupAggroTargets = true,
            -- Grind at the static spawner points around the novice's city (the spawn
            -- eggs placed by screenplays/spawner/*_city_spawners.lua) before taking
            -- missions: visit active ones in rotation, scan, move on; fall back to
            -- the bearing ring when a city has none. No location is hard-coded -
            -- whatever a spawner produces now is what the scan finds.
            roamCitySpawners = true,
            spawnerSearchRadiusMeters = 1000,
            skipInactiveSpawners = true,
            -- Close enough to a spawner for the scan to cover its pod (spawns land
            -- within ~10 m); final path legs failed 50-70 m short in live checks.
            spawnerArriveMeters = 60,
            -- A creature whose path failed is skipped this long, and up to this many
            -- are remembered at once, so several unreachable creatures around one
            -- spot are not reselected in turn (TRIP-verify 20260915-185828).
            unreachableSkipSeconds = 60,
            unreachableSkipMaxTargets = 8,
            assumedHitRate = 0.75,
            fleeHamFraction = 0.30,
            restHamFraction = 0.70,
            -- Default-off movement telemetry for early-game bodies: controller
            -- state, move requests, path generations and position on the
            -- dashboard earlyGame row. Hot-reloaded; no restart needed.
            movementTelemetry = false,
            -- Default-off combat telemetry for early-game bodies: HAM, the
            -- defender list (with the target-selection filter each one fails)
            -- and the self-defence decision on every tick, plus the captured
            -- killing blow at death. Dashboard earlyGame row `combat`; core3.log
            -- SimPveEarlyGameCombat / SimPveEarlyGameDeath. Hot-reloaded.
            combatTelemetry = false,
            -- Grind anchors avoid places a novice recently died: an anchor whose
            -- overland leg passes within this radius of one of the last
            -- deathAvoidMemory deaths (younger than deathAvoidSeconds) is
            -- skipped. 0 disables. Verification 20260914-173645: two deaths on
            -- one deterministic route.
            deathAvoidRadiusMeters = 150,
            deathAvoidMemory = 4,
            deathAvoidSeconds = 3600,
            -- Clone wounds (pveConfig combat.cloneWoundAmount) are sized for
            -- multi-thousand-HAM hunter bodies; 500 wounds pinned every novice
            -- pool at 1 HP (diagnosis 20260914-184412). A planned novice body caps
            -- them at this fraction of each pool's maximum. 0 heals every wound
            -- on death until medic bots can heal wounds at med centers. Clamped
            -- to at most 1 - restHamFraction so a wounded novice can finish resting.
            cloneWoundMaxFraction = 0,
            -- XP a hunting bot cannot earn from kills. A box funded by one of
            -- these neither blocks graduation nor keeps the template
            -- "incomplete" (F_0.9.8b). Owner 2026-10-09: medical and crafting,
            -- e.g. Medic 444x's science_medic_crafting_04 is paid in
            -- crafting_medicine_general.
            deferredXpTypes = {"medical", "crafting_medicine_general", "crafting_general"},
            -- F_0.9.8 mission graduation (owner decision 2026-09-22). A planned
            -- bot may take missions once it has trained box missionMinLineDepth
            -- in ONE line of a listed profession -- the 3xxx/4xxx shorthand, so
            -- 4 means e.g. combat_brawler_unarmed_04 or combat_marksman_rifle_04.
            -- A bot spread thin across lines (novice + unarmed_01 + 1hand_01)
            -- does NOT qualify. It still graduates on completing its whole plan,
            -- which is what the parity harness asserts. Below the threshold the
            -- bot keeps grinding and every mission order is declined. 0 disables
            -- the depth rule, leaving plan completion only.
            missionMinLineDepth = 4,
            missionProfessionPrefixes = { "combat_brawler", "combat_marksman" },
            -- Owner rule 2026-09-22: pack-aggro avoidance is a NOVICE crutch.
            -- It lifts once the bot is past a novice elite profession (one line
            -- box in any elite tree, e.g. "xx1x Teras Kasi") OR the bot is in a
            -- group. A capable bot SHOULD take a whole lair's aggro and spin it
            -- down -- that is the fastest XP and credits. eliteMinLineDepth = 0
            -- disables the lift, so avoidance would always apply.
            eliteMinLineDepth = 1,
            eliteProfessionPrefixes = {
                -- Brawler elites
                "combat_unarmed", "combat_1hsword", "combat_2hsword",
                "combat_polearm",
                -- Marksman elites
                "combat_rifleman", "combat_pistol", "combat_carbine",
                -- Hybrid / other combat elites
                "combat_bountyhunter", "combat_smuggler", "combat_commando",
            },
            weaponsByXpType = {
                combat_meleespecialize_onehand = "object/weapon/melee/knife/knife_stone.iff",
                combat_meleespecialize_twohand = "object/weapon/melee/axe/axe_heavy_duty.iff",
                combat_meleespecialize_polearm = "object/weapon/melee/polearm/lance_staff_wood_s1.iff",
                combat_meleespecialize_unarmed = "",
            },
        },
        trainingMaxPerTick = 4,
        trainingSweepBatch = 64,
        killXpRate = 1.0,
        flushIntervalSeconds = 60,
        trainingPlans = {
            {
                name = "brawler",
                goalSkill = "combat_unarmed_master",
                profession = "brawler",
                weaponTemplate = "",
                weaponType = "unarmed",
                -- Owner rule 2026-09-30 for Teras Kasi elite grinding. Mapped
                -- from what each box GRANTS, not its id, which misleads:
                --   xx4x Speed               combat_unarmed_ability  (unarmed_speed)
                --   x1xx Balance Conditioning combat_unarmed_speed    (combat_equillibrium)
                --   1xxx Meditation           combat_unarmed_accuracy (meditate)
                --   xxx1 combat-XP tree       combat_unarmed_support  (combat_general XP)
                linePriority = {
                    "combat_unarmed_ability",
                    "combat_unarmed_speed",
                    "combat_unarmed_accuracy",
                    "combat_unarmed_support",
                },
                -- The combat-XP tree spends combat_general, a different pool,
                -- so it is "trained separately" rather than waiting its turn.
                parallelXpStreams = true,
            },
            {
                name = "invalid_weapon_probe",
                goalSkill = "combat_brawler_novice",
                profession = "brawler",
                weaponTemplate = "",
                weaponType = "not_a_weapon_type",
            },
            {
                name = "pve_melee_medic",
                -- Explicit leaves for Brawler, Swordsman, Pikeman, Fencer
                -- 32xx, and Medic 2xxx. The manager derives the complete
                -- transitive union, so this remains a retunable Lua template.
                -- Branch identity for the three partials is confirmed from
                -- training.plans[].goalSkillDetails on the dashboard, which
                -- publishes each goal box's granted skillMods and commands.
                -- All four branches of a profession cost the same (measured:
                -- 47 pts at depth 3, 44 at depth 2 for 1hsword; 20 at depth 2
                -- for medic), so a wrong branch is a gameplay mismatch fixable
                -- here without a rebuild, never a skill-point budget overrun.
                goalSkills = {
                    "combat_brawler_master",
                    "combat_2hsword_master",
                    "combat_polearm_master",
                    -- Fencer 3xxx, intended: Expert Fencing Stances and Grips
                    -- (melee + ranged defense)
                    "combat_1hsword_accuracy_03",
                    -- Fencer x2xx, intended: Advanced Footwork
                    -- (knockdown defense)
                    "combat_1hsword_speed_02",
                    -- Medic 2xxx, intended: First Aid II (grants firstaid)
                    "science_medic_injury_02",
                },
                profession = "brawler",
                weaponTemplate = "",
                weaponType = "unarmed",
            },
            {
                name = "test_defer_medic",
                goalSkills = { "science_medic_injury_01" },
                profession = "brawler",
                weaponTemplate = "",
                weaponType = "unarmed",
            },
            {
                name = "test_onehand_weapon",
                goalSkills = { "combat_brawler_1handmelee_01" },
                profession = "brawler",
                weaponTemplate = "",
                weaponType = "onehandmelee",
            },
            {
                name = "test_graduation_tiny",
                goalSkills = {
                    "combat_brawler_1handmelee_01",
                    "science_medic_injury_01",
                },
                profession = "brawler",
                weaponTemplate = "",
                weaponType = "unarmed",
            },
        },
        reaper = {
            enabled = false,
            minAgeSeconds = 3600,
        },
    },

    -- P.8 Phase 2: persistent PvE identity roster, passed spike foundation,
    -- and the player-mimetic solo hunt loop.
    pveConfig = {
        enabled = true,
        enableHunterBots = true, -- spike foundation passed; Phase 2 live
        enableWorldPresence = true, -- P.8.0b: spike/hunters act as players to world spawns
        maxHunters = 6,
        -- "marksman" added with F_0.9.8 mission graduation: a marksman that
        -- reaches box 4 in a line qualifies for missions, and without this it
        -- would qualify and then never be dispatched.
        huntingProfessions = { "hunter", "brawler", "marksman" },
        novice = {
            enabled = false,
            -- Owner decision 2026-09-22: retire the legacy hunters and give the
            -- slots to novices. Retirement deletes one planless profession
            -- "hunter" identity per maintenance tick and the legacy capacity
            -- filler is skipped while novice mode is on, so the population
            -- converges to novices only. This DELETES those identities and
            -- their lifetime stats.
            retireLegacyHunters = false,
            bodyTemplate = "sim_playerbot_novice",
            -- HAM for every pool of a planned novice body (health/action/mind and
            -- their secondaries), like a starting player. 0 keeps the mobile
            -- template: 400-500 primaries, secondaries at 1/100 of that.
            bodyHam = 800,
            -- Where planned novices spawn and grind, overriding each identity's
            -- own home city (a city name from the shuttleport list below).
            -- Keren's shuttle pad sits on a navmesh island that cannot route to
            -- the city exits (verification 20260914), so novices start from Mos
            -- Eisley. Empty = use the identity's own home city.
            spawnPlanet = "tatooine",
            spawnCity = "mos_eisley",
            distribution = {
                -- Raised 1 -> 6 to fill maxHunters with novices as the legacy
                -- hunters retire; at count = 1 the roster would simply shrink.
                { profession = "brawler", trainingPlan = "brawler", count = 1 },
            },
        },
        skillTier = 1,
        maintenanceIntervalSeconds = 30,
        respawnDelaySeconds = 120,
        maxHuntDistanceMeters = 1500,
        announceCooldownSeconds = 90,
        announceSiteGapSeconds = 300,
        -- P.8.1c Phase 1: shared substitution-pool supply plumbing and
        -- per-family demand signals. Default-off until the Phase 2 matcher
        -- consumes these signals. This remains simulation-only.
        acquisitionLedger = {
            enabled = true,
            -- Miner creature-resource exclusion and hunter-only baseline
            -- provenance are latched on first applyPveConfig; changing this
            -- gate requires a process restart.
            minerCreatureResourceExclusion = true,
            creatureResourceClassMarkers = { "creature_resources" },
            creatureFamilies = { "meat", "hide", "bone" },
            familyReserveTargets = { meat = 2500, hide = 2500, bone = 2500 },
            familyReserveCap = 5000,
            familyAllocationCeilingUnits = { meat = 0, hide = 0, bone = 0 },
            -- Each creature family has its own reserve-driven demand signal.
            -- A full 1.0 fraction prevents the multi-family anti-domination
            -- fallback from starving any one family before its reserve is met.
            familyAllocationCeilingFraction = {
                meat = 1.0,
                hide = 1.0,
                bone = 1.0,
            },
            reservePressureFloor = 25,
            huntTimeEstimateSeconds = 600,
            baselineRetryBackoffMs = 5000,
        },
        -- Combat template (not artisan): artisan has NO combat skills/attacks, so
        -- the AI limped along on defaultattack and did ~15 dmg. death_watch_wraith
        -- is a high-level NEUTRAL Mandalorian mercenary -- socialGroup "death_watch",
        -- NOT a GCW faction, so no rebel/imperial "faction character" walking around
        -- -- with master rifleman/marksman attacks that match the equipped T-21
        -- rifle (so setupAttackMaps populates real ranged specials) plus large
        -- HAM/resists so it survives interceptors and kills reliably. The C++ spawn
        -- still overrides faction to 0 so it attacks / is attacked by wildlife.
        bodyTemplates = { "death_watch_wraith" },
        hunterLoadout = {
            -- Explicitly created and equipped by the C++ hunter spawn path.
            -- T-21 rifle: matches the wraith's rifleman/marksman attack maps.
            weaponTemplate = "object/weapon/ranged/rifle/rifle_t21.iff",
        },
        buffs = {
            {
                name = "pve_hunter_ham",
                crc = "medical_enhance_health",
                durationSeconds = 7200,
                type = 2, -- BuffType::MEDICAL
                attribute = 0, -- CreatureAttribute::HEALTH
                modifier = 2500, -- realistic doctor-tier enhance (was 100 = imperceptible)
            },
            {
                name = "pve_hunter_action",
                crc = "medical_enhance_action",
                durationSeconds = 7200,
                type = 2, -- BuffType::MEDICAL
                attribute = 3, -- CreatureAttribute::ACTION
                modifier = 2500, -- realistic doctor-tier enhance (was 100 = imperceptible)
            },
            {
                name = "pve_hunter_mind",
                crc = "performance_enhance_dance_mind",
                durationSeconds = 7200,
                type = 3, -- BuffType::PERFORMANCE
                attribute = 6, -- CreatureAttribute::MIND
                modifier = 2500, -- realistic entertainer-tier enhance (was 100 = imperceptible)
            },
        },
        -- P.8.6 Phase 1: real buff need detection and fallback configuration.
        -- The feature remains disabled until the provider interaction phases
        -- are live-verified. Keep this list separate from the legacy buffs
        -- table above so the gated-off synthetic behavior does not change.
        realBuffs = {
            enabled = true,
            fallbackToSynthetic = true,
            -- F_0.7.5: approach a provider in a DIFFERENT building by walking
            -- OUTDOORS to that building's exterior anchor first, then entering
            -- from the doorstep. Direct cross-building cell entry is broken
            -- where the two buildings are any real distance apart: measured
            -- 2026-08-31, Theed's doctor failed 22 of 23 attempts, and the
            -- harness reproduced it from a FRESH spawn at the same point
            -- (theed_doc_A FAIL) while the staged route from that identical
            -- point passed (theed_doc_D PASS). Those six isolation scenarios
            -- were removed from the matrix after the diagnosis; see
            -- docs/2-changelog for the run. Set false to restore the
            -- pre-F_0.7.5 direct entry.
            -- Compiled default is FALSE (SimPlayerManager.h); the deployment
            -- config turns it on. Only engages when structure traversal is also
            -- enabled -- see the gate on the approach path.
            crossBuildingStaging = true,
            reapplyThresholdSeconds = 900,
            providerScanRadiusMeters = 400,
            doctorProviderName = "Doctor Buffer",
            musicianProviderName = "Musician Buffer",
            dancerProviderName = "Dancer Buffer",
            entertainerTemplate = "entertainer",
            doctorTemplate = "smart_doctor_buffer",
            doctorInteractionTimeoutMs = 45000,
            entertainerDwellMs = 4000,
            providerApproachRangeMeters = 8,
            -- P.8.7 Phase 4: opt-in cross-planet real-buff hubs.
            buffHubs = {
                enabled = true,
                hubs = { "corellia:coronet", "tatooine:mos_eisley", "naboo:theed" },
                maxBuffTripsPerHunt = 1,
                buffTripDeadlineSeconds = 1800,
            },
            -- Keep these nine entries aligned with
            -- SimPlayerManager::getPveTrackedBuffCrcs: six medical buffs,
            -- dance mind, and music focus/willpower.
            fallbackBuffs = {
                {
                    name = "medical_enhance_health",
                    crc = "medical_enhance_health",
                    durationSeconds = 7200,
                    type = 2, -- BuffType::MEDICAL
                    attribute = 0, -- CreatureAttribute::HEALTH
                    modifier = 2500,
                },
                {
                    name = "medical_enhance_strength",
                    crc = "medical_enhance_strength",
                    durationSeconds = 7200,
                    type = 2, -- BuffType::MEDICAL
                    attribute = 1, -- CreatureAttribute::STRENGTH
                    modifier = 2500,
                },
                {
                    name = "medical_enhance_constitution",
                    crc = "medical_enhance_constitution",
                    durationSeconds = 7200,
                    type = 2, -- BuffType::MEDICAL
                    attribute = 2, -- CreatureAttribute::CONSTITUTION
                    modifier = 2500,
                },
                {
                    name = "medical_enhance_action",
                    crc = "medical_enhance_action",
                    durationSeconds = 7200,
                    type = 2, -- BuffType::MEDICAL
                    attribute = 3, -- CreatureAttribute::ACTION
                    modifier = 2500,
                },
                {
                    name = "medical_enhance_quickness",
                    crc = "medical_enhance_quickness",
                    durationSeconds = 7200,
                    type = 2, -- BuffType::MEDICAL
                    attribute = 4, -- CreatureAttribute::QUICKNESS
                    modifier = 2500,
                },
                {
                    name = "medical_enhance_stamina",
                    crc = "medical_enhance_stamina",
                    durationSeconds = 7200,
                    type = 2, -- BuffType::MEDICAL
                    attribute = 5, -- CreatureAttribute::STAMINA
                    modifier = 2500,
                },
                {
                    name = "performance_enhance_dance_mind",
                    crc = "performance_enhance_dance_mind",
                    durationSeconds = 7200,
                    type = 3, -- BuffType::PERFORMANCE
                    attribute = 6, -- CreatureAttribute::MIND
                    modifier = 2500,
                },
                {
                    name = "performance_enhance_music_focus",
                    crc = "performance_enhance_music_focus",
                    durationSeconds = 7200,
                    type = 3, -- BuffType::PERFORMANCE
                    attribute = 7, -- CreatureAttribute::FOCUS
                    modifier = 2500,
                },
                {
                    name = "performance_enhance_music_willpower",
                    crc = "performance_enhance_music_willpower",
                    durationSeconds = 7200,
                    type = 3, -- BuffType::PERFORMANCE
                    attribute = 8, -- CreatureAttribute::WILLPOWER
                    modifier = 2500,
                },
            },
        },
        combat = {
            retreatHamPct = 30,
            resumeHamPct = 95,
            maxRetreatCycles = 12,
            retreatRangeMeters = 40,
            -- Planned novices cap this per pool with
            -- playerBotProgression.earlyGame.cloneWoundMaxFraction.
            cloneWoundAmount = 500,
            huntActiveTickSeconds = 2,
            huntQuota = 1,
            huntTimeoutSeconds = 2400,
            scanRadiusMeters = 96,
            weaponRangeMeters = 48,
        },
        -- P.8.2 Phase 1 spawn + terminal plumbing. This is intentionally an
        -- independent gate from enableHunterBots; terminal discovery and
        -- transient lairs stay completely inert until explicitly enabled.
        missionHunt = {
            enabled = true,
            missionSpawnDistanceMeters = 200,
            terminalScanRadiusMeters = 600,
            maxSpawnPointTries = 32,
            maxSimultaneousAdds = 3,
            addsAbandonCycles = 8,
            -- Phase 0: safety bound for mission-order HUNTING only.
            missionHuntTimeoutSeconds = 600,
            terminalDwellSeconds = 5,
            terminalResolveWaitCycles = 10,
            -- Bound on consecutive NO_SPAWN_POINT lair-reveal deferrals for
            -- one offer (2s cadence, so 30 is ~60s). This counts ONLY the
            -- permanent case -- the advertised point is not spawn-permitted and
            -- all 32 relocation probes failed. Transient deferrals (lair
            -- capacity full, registration race) are never counted and reset the
            -- counter, because a lair slot can legitimately be held for
            -- lairTimeoutSeconds. Measured 2026-09-21: one bot spent an hour on
            -- 689 retries against one point before the 900s phase timeout
            -- released it. 0 restores the unbounded legacy retry.
            lairRevealDeferMaxCycles = 30,
            lairTimeoutSeconds = 3600,
            maxActiveLairs = 6,
            navmeshModeDebounceTicks = 2,
            navmeshRepathTries = 3,
            -- F_0.9.8: when the navmesh retry budget is exhausted and the
            -- destination is OUTDOORS, walk the leg overland instead of failing.
            -- Diagnosed at dantooine mining_outpost: recast returned a partial
            -- route across one NavArea (both polygons found, 3.81m of progress,
            -- 33.72m short) so a terminal 37.5m away was declared unreachable.
            -- Cell targets are never affected -- those must still use
            -- structure traversal.
            --
            -- ENABLED 2026-09-21 by owner decision, after live verification:
            -- terminal 7275419 went from 9 consecutive TERM_PATH_FAILED and 0
            -- arrivals to 3 bots arriving ~7s after resolve, and the mission
            -- loop then completed end to end (missionsCompleted 0 -> 5,
            -- abandons 0, abandonReasons {}). This is DELIBERATELY true and is
            -- not a leftover verification override; set false to restore the
            -- pre-fix behaviour, which abandons the mission instead.
            overlandFallbackOnNavmeshFailure = true,
        },
        -- Phase 2 real destroy-mission board. This is deliberately opt-in;
        -- the legacy terminal/lair contract remains the default path.
        missionBoard = {
            enabled = true,
            acceptedTerminalTypes = {"general"},
            maxHeldMissions = 2,
            sameDirectionArcDegrees = 60,
            baseDistanceMeters = 1000,
            difficultyDistanceFactor = 0,
            randomDistanceMeters = 1000,
            difficultyRandomDistance = 0,
            lairRevealRadiusMeters = 120,
            lairEngageAfterFieldClear = true,
            -- F_0.9.8 lair-engagement hold. A lair fight constantly pulls the
            -- bot's follow object onto whatever is hitting it, and the engage
            -- guard demanded BOTH the engagement record and the follow object,
            -- so it tore the engagement down and rebuilt it every tick --
            -- measured up to 136 times in one mission, where a bot that KILLS a
            -- lair does it once. Each teardown clears combat state and drops
            -- every defender, throwing away the bot's standing with the wave
            -- too. Holding the engagement and re-pointing at the lair on a
            -- bounded cadence stops that.
            --
            -- On since the v0.9.8 release, after the live runs. Set false to
            -- revert exactly, with no rebuild.
            lairEngagementHold = true,
            -- How often a held engagement re-points at the lair. Deliberately
            -- not every tick: that would replace a teardown storm with a
            -- retarget storm fighting the agent's own combat retargeting.
            lairRetargetIntervalMs = 5000,
            -- Break off the lair when a NEW WAVE spawns, clear it, then return.
            -- Attacking a lair past a wave threshold pulls the whole camp onto
            -- the attacker and they heal the lair -- observed in the log as
            -- condition damage RECOVERING (3840->1965, 2224->353) while the bot
            -- sat at 5-6% HAM with three attackers on it. The waves are also
            -- where the XP is: a winning mission currently kills only ~4.5 of
            -- ~12.6 spawned creatures.
            --
            -- Triggered by the spawnNumber TRANSITION, not by a mob being
            -- nearby; the latter fired every tick and was the thrash this
            -- replaces. On since the v0.9.8 release; set false to revert.
            waveDisengage = true,
            -- Shed the previous wave's aggro at each wave transition, so the
            -- bot faces one wave at a time instead of an accumulating stack.
            -- Deaths cluster where 5-6 lair members are on the bot, and the bot
            -- dies ABOVE its 30% retreat threshold -- it is burst damage inside
            -- the 2s tick, not attrition, so a lower threshold would not help.
            --
            -- Owner-scoped: mission lairs only (never krayts or other
            -- wilderness creatures), that lair's own members only, solo bots
            -- only (never while grouped with a player). Hard-capped at 2 per
            -- lair in code, because the lair cannot produce more than two wave
            -- transitions: waves fire at reveal, at the FIRST point of damage,
            -- and past half condition, then stop.
            waveDeaggro = true,
            -- F_0.9.8c travel combat (owner 2026-10-09). On the two walking legs
            -- of a mission, a bot whose lowest HAM pool falls below the retreat
            -- threshold while attacked runs TOWARD the mission (veering away
            -- from an attacker ahead, sideways near the lair), then rests to
            -- resumeHamPct before walking on. No cap on travel retreats. A
            -- fight that ends below travelRestHamPct also rests before walking
            -- on (0 disables that rest). Stand-and-fight is the base mover's
            -- existing in-combat hold; a ranged weapon with any
            -- <weapon>_hit_while_moving fights on the run instead.
            travelCombat = true,
            travelRestHamPct = 50,
            -- Flee legs a travelling bot may run per encounter before it turns
            -- and fights, to the death if need be (owner 2026-10-12: bouncing
            -- between running and resting until it dies looks worse than
            -- fighting). The count resets once the bot is clear and recovered.
            -- 0 = no limit.
            travelMaxFleeLegs = 1,
            -- F_0.9.8c attacker recognition. Core3's startCombat puts the bot on
            -- the attacking creature's defender list and the creature on the
            -- bot's threat map, never on the bot's own defender list -- which is
            -- all the bot's self-defense read, so a walking bot could be hit to
            -- death unseen. A threat-map creature that is in combat and aimed at
            -- the bot now counts as an attacker.
            attackerRecognition = true,
            -- F_0.9.8e lair retreat (owner 2026-10-11: "it would look weird if
            -- they just waited to die"). A bot retreating from a lair fight
            -- keeps running while something is still on it (up to 5 legs per
            -- retreat) and fights back if attacked while resting. Before, the
            -- base in-combat hold pinned it where it stood, its tick slept
            -- until an arrival that never came, and self-defense was off.
            lairRetreatDefense = true,
            maxOfferAgeSeconds = 1800,
            -- On reveal, if the advertised wilderness point is no longer
            -- spawn-permitted (a no-build object appeared since the offer was
            -- generated), relocate the lair to the nearest clear point around
            -- the waypoint instead of abandoning the mission. false = legacy
            -- strict behaviour (abandon + terminal round trip).
            revealRelocateEnabled = true,
            -- Place mission lairs the way the stock game does (owner
            -- 2026-10-13). The terminal only needs a waypoint that is in
            -- bounds, dry and outside a city, drawn inside the arc; the exact
            -- lair point is found on arrival by a random search around the
            -- waypoint that widens every 32 tries (256 tries, capped at 512m).
            -- false = the strict terminal check plus the 64m probe ring.
            stockPlacement = true,
            -- Terminal offer-generation retry. On a partial (1-of-2) or empty
            -- board the hunter dwells offerRetrySeconds at the terminal and
            -- re-generates, up to offerMaxAttempts times, before accepting a
            -- single offer (or abandoning if still empty). Fixes the dominant
            -- mission_offers_unavailable churn where a valid offer was discarded
            -- by the old all-or-nothing 2-of-2 rule.
            offerRetrySeconds = 25,
            offerMaxAttempts = 3,
        },
        -- Phase 1 dispatch gate. The legacy home-planet/home-city rules stay
        -- byte-for-byte active until this opt-in is enabled.
        dispatch = {
            locationBasedEligibility = true,
            dispatchRadiusMeters = 2500,
        },
        -- P.8.7 Phase 3 market dispatch. Families are intentionally limited
        -- to the three resource paths recordPveHunterHarvest understands.
        marketDispatch = {
            enabled = true,
            families = { "meat", "hide", "bone" },
            qualityWeights = {
                meat = { OQ = 1.0 },
                hide = { OQ = 0.6, SR = 0.4 },
                bone = { OQ = 0.6, SR = 0.4 },
            },
            minQualityScore = 0,
            maxConcurrentRelocations = 1,
            minHuntersPerActivePlanet = 1,
            -- A relocation that never concludes used to hold the single slot
            -- above forever, which stops EVERY other hunter being dispatched:
            -- startedAtMs was recorded and shown on the dashboard but never
            -- read. Measured 2026-09-24 -- identity 851 sat at leg 0 of 1 for
            -- 51.7 minutes while both mission bots idled at the Mos Eisley
            -- spawn point. The reaper concludes an over-age relocation through
            -- the same failure path a refused departure already takes.
            -- 0 disables it (pre-fix behaviour); values below 60 are raised to
            -- 60 so a typo cannot cancel healthy legs mid-flight. Matches the
            -- existing buffTripDeadlineSeconds safety net.
            relocationDeadlineSeconds = 1800,
            relocationCooldownSeconds = 1800,
            relocationMinDwellSeconds = 900,
            -- F_0.9.8b progression-first dispatch (owner rule 2026-10-09). A bot
            -- whose training template -- its WHOLE plan, every profession in it
            -- -- is incomplete is sent to the lair it will actually fight (no
            -- PACK lair while it is solo and without an elite box, the same rule
            -- combat applies), ranked for XP at the tier of the weapon it is
            -- grinding, with market demand only breaking ties. A
            -- template-complete bot keeps the resource dispatch above.
            progressionDispatch = {
                enabled = true,
                -- Nothing fightable on any planet: grind for this many minutes
                -- (clamped 5..240) instead of idling, then dispatch again.
                grindFallback = true,
                grindFallbackMinutes = 30,
            },
        },
        -- Each ground is a real SPAWNAREA from the planet region scripts;
        -- C++ validates the object, terrain/boundary, and P.4.1 guards once
        -- per config publication before a row becomes usable.
        species = {
            {
                key = "tatooine_womprat_meat",
                planet = "tatooine",
                spawnArea = "@tatooine_region_names:mos_eisley_easy_newbie",
                huntGround = { 4200, -4784, 0 },
                lairTemplate = "tatooine_womprat_lair_neutral_small",
                missionDifficulty = 1,
                lairBuildingLevel = 1,
                lairSize = 20,
                templateFilter = "womp_rat",
                requestedResourceType = "meat_wild",
                harvestKind = "meat",
                estimatedMeatUnits = 25,
                soloable = true,
                minSkillTier = 1,
                eligibleHomeCities = { "mos_eisley" },
            },
            {
                key = "tatooine_kreetle_meat",
                planet = "tatooine",
                spawnArea = "@tatooine_region_names:bestine_medium_newbie",
                huntGround = { -1216, -3660, 0 },
                lairTemplate = "tatooine_kreetle_lair_neutral_small",
                missionDifficulty = 1,
                lairBuildingLevel = 1,
                lairSize = 20,
                templateFilter = "kreetle",
                requestedResourceType = "meat_insect",
                harvestKind = "meat",
                estimatedMeatUnits = 20,
                soloable = true,
                minSkillTier = 1,
                eligibleHomeCities = { "bestine" },
            },
            -- Group-tier rows are data for P.8.2. The v1 matchmaker ignores
            -- them, but keeping the species/resource contract visible now
            -- lets the next controller add group formation without changing
            -- the config schema.
            {
                key = "tatooine_krayt_group",
                planet = "tatooine",
                spawnArea = "@tatooine_region_names:hard_krayt_ne",
                huntGround = { 6515, 4215, 0 },
                lairTemplate = "tatooine_krayt_lair_neutral_small",
                missionDifficulty = 3,
                lairBuildingLevel = 2,
                lairSize = 20,
                templateFilter = "krayt",
                requestedResourceType = "meat_wild",
                harvestKind = "meat",
                estimatedMeatUnits = 120,
                soloable = false,
                minSkillTier = 3,
                eligibleHomeCities = { "bestine" },
            },
            {
                key = "yavin4_acklay_group",
                planet = "yavin4",
                spawnArea = "@yavin4_region_names:yavin4_geo_bunker_nobuild",
                huntGround = { -6488, -417, 0 },
                lairTemplate = "yavin4_acklay_lair_neutral_small",
                missionDifficulty = 5,
                lairBuildingLevel = 3,
                lairSize = 20,
                templateFilter = "acklay",
                requestedResourceType = "meat_wild",
                harvestKind = "meat",
                estimatedMeatUnits = 150,
                soloable = false,
                minSkillTier = 5,
                eligibleHomeCities = { "theed" },
            },
            {
                key = "yavin4_geonosian_group",
                planet = "yavin4",
                spawnArea = "@yavin4_region_names:yavin4_geo_bunker_nobuild",
                huntGround = { -6488, -417, 0 },
                lairTemplate = "yavin4_geonosian_lair_neutral_small",
                missionDifficulty = 3,
                lairBuildingLevel = 2,
                lairSize = 20,
                templateFilter = "geonosian",
                requestedResourceType = "meat_insect",
                harvestKind = "meat",
                estimatedMeatUnits = 40,
                soloable = false,
                minSkillTier = 3,
                eligibleHomeCities = { "theed" },
            },
            {
                key = "dathomir_nightsister_group",
                planet = "dathomir",
                spawnArea = "@dathomir_region_names:nightsister_clan",
                huntGround = { -4069, -184, 0 },
                lairTemplate = "dathomir_nightsister_lair_neutral_small",
                missionDifficulty = 4,
                lairBuildingLevel = 2,
                lairSize = 20,
                templateFilter = "nightsister",
                requestedResourceType = "meat_wild",
                harvestKind = "meat",
                estimatedMeatUnits = 50,
                soloable = false,
                minSkillTier = 4,
                eligibleHomeCities = { "moenia" },
            },
        },
        identityRoster = {
            maxHunters = 6,
            skillTier = 1,
            flushIntervalSeconds = 60,
        },
        spike = {
            enabled = true,
            worldWaitTimeoutSeconds = 300,
            combatTimeoutSeconds = 180,
            -- 250 covers the lair spawn range (CLOSEOBJECTRANGE 192m) plus
            -- the creatures' spread around their lair; 96 missed them.
            scanRadiusMeters = 250,
            -- The spike only needs to target + engage a wild creature (its
            -- redefined PASS boundary); dealing damage is Phase 2. Any NEUTRAL
            -- template works. Phase 2 hunter bodies use the same functional
            -- artisan appearance with an explicitly-equipped rifle.
            hunterTemplate = "artisan",
            -- Empty = ANY attackable wild creature proves the kill path. The
            -- old "womprat" filter never matched western_dune_sea_2's
            -- hard_dune_sea spawn group. Phase 2's per-species hunts filter;
            -- the spike only needs to prove attackability.
            targetTemplateFilter = "",
            -- This is a real main-planet SPAWNAREA from
            -- managers/planet/tatooine_regions.lua. NOTE: use the large
            -- western_dune_sea_2 region with an EXPLICIT x/y - medium_womprats'
            -- center is entirely swallowed by the Lars Homestead NOSPAWN zone
            -- (proven live: all rolls rejected sub=cityOrNoSpawn). The point
            -- below is open desert, verified clear of every nospawn/city/
            -- nobuild region and inside the dune-sea spawn circle.
            spawnArea = {
                planet = "tatooine",
                name = "@tatooine_region_names:western_dune_sea_2",
                x = -1500,
                y = -6500,
            },
        },
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
        -- P.6.6 controller-driven engagement is deliberately opt-in. The
        -- controller owns only combat approach/engage movement while this
        -- gate is true; all existing PvP behavior remains unchanged by
        -- default.
        combat = {
            controllerDrivenEngage = false,
            -- P.6.6b squad combat contagion is independently gated. Keep it
            -- off until the owner enables the live convergence soak.
            squadAggroSharing = false,
            squadAggroConvergeRadiusMeters = 300,
            squadAggroConvergeTimeoutMillis = 60000, -- ~300m at run speed; a
                                                     -- far squadmate needs time
            squadAggroTargetTtlSeconds = 8,
            squadAggroFailedTargetIgnoreSeconds = 10,
            approachRadiusMeters = 100,
            reapproachHysteresisMeters = 8,
            arrivalToleranceMeters = 4,
            approachTimeoutMillis = 15000,
            losGateDamage = true,
            allowInCellEngage = true,
            combatTickMillis = 500,
            fallbackWeaponRangeMeters = 64,
            logCombatMovement = false,
        },
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
            -- P.6.2b follow-up (P.6.5e): break the Theed doorway/collector
            -- stalemate when live bots make no HAM progress. Set to 0 to
            -- disable; any non-zero value is clamped to the 15s floor.
            stalemateBreakSeconds = 45,
            stalemateGraceSeconds = 20,
        },
        -- P.6.2 scouts + gank convergence: small scout squads run the same
        -- city loop but REPORT enemy contacts instead of engaging; the
        -- nearest eligible patrol squad of that faction breaks off, runs to
        -- its shuttle, and travels to the contact's city. Any squad that
        -- engages a real PLAYER also calls it in. Cooldowns per squad and
        -- per city stop ping-pong; contacts expire on their own.
        scouts = {
            enabled = true,
            squadsPerFaction = 2,
            squadSize = 1,              -- lone scout (players use 1-2)
            scanRadiusMeters = 128,      -- scouts watch a wider bubble
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
            collectorJitterMeters = 3,
            -- P.6.5d: city-loop hangouts default to the nearest validated
            -- cantina exterior; Theed keeps its verified hand-placed spot.
            useCantinaHangouts = true,
            cantinaScanRadiusMeters = 400,
            -- F_0.7.3: the hospital search gets its own, wider radius and is
            -- run from BOTH the shuttle pad and the cantina/hangout. 400m is
            -- measured from the pad and does not span a large city -- Theed's
            -- pad is 561m from its own cantina, so no hospital was ever in
            -- range and every Theed hunter fell back to synthetic doctor
            -- buffs. Set a city's medCenter = {x, y, z} with
            -- medCenterManual = true to pin the anchor outright.
            medCenterScanRadiusMeters = 900,
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
    -- derives exterior cantina hangouts unless hangoutManual is true, and an
    -- exterior med-center anchor unless medCenterManual is true (which makes
    -- a city's medCenter = {x, y, z} authoritative).
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
        dantooine = {
            { name = "mining_outpost", spawn = {-637.0, 2504.4, 3.0}, hangout = {-637.0, 2504.4, 3.0}, starport = "Dantooine Mining Outpost", routingOnly = true },
        },
        lok = {
            { name = "nym_stronghold", spawn = {478.9, 5512.0, 9.0}, hangout = {478.9, 5512.0, 9.0}, starport = "Nym's Stronghold", routingOnly = true },
        },
        dathomir = {
            { name = "trade_outpost", spawn = {618.9, 3092.0, 6.0}, hangout = {618.9, 3092.0, 6.0}, starport = "Trade Outpost", routingOnly = true },
        },
        endor = {
            { name = "smuggler_outpost", spawn = {-950.6, 1553.4, 73.0}, hangout = {-950.6, 1553.4, 73.0}, starport = "Smuggler Outpost", routingOnly = true },
        },
        talus = {
            { name = "dearic", spawn = {263.6, -2952.1, 6.0}, hangout = {263.6, -2952.1, 6.0}, starport = "Dearic Starport", routingOnly = true },
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
