local ObjectManager = require("managers.object.object_manager")
local AiBrain = require("custom_scripts.ai_brain")
local AiRegistry = require("custom_scripts.ai_registry")

AiGlobalChatHandler = ScreenPlay:new {
    --numberOfActs = 1,
}

-- 1. CRITICAL FIX: This registers the script so the server runs it.
registerScreenPlay("AiGlobalChatHandler", true)

----------------------------------------------------------------------
-- 1. STARTUP & LOGIN LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:start()
    print("[AI Global] Handler Started.")
end

-- 2. LOGIN HANDLER
-- Standard Core3 automatically calls "onPlayerLoggedIn" for all registered screenplays.
-- We use this to attach the ears (Observer) to the player.
function AiGlobalChatHandler:onPlayerLoggedIn(pPlayer)
    
    -- Safety Check 1: Did we get a valid object?
    if (pPlayer == nil) then 
        print("[AI Global] ERROR: onPlayerLoggedIn received nil player!")
        return 0 
    end

    -- Safety Check 2: Is it actually a scene object?
    local pSceneObject = LuaSceneObject(pPlayer)
    if (pSceneObject == nil) then
        return 0
    end
    
    -- Call the internal function using COLON because we are inside Lua now
    AiGlobalChatHandler:registerObservers(pPlayer)
    
    print("[AI Global] Chat Observer attached to " .. pSceneObject:getCustomObjectName())
    
    return 0
end

function AiGlobalChatHandler:getPlayerContext(pPlayer)
    if (pPlayer == nil) then return "" end
    
    local pCreature = CreatureObject(pPlayer)
    local name = pCreature:getFirstName()
    
    -- 1. FACTION
    local faction = "Civilian"
    if (pCreature:isRebel()) then faction = "Rebel" end
    if (pCreature:isImperial()) then faction = "Imperial" end

    -- 2. RANK
    -- We only care about rank if they are declared
    local rankTitle = ""
    if (faction ~= "Civilian") then
        local rankID = pCreature:getFactionRank()
        if (FactionRanks[rankID]) then
            rankTitle = FactionRanks[rankID]
        else
            rankTitle = "Rank " .. rankID
        end
    end

    -- 3. SPECIES (Optional, requires ID mapping, keeping simple for now)
    -- local species = "Humanoid"

    -- 4. CONSTRUCT SENTENCE
    local context = "The player's name is " .. name .. "."
    
    if (faction ~= "Civilian") then
        context = context .. " They are a " .. faction .. " " .. rankTitle .. "."
    else
        context = context .. " They are a civilian."
    end

    -- 5. JEDI CHECK (Fun addition)
    if (pCreature:hasSkill("force_title_jedi_novice")) then
        context = context .. " They appear to be force sensitive."
    end
    
    return context
end

function AiGlobalChatHandler:registerObservers(pPlayer)
    if (pPlayer == nil) then return end

    -- FIX: The 'hasObserver' function caused a server crash (SIGSEGV).
    -- Instead, we use the standard "Drop then Create" pattern.
    -- This guarantees we never have duplicates and avoids the crashy check.
    
    -- 1. Drop any existing observer (Safe to call even if none exists)
    dropObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
    
    -- 2. Create the new observer
    createObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
    
    -- print("[AI Global] Chat Observer Refreshed.")
end

----------------------------------------------------------------------
-- 2. CHAT LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:notifySpatialChatSent(pPlayer, pChatMessage, nothing)
    
    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    -- A. Decode the message
    local spatialMsg = getChatMessage(pChatMessage)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    -- B. GET THE TARGET
    local pCreature = CreatureObject(pPlayer)
    local targetID = pCreature:getTargetID()

    if (targetID == 0) then return 0 end

    local pTarget = getSceneObject(targetID)

    if (pTarget == nil or not SceneObject(pTarget):isCreatureObject()) then
        return 0
    end

    -- C. CHECK DISTANCE
    if (not SceneObject(pPlayer):isInRangeWithObject(pTarget, 15)) then
        return 0
    end

    -- D. CHECK THE REGISTRY (Simpler now!)
    -- We pass the whole object to the registry, and it figures out the best match
    local profile = AiRegistry.getProfile(pTarget)

    if (profile == nil) then
        return 0
    end
    -- --- NEW CONTEXT BLOCK ---
    local playerContext = self:getPlayerContext(pPlayer)
    print("[AI Global] Context: " .. playerContext)
    print("[AI Global] Targeted Chat detected for Profile: " .. profile.name)
    -- -------------------------

    -- E. TRIGGER THE AI
    -- We pass the context as a 3rd argument now
    local aiResponse = AiBrain.askBrain(spatialMsg, profile, playerContext)

    spatialChat(pTarget, aiResponse)
    
    if profile.skills then
        for keyword, skillData in pairs(profile.skills) do
            if string.find(string.lower(spatialMsg), keyword) then
                if skillData.animation then
                    CreatureObject(pTarget):doAnimation(skillData.animation)
                end
                if skillData.cpp_function == "healCreatureTarget" then
                    LuaAiAgent(pTarget):healCreatureTarget(pPlayer)
                end
            end
        end
    end

    return 0
end

return AiGlobalChatHandler