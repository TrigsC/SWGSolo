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

    -- Simulation-only route validation for D.6.6-aligned density coordinates.
    minerPathValidationSimulationConfig = {
        enabled = true,
        intervalSeconds = 60,
        validateOnlyAcceptedDensityTargets = true,
        maxPathDistance = 2500,
        maxPathNodes = 256,
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
        maxActiveMiners = 2,
        requireDemandWeightedPlan = true,
        requireAcceptedDensityTarget = true,
        requireValidPath = true,
        fallbackToConceptualLoop = true,
        rollbackOnFailureCount = 3,
        logDecisionSummary = true,
        -- Full per-miner switch lines are otherwise emitted only for useful
        -- transitions/failures/activation-capable decisions.
        logVerboseSwitchDecisions = false,
        assignmentConfig = {
            enabled = true,
            -- Retained target lifetime. With requireValidPath=true, C++ clamps
            -- this high enough for density/path validation to catch up.
            ttlSeconds = 180,
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
        },
        limitedActivationConfig = {
            enabled = true,
            -- Number of miners currently queued, moving, or sampling through
            -- the intelligent assignment path.
            maxActiveIntelligentMiners = 2,
            -- Number of new intelligent activations accepted in one manager interval.
            maxActivationsPerInterval = 1,
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
                desiredReserve = 5000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            master_weaponsmith_staples = {
                enabled = true,
                desiredReserve = 3000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            high_damage_weapon_components = {
                enabled = true,
                desiredReserve = 3000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            chef_buff_foods = {
                enabled = true,
                desiredReserve = 5000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            chef_high_value_consumables = {
                enabled = true,
                desiredReserve = 3000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
            production_infrastructure = {
                enabled = true,
                desiredReserve = 10000,
                lowStockThreshold = 0.35,
                criticalStockThreshold = 0.10,
            },
        },
    },

    -- Read-only observation of public resource listings on bazaars and player vendors.
    marketSupplyObservationConfig = {
        enabled = true,
        intervalSeconds = 300,
        maxListingsScanned = 5000,
        includeBazaar = true,
        includePlayerVendors = true,
        includeVendorStockrooms = true,
        includePlayerInventory = false,
        includePrivateContainers = false,
        minQuantity = 1,
        logTopN = 5,
    },

    -- Memory-only stockpile-shaped diagnostics. Never imports market supply or persists rows.
    stockpileSnapshotSimulationConfig = {
        enabled = true,
        intervalSeconds = 300,
        logTopN = 10,
        includeConceptualMinerTotals = true,
        includeMarketObservation = true,
    },

    -- Persist aggregate conceptual miner totals only. Disabled by default.
    aiEconomyPersistenceConfig = {
        persistConceptualMinerTotals = false,
        intervalSeconds = 300,
        logSummary = true,
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

    -- 1. LOCATIONS
    shuttleports = {
        naboo = {
            { name = "moenia", spawn = {4963.0, -4892.0, 3.0}, hangout = {4807.0, -4700.0, 4.0} },
            { name = "theed", spawn = {-5410, 4325.0, 6.0}, hangout = {-4880.0, 4140.0, 6.0} },
        },
        corellia = {
            { name = "coronet", spawn = {-328.0, -4600.0, 28.0}, hangout = {-155.0, -4722.0, 28.0} },
        },
        tatooine = {
            { name = "mos_eisley", spawn = {3416.0, -4645.0, 5.0}, hangout = {3467.0, -4890.0, 5.0} },
        }
    },

    -- 2. SPAWN RULES
    spawnGroups = {
        -- MINERS: Spread randomly across ALL defined shuttleports
        {
            type = "miner",
            totalCount = 4,
            templates = { "light_jedi_sentinel", "artisan" }, -- Randomly picks appearance
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
        },
        {
            type = "pvp_solo",
            totalCount = 0,
            templates = { "rebel_trooper", "stormtrooper" },
            minStaySeconds = 60,
            maxStaySeconds = 180
        }
    }
}

-- DEBUG CHECK: Verifies the table exists before passing to C++
--print("LUA DEBUG: SimPlayerManagerConfig.spawnGroups type=", type(SimPlayerManagerConfig.spawnGroups))

-- return SimPlayerManagerConfig
