-- scripts/managers/sim_player_manager.lua

--print("LUA DEBUG: SimPlayerManagerConfig Loaded")
SimPlayerManagerConfig = {
    -- MASTER SWITCH
    enabled = true,

    -- Read-only resource intelligence observability. Disabled by default.
    resourceIntelligenceConfig = {
        enabled = false,
        logTopResources = false,
        summaryIntervalSeconds = 60,
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

    -- Log-only miner target recommendations. Disabled by default and does not change miner behavior.
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

    -- Simulation-only single target plans. Disabled by default and never changes miner state.
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

    -- Simulation-only density pocket search for the current D.4 plan.
    minerDensityTargetSimulationConfig = {
        enabled = false,
        intervalSeconds = 30,
        searchRadii = { 250, 500, 1000, 2000 },
        samplesPerRadius = 48,
        minAcceptableDensity = 0.65,
        preferredDensity = 0.80,
        requireNavmesh = false,
        maxPathCheckAttempts = 8,
        distancePenaltyPerMeter = 0.02,
    },

    -- Simulation-only route validation for D.5-prep density coordinates.
    minerPathValidationSimulationConfig = {
        enabled = false,
        intervalSeconds = 30,
        validateOnlyAcceptedDensityTargets = false,
        maxPathDistance = 2500,
        maxPathNodes = 256,
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
