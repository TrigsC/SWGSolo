local ObjectManager = require("managers.object.object_manager")
local AiBrain = require("custom_scripts.ai_brain")
local AiRegistry = require("custom_scripts.ai_registry")

AiGlobalChatHandler = ScreenPlay:new {
    --numberOfActs = 1,
}

--registerScreenPlay("AiGlobalChatHandler", true)

----------------------------------------------------------------------
-- 1. STARTUP & LOGIN LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:start()
    -- This runs when the server boots up
    print("[AI Global] Handler Started.")
end

function AiGlobalChatHandler:registerObservers(pPlayer)
	createObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
    print("[AI Global] Chat Observer attached to player.")
end

-- This function is automatically called by Core3 when a player logs in
-- We use it to attach the "ears" (Chat Observer) immediately.
function AiGlobalChatHandler:onPlayerLoggedIn(pPlayer)
    if (pPlayer == nil) then return end
    
    -- Observer 50 = SPATIALCHATSENT
    -- We attach it to the player so we hear everything they say
    self:registerObservers(pPlayer)
    --createObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
    print("[AI Global] Chat Observer.")
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
    -- We check who the player is actually looking at/targeting
    local pCreature = CreatureObject(pPlayer)
    local pTarget = getSceneObject(targetID)

    if (pTarget == nil or not SceneObject(pTarget):isCreatureObject()) then
		return 0
	end

    -- C. VALIDATE THE TARGET
    --if (pTarget == nil or not pTarget:isCreatureObject()) then return 0 end

    -- Check Distance (Must be within 15 meters)
    if (not SceneObject(pPlayer):isInRangeWithObject(pTarget, 15)) then
        return 0
    end

    -- D. CHECK THE REGISTRY
    -- Does this NPC have a brain?
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
                    -- We use the Bridge function we made earlier!
                    LuaAiAgent(pTarget):healCreatureTarget(pPlayer)
                end
            end
        end
    end

    return 0
end

return AiGlobalChatHandler