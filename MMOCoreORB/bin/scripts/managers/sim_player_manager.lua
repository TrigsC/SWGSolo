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
                desiredReserve = 500000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            master_weaponsmith_staples = {
                enabled = true,
                desiredReserve = 500000,
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
                desiredReserve = 500000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            chef_high_value_consumables = {
                enabled = true,
                desiredReserve = 500000,
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
        startupDelaySeconds = 60,
        scenarios = {
            {name="store_boot_invariant", budgetMs=300000, steps={
                {op="assertStore", records=-1, orphanRecords=0, rosterWithoutRecord=0}}},
            {name="harness_identity_minted_with_record", budgetMs=300000, steps={
                {op="assertXp", identityIndex=0, xpType="harness_zero", expect="0"},
                {op="assertXp", identityIndex=1, xpType="harness_zero", expect="0"},
                {op="assertCredits", identityIndex=0, bank=true, expect="0"},
                {op="assertCredits", identityIndex=1, bank=true, expect="0"}}},
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
                {op="recordSkill", identityIndex=0, xpType="combat_marksman_novice"},
                {op="recordSkill", identityIndex=0, xpType="combat_marksman_novice"},
                {op="assertSkill", identityIndex=0, xpType="combat_marksman_novice"}}},
            {name="body_destroy_respawn_keeps_scalars", budgetMs=300000, steps={
                {op="destroyBody", identityIndex=0},
                {op="respawnBody", identityIndex=0},
                {op="assertXp", identityIndex=0, xpType="combat_rangedspecialize_rifle", expect="250"},
                {op="assertCredits", identityIndex=0, bank=true, expect="600"}}},
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
                {op="grantXp", identityIndex=1, xpType="harness_restart", amount=4242, restartPhase="A"},
                {op="grantCredits", identityIndex=1, bank=true, amount=777, restartPhase="A"},
                {op="writeRestartProbe", identityIndex=1, restartPhase="A"},
                {op="flushNow", force=true, restartPhase="A"},
                {op="assertXp", identityIndex=0, xpType="harness_restart", expect="4242", restartPhase="B"},
                {op="assertCredits", identityIndex=0, bank=true, expect="777", restartPhase="B"},
                {op="deleteIdentity", identityIndex=0, restartPhase="B"},
                {op="runReaper", force=true, restartPhase="B"}}},
            {name="orphan_counted_not_reaped_gate_off", budgetMs=300000, steps={
                {op="injectOrphan"},
                {op="assertCounterDelta", counter="orphanRecords", expect="1"},
                {op="runReaper", force=false},
                {op="assertCounterDelta", counter="orphanRecords", expect="1"}}},
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
                {op="mintIdentity"},
                {op="deleteIdentity", identityIndex=2},
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
                {op="mintIdentity"},
                {op="deleteProgressionRow", identityIndex=3},
                {op="assertStore", orphanRecords=0, rosterWithoutRecord=1},
                {op="flushNow", force=true},
                {op="assertStore", records=-1, orphanRecords=0, rosterWithoutRecord=0},
                {op="assertXp", identityIndex=3, xpType="harness_partial", expect="0"},
                {op="reloadStore"},
                {op="assertXp", identityIndex=3, xpType="harness_partial", expect="0"},
                {op="assertCounterDelta", counter="createRefusedNotInRoster", expect="0"},
                {op="deleteIdentity", identityIndex=3},
                {op="runReaper", force=true}}},
        },
    },

    -- P.10a: manager-owned progression ledger. Capability gates added by
    -- later chunks belong in this block: awardKillXp, awardMissionCredits,
    -- lootEnabled, groupLoot, trainingEnabled, bazaarSell, and bazaarBuy.
    -- The foundation is intentionally inert until its live receipt is accepted.
    playerBotProgression = {
        enabled = false,
        flushIntervalSeconds = 60,
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
            resumeHamPct = 70,
            maxRetreatCycles = 3,
            retreatRangeMeters = 40,
            cloneWoundAmount = 500,
            huntActiveTickSeconds = 2,
            huntQuota = 1,
            huntTimeoutSeconds = 1800,
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
            terminalDwellSeconds = 5,
            terminalResolveWaitCycles = 10,
            lairTimeoutSeconds = 1800,
            maxActiveLairs = 6,
            navmeshModeDebounceTicks = 2,
            navmeshRepathTries = 3,
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
            maxOfferAgeSeconds = 1800,
            -- On reveal, if the advertised wilderness point is no longer
            -- spawn-permitted (a no-build object appeared since the offer was
            -- generated), relocate the lair to the nearest clear point around
            -- the waypoint instead of abandoning the mission. false = legacy
            -- strict behaviour (abandon + terminal round trip).
            revealRelocateEnabled = true,
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
            relocationCooldownSeconds = 1800,
            relocationMinDwellSeconds = 900,
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
