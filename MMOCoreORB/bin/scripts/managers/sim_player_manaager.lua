-- scripts/managers/sim_player_manager.lua

local SimPlayerManagerConfig = {
    -- MASTER SWITCH
    enabled = true,

    -- ---------------------------------------------------------
    -- 1. LOCATIONS DATABASE
    -- Define all valid spawn points here. The C++ code will pick from these.
    -- Format: { x, z, y } (Note: Lua uses Z for height usually in other engines, 
    -- but SWGEmu C++ expects X, Z(North), Y(Height). 
    -- Let's stick to standard map coords: X, Y (North), Z (Height) to match /loc 
    -- and we will swap them in C++ if needed, OR just use explicit keys.
    -- ---------------------------------------------------------
    shuttleports = {
        naboo = {
            { name = "moenia", spawn = {4961.0, -4892.0, 3.0}, hangout = {4807.0, -4700.0, 4.0} },
            { name = "theed",  spawn = {-5411.0, 4321.0, 6.0}, hangout = {-4873.0, 4149.0, 6.0} },
            { name = "kaadara", spawn = {5123.0, 6615.0, -192.0}, hangout = {5202.0, 6684.0, -192.0} },
        },
        tatooine = {
            { name = "mos_eisley", spawn = {3417.0, -4646.0, 5.0}, hangout = {3468.0, -4881.0, 5.0} },
            --{ name = "mos_espa",   spawn = {-2900.0, 2200.0, 5.0}, hangout = {-2800.0, 2100.0, 5.0} },
        },
        corellia = {
            { name = "coronet", spawn = {-328.0, -4600.0, 28.0}, hangout = {-155.0, -4722.0, 28.0} },
        }
    },

    -- ---------------------------------------------------------
    -- 2. SPAWN RULES
    -- Define who spawns and how they behave.
    -- ---------------------------------------------------------
    spawnGroups = {
        
        -- MINERS: Spread randomly across ALL defined shuttleports
        {
            type = "miner",
            totalCount = 0, 
            templates = { "light_jedi_sentinel", "artisan", "noble", "architect" }, -- Randomly picks appearance
            behavior = "gather_resources"
        },

        -- SOLO PVP: Single hunters moving between spots
        {
            type = "pvp_solo",
            totalCount = 5,
            templates = { "stormtrooper" },  --{ "stormtrooper", "rebel_trooper" },
            faction = "imperial", -- or "imperial", "rebel" ," random"
            behavior = "roam_solo"
        },

        -- SQUAD PVP: Groups of 3 that stick together
        {
            type = "pvp_squad",
            totalCount = 3, -- This means 3 SQUADS (not 3 players)
            squadSize = 3,  -- 3 players per squad
            templates = { "stormtrooper" }, 
            faction = "imperial", -- or "imperial", "rebel" ," random"
            behavior = "roam_squad"
        }
    }
}

return SimPlayerManagerConfig