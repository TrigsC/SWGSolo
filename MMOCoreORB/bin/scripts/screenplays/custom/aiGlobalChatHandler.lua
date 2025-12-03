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
    
    -- DEBUG 1: Did the observer fire at all?
    -- print("[AI Debug] Observer Triggered by: " .. SceneObject(pPlayer):getCustomObjectName())

    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    -- A. Decode the message
    local spatialMsg = getChatMessage(pChatMessage)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    -- DEBUG 2: What did they say?
    -- print("[AI Debug] Message: " .. spatialMsg)

    -- B. GET THE TARGET
    local pCreature = CreatureObject(pPlayer)
    local targetID = pCreature:getTargetID()

    -- DEBUG 3: Do they have a target?
    if (targetID == 0) then 
        -- print("[AI Debug] Ignored: Player has no target selected.")
        return 0 
    end

    local pTarget = getSceneObject(targetID)

    if (pTarget == nil or not SceneObject(pTarget):isCreatureObject()) then
        -- print("[AI Debug] Ignored: Target is not a creature.")
        return 0
    end

    -- C. CHECK DISTANCE (15 meters)
    if (not SceneObject(pPlayer):isInRangeWithObject(pTarget, 15)) then
        print("[AI Debug] Ignored: Target is too far away.")
        return 0
    end

    -- D. CHECK THE REGISTRY
    local templatePath = SceneObject(pTarget):getTemplateObjectPath()
    
    -- DEBUG 4: Check the template path
    print("[AI Debug] Checking Template: " .. templatePath)
    
    local profile = AiRegistry.getProfileByTemplate(templatePath)

    if (profile == nil) then
        print("[AI Debug] Ignored: No profile found for this template.")
        return 0
    end

    -- E. TRIGGER THE AI
    print("[AI Global] SUCCESS! Targeted Chat detected for: " .. profile.name)
    
    local aiResponse = AiBrain.askBrain(spatialMsg, profile)

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