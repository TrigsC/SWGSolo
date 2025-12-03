local ObjectManager = require("managers.object.object_manager")
local AiBrain = require("custom_scripts.ai_brain")
local AiRegistry = require("custom_scripts.ai_registry")

AiGlobalChatHandler = ScreenPlay:new {
    --numberOfActs = 1,
}

-- 1. CRITICAL FIX: Uncomment this! 
registerScreenPlay("AiGlobalChatHandler", true)

----------------------------------------------------------------------
-- 1. STARTUP & LOGIN LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:start()
    print("[AI Global] Handler Started.")
    
    -- 2. CRITICAL FIX: Bind the Login Observer
    -- Since "onPlayerLoggedIn" isn't automatic, we attach an observer to the ZoneServer.
    -- Event 2 = PLAYERLOGGEDIN
    local pZoneServer = getZoneServer()
    if (pZoneServer ~= nil) then
        createObserver(PLAYERLOGGEDIN, "AiGlobalChatHandler", "onPlayerLoggedIn", pZoneServer)
        print("[AI Global] Login Observer attached to ZoneServer.")
    else
        print("[AI Global] ERROR: Could not find ZoneServer to attach observer.")
    end
end

-- This function is now called by the Observer we created in start()
function AiGlobalChatHandler:onPlayerLoggedIn(pPlayer)
    if (pPlayer == nil) then return 0 end
    
    self:registerObservers(pPlayer)
    print("[AI Global] Chat Observer attached to " .. SceneObject(pPlayer):getCustomObjectName())
    
    return 0
end

function AiGlobalChatHandler:registerObservers(pPlayer)
    -- Observer 50 = SPATIALCHATSENT
    -- Check if already attached to avoid duplicates (optional but good)
    if (not hasObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)) then
        createObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
    end
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
    
    -- 3. CRITICAL FIX: You were trying to use targetID before defining it!
    local targetID = pCreature:getTargetID()

    -- Check if they have a target
    if (targetID == 0) then return 0 end

    local pTarget = getSceneObject(targetID)

    if (pTarget == nil or not SceneObject(pTarget):isCreatureObject()) then
        return 0
    end

    -- C. CHECK DISTANCE (Must be within 15 meters)
    if (not SceneObject(pPlayer):isInRangeWithObject(pTarget, 15)) then
        return 0
    end

    -- D. CHECK THE REGISTRY
    local templatePath = SceneObject(pTarget):getTemplateObjectPath()
    local profile = AiRegistry.getProfileByTemplate(templatePath)

    if (profile == nil) then
        -- This is just a normal NPC, ignore it.
        return 0
    end

    -- E. TRIGGER THE AI
    print("[AI Global] Targeted Chat detected for: " .. profile.name)
    
    -- 1. Get Response from Brain
    local aiResponse = AiBrain.askBrain(spatialMsg, profile)

    -- 2. Make the NPC Speak
    spatialChat(pTarget, aiResponse)
    
    -- 3. Check for Skills (Heals/Buffs)
    if profile.skills then
        for keyword, skillData in pairs(profile.skills) do
            if string.find(string.lower(spatialMsg), keyword) then
                
                -- Play Animation
                if skillData.animation then
                    CreatureObject(pTarget):doAnimation(skillData.animation)
                end

                -- Call C++ Function
                if skillData.cpp_function == "healCreatureTarget" then
                    LuaAiAgent(pTarget):healCreatureTarget(pPlayer)
                end
            end
        end
    end

    return 0
end

return AiGlobalChatHandler