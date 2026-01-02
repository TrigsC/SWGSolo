-- scripts/managers/sim_player_manager.lua

print("LUA DEBUG: SimPlayerManagerConfig Loaded")
SimPlayerManagerConfig = {
    -- MASTER SWITCH
    enabled = true,

    -- 1. LOCATIONS
    shuttleports = {
        naboo = {
            { name = "moenia", spawn = {4963.0, -4892.0, 3.0}, hangout = {4807.0, -4700.0, 4.0} },
        },
        corellia = {
            { name = "coronet", spawn = {-328.0, -4600.0, 28.0}, hangout = {-155.0, -4722.0, 28.0} },
        }
    },

    -- 2. SPAWN RULES
    spawnGroups = {
        -- MINERS: Spread randomly across ALL defined shuttleports
        {
            type = "miner",
            totalCount = 1, 
            templates = { "light_jedi_sentinel", "artisan" }, -- Randomly picks appearance
            behavior = "gather_resources"
        },
        {
            type = "pvp_solo",
            totalCount = 1,
            templates = { "rebel_commando", "rebel_trooper" }
        }
    }
}

-- DEBUG CHECK: Verifies the table exists before passing to C++
if SimPlayerManagerConfig.spawnGroups then
    print("LUA DEBUG: spawnGroups table exists in Lua.")
else
    print("LUA DEBUG: spawnGroups table is MISSING in Lua.")
end

return SimPlayerManagerConfig