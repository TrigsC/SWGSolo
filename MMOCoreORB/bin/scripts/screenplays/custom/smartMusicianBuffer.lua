-- MMOCoreORB/bin/scripts/screenplays/custom/smartMusicianBuffer.lua
--
-- Minimal Musician Buffer NPC:
--  - Spawns a musician NPC (custom creature template)
--  - Gives + equips an instrument (slitherhorn)
--  - Forces it to play music
--  - When a player "listens" to it, apply buffs instantly

local ObjectManager = require("managers.object.object_manager")

SmartMusicianConfig = SmartMusicianConfig or {}

SmartMusicianConfig.spawn_points = SmartMusicianConfig.spawn_points or {
    -- coronet
    {
        planet = "corellia",
        x = 19.59,
        z = -0.89,
        y = -1.51,
        heading = 90,
        cell = 8105496,
        customName = "Musician Buffer"
    },
    -- moenia
    {
        planet = "naboo",
        x = 19.16,
        z = -0.89,
        y = 3.72,
        heading = 90,
        cell = 111,
        customName = "Musician Buffer"
    },
    -- theed
    {
        planet = "naboo",
        x = 20.04,
        z = -0.89,
        y = -0.36,
        heading = 90,
        cell = 91,
        customName = "Musician Buffer"
    },
    -- mos eisly
    {
        planet = "tatooine",
        x = 18.05,
        z = -0.89,
        y = -0.85,
        heading = 90,
        cell = 1082877,
        customName = "Musician Buffer"
    }
}

-- Pick a safe/default song name you know exists
SmartMusicianConfig.songName = SmartMusicianConfig.songName or "ballad"

-- Instrument to equip (must exist in your TRE)
SmartMusicianConfig.instrumentIff = SmartMusicianConfig.instrumentIff or "object/tangible/instrument/slitherhorn.iff"

-- Tune to taste
SmartMusicianConfig.buffAmount = SmartMusicianConfig.buffAmount or 1000
SmartMusicianConfig.buffDuration = SmartMusicianConfig.buffDuration or 3600 -- seconds

SmartMusicianConfig.maxRange = SmartMusicianConfig.maxRange or 10
SmartMusicianConfig.keepPlayingHeartbeatMs = SmartMusicianConfig.keepPlayingHeartbeatMs or 15000

local function safeSetCustomObjectName(pObj, name)
    if pObj == nil or name == nil or name == "" then return end
    SceneObject(pObj):setCustomObjectName(name)
end

local function inRange(pNpc, pPlayer)
    if pNpc == nil or pPlayer == nil then return false end
    if SceneObject(pNpc).isInRangeWithObject ~= nil then
        return SceneObject(pNpc):isInRangeWithObject(pPlayer, SmartMusicianConfig.maxRange)
    end
    local dist = SceneObject(pNpc):getDistanceTo(pPlayer)
    return dist ~= nil and dist <= SmartMusicianConfig.maxRange
end

local function giveAndEquipInstrument(pMob)
    --print("Musician: giveAndEquipInstrument " .. tostring(pMob))
    if pMob == nil then return end

    local pInv = SceneObject(pMob):getSlottedObject("inventory")
    --print("Musician: inventory " .. tostring(pInv))
    if pInv == nil then return end

    local pInst = giveItem(pInv, SmartMusicianConfig.instrumentIff, -1, true)
    --print("Musician: giveItem " .. tostring(pInst))
    if pInst == nil then return end

    -- Try to slot it into hold_r via arrangementGroup 0 (containmentType = 4)
    -- LuaSceneObject::transferObject(obj, containmentType, notifyClient)
    local ok = SceneObject(pMob):transferObject(pInst, 4, true)
    --print("Musician: transferObject to mob (containment=4) ok=" .. tostring(ok))

    -- If 4 fails (rare), try a couple other arrangement groups (5,6)
    if not ok then
        ok = SceneObject(pMob):transferObject(pInst, 5, true)
        print("Musician: transferObject containment=5 ok=" .. tostring(ok))
    end
    if not ok then
        ok = SceneObject(pMob):transferObject(pInst, 6, true)
        print("Musician: transferObject containment=6 ok=" .. tostring(ok))
    end
end

SmartMusicianBuffer = SmartMusicianBuffer or ScreenPlay:new { numberOfActs = 1 }
registerScreenPlay("SmartMusicianBuffer", true)

function SmartMusicianBuffer:start()
    for _, sp in ipairs(SmartMusicianConfig.spawn_points) do
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
                safeSetCustomObjectName(pMob, sp.customName or "Musician Buffer")

                giveAndEquipInstrument(pMob)

                local agent = LuaAiAgent(pMob)
                if agent ~= nil and agent.startPlayingMusicByName ~= nil then
                    agent:startPlayingMusicByName(SmartMusicianConfig.songName)
                end

                createObserver(WASLISTENEDTO, "SmartMusicianBuffer", "notifyListened", pMob, 1)
                createEvent(SmartMusicianConfig.keepPlayingHeartbeatMs, "SmartMusicianBuffer", "keepPlaying", pMob, "")
            end
        end
    end
end

function SmartMusicianBuffer:keepPlaying(pMob)
    if pMob == nil then return end
    if not SceneObject(pMob):isCreatureObject() then return end

    local c = CreatureObject(pMob)
    if c == nil then return end

    if not c:isPlayingMusic() then
        giveAndEquipInstrument(pMob)
        print("[MUSICIAN]: keepPlaying - c:is not playing")

        local agent = LuaAiAgent(pMob)
        if agent ~= nil and agent.startPlayingMusicByName ~= nil then
            agent:startPlayingMusicByName(SmartMusicianConfig.songName)
        end
    end

    createEvent(SmartMusicianConfig.keepPlayingHeartbeatMs, "SmartMusicianBuffer", "keepPlaying", pMob, "")
end

function SmartMusicianBuffer:notifyListened(pNpc, pListener)
    if pNpc == nil or pListener == nil then return 0 end
    if not SceneObject(pListener):isPlayerCreature() then return 0 end

    if not inRange(pNpc, pListener) then
        return 0
    end

    local agent = LuaAiAgent(pNpc)
    if agent == nil or agent.applyMusicBuffs == nil then
        return 0
    end
    
    -- wipe only music (and BF+wounds)
    agent:wipeEnhanceBuffs(pListener, 4)
    agent:applyMusicBuffs(pListener, SmartMusicianConfig.buffAmount, SmartMusicianConfig.buffDuration)
    CreatureObject(pListener):sendSystemMessage("You feel inspired by the music.")
    return 0
end