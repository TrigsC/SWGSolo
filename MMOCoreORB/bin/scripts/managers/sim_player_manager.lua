-- scripts/managers/sim_player_manager.lua

--print("LUA DEBUG: SimPlayerManagerConfig Loaded")
SimPlayerManagerConfig = {
    -- MASTER SWITCH
    enabled = true,

    -- Read-only resource intelligence observability. Disabled by default.
    resourceIntelligenceConfig = {
        enabled = false,
        logTopResources = false,
        summaryIntervalSeconds = 600,
        topN = 10,
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
                logStateTransitions = true,
                yieldConfig = {
                    enabled = true,
                    minAmount = 5,
                    maxAmount = 25,
                    logYield = true,
                },
                summaryConfig = {
                    enabled = false,
                    intervalSeconds = 300,
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
