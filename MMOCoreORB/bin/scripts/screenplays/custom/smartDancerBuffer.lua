-- MMOCoreORB/bin/scripts/screenplays/custom/smartDancerBuffer.lua
--
-- Minimal Dancer Buffer NPC:
--  - Spawns an entertainer NPC
--  - Forces it to dance
--  - When a player "watches" it, apply performance mind buff instantly

local ObjectManager = require("managers.object.object_manager")

SmartDancerConfig = SmartDancerConfig or {}

SmartDancerConfig.spawn_points = SmartDancerConfig.spawn_points or {
    -- Coronet
    {
        planet = "corellia",
        x = 19.73,
        z = -0.89,
        y = 4.67,
        heading = 90,
        cell = 8105496,
        customName = "Dancer Buffer"
    },
    -- Moenia
    {
        planet = "naboo",
        x = 19.41,
        z = -0.89,
        y = -1.20,
        heading = 90,
        cell = 111,
        customName = "Dancer Buffer"
    },
    -- Theed
    {
        planet = "naboo",
        x = 19.88,
        z = -0.89,
        y = 4.21,
        heading = 90,
        cell = 91,
        customName = "Dancer Buffer"
    },
    -- mos eisly
    {
        planet = "tatooine",
        x = 18.36,
        z = -0.89,
        y = 2.53,
        heading = 90,
        cell = 1082877,
        customName = "Dancer Buffer"
    }
}

-- Pick a safe/default dance name that exists in your PerformanceManager tables
SmartDancerConfig.danceName = SmartDancerConfig.danceName or "basic"

-- Tune these to whatever you want for your server
SmartDancerConfig.buffAmount = SmartDancerConfig.buffAmount or 1000
SmartDancerConfig.buffDuration = SmartDancerConfig.buffDuration or 3600 -- seconds

SmartDancerConfig.maxRange = SmartDancerConfig.maxRange or 10
SmartDancerConfig.keepDancingHeartbeatMs = SmartDancerConfig.keepDancingHeartbeatMs or 15000

local function safeSetCustomObjectName(pObj, name)
    if pObj == nil or name == nil or name == "" then return end
    -- IMPORTANT: no boolean arg (your doctor buffer notes this can crash some builds)
    SceneObject(pObj):setCustomObjectName(name)
end

local function inRange(pNpc, pPlayer)
    if pNpc == nil or pPlayer == nil then return false end
    if SceneObject(pNpc).isInRangeWithObject ~= nil then
        return SceneObject(pNpc):isInRangeWithObject(pPlayer, SmartDancerConfig.maxRange)
    end
    local dist = SceneObject(pNpc):getDistanceTo(pPlayer)
    return dist ~= nil and dist <= SmartDancerConfig.maxRange
end

SmartDancerBuffer = SmartDancerBuffer or ScreenPlay:new { numberOfActs = 1 }
registerScreenPlay("SmartDancerBuffer", true)

function SmartDancerBuffer:start()
    for i, sp in ipairs(SmartDancerConfig.spawn_points) do
        if sp.planet ~= nil and isZoneEnabled(sp.planet) then
            local pMob = spawnMobile(
                sp.planet,
                "entertainer",
                0,
                sp.x or 0, sp.z or 0, sp.y or 0,
                sp.heading or 0,
                sp.cell or 0
            )

            if pMob ~= nil then
                safeSetCustomObjectName(pMob, sp.customName or "Dancer Buffer")

                -- Start dancing
                local agent = LuaAiAgent(pMob)
                if agent ~= nil and agent.startDancingByName ~= nil then
                    agent:startDancingByName(SmartDancerConfig.danceName)
                end

                -- Watch observer: when players watch this NPC, buff them
                createObserver(WASWATCHED, "SmartDancerBuffer", "notifyWatched", pMob, 1)

                -- Keep dancing in case something interrupts it
                createEvent(SmartDancerConfig.keepDancingHeartbeatMs, "SmartDancerBuffer", "keepDancing", pMob, "")
            end
        end
    end
end

function SmartDancerBuffer:keepDancing(pMob)
    if pMob == nil then return end
    if not SceneObject(pMob):isCreatureObject() then return end

    local c = CreatureObject(pMob)
    if c == nil then return end

    if not c:isDancing() then
        local agent = LuaAiAgent(pMob)
        if agent ~= nil and agent.startDancingByName ~= nil then
            agent:startDancingByName(SmartDancerConfig.danceName)
        end
    end

    createEvent(SmartDancerConfig.keepDancingHeartbeatMs, "SmartDancerBuffer", "keepDancing", pMob, "")
end

function SmartDancerBuffer:notifyWatched(pNpc, pWatcher)
    if pNpc == nil or pWatcher == nil then return 0 end
    if not SceneObject(pWatcher):isPlayerCreature() then return 0 end

    -- optional: only buff if they’re nearby
    if not inRange(pNpc, pWatcher) then
        return 0
    end

    local agent = LuaAiAgent(pNpc)
    if agent == nil or agent.applyDanceMindBuff == nil then
        return 0
    end
    
    -- wipe only dance (and BF+wounds)
    agent:wipeEnhanceBuffs(pWatcher, 2)
    agent:applyDanceMindBuff(pWatcher, SmartDancerConfig.buffAmount, SmartDancerConfig.buffDuration)

    -- optional flavor message
    CreatureObject(pWatcher):sendSystemMessage("You feel your mind sharpen as you watch the dancer.")
    return 0
end