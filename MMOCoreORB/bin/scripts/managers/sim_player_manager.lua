-- scripts/managers/sim_player_manager.lua

print("LUA DEBUG: SimPlayerManagerConfig Loaded")
SimPlayerManagerConfig = {
    -- MASTER SWITCH
    enabled = true,

    -- 1. LOCATIONS
    shuttleports = {
        corellia = {
            { name = "coronet", spawn = {-328.0, -4600.0, 28.0}, hangout = {-155.0, -4722.0, 28.0} },
        }
    }, -- <--- This comma is CRITICAL

    -- 2. SPAWN RULES
    spawnGroups = {
        {
            type = "pvp_solo",
            totalCount = 5,
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