local ObjectManager = require("managers.object.object_manager")
local AiBrain = require("custom_scripts.ai_brain")
local AiRegistry = require("custom_scripts.ai_registry")

local FactionRanks = {
    [0] = "Recruit",
    [1] = "Private",
    [2] = "Lance Corporal",
    [3] = "Corporal",
    [4] = "Staff Corporal",
    [5] = "Sergeant",
    [6] = "Staff Sergeant",
    [7] = "Master Sergeant",
    [8] = "Warrant Officer II",
    [9] = "Warrant Officer I",
    [10] = "Second Lieutenant",
    [11] = "Lieutenant",
    [12] = "Captain",
    [13] = "Major",
    [14] = "Lieutenant Colonel",
    [15] = "Colonel",
}

-- CONFIGURATION
local AI_RANGE = 20 -- Both Scanner and Chatter will use this now

AiGlobalChatHandler = ScreenPlay:new {
    --numberOfActs = 1,
}

registerScreenPlay("AiGlobalChatHandler", true)

----------------------------------------------------------------------
-- 1. STARTUP & LOGIN LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:start()
    print("[AI Global] Handler Started.")
end

function AiGlobalChatHandler.onPlayerLoggedIn(pPlayer)
    if (pPlayer == nil) then 
        print("[AI Global] ERROR: onPlayerLoggedIn received nil player!")
        return 0 
    end

    local pSceneObject = LuaSceneObject(pPlayer)
    if (pSceneObject == nil) then return 0 end
    
    AiGlobalChatHandler:registerObservers(pPlayer)
    
    print("[AI Global] Attached!")
    
    return 0
end

function AiGlobalChatHandler:registerObservers(pPlayer)
    if (pPlayer == nil) then return end
    dropObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
    createObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer)
end

----------------------------------------------------------------------
-- 2. HELPER FUNCTIONS
----------------------------------------------------------------------
function AiGlobalChatHandler:getPlayerContext(pPlayer)
    if (pPlayer == nil) then return "" end
    
    local pCreature = CreatureObject(pPlayer)
    local name = pCreature:getFirstName()
    
    local faction = "Civilian"
    if (pCreature:isRebel()) then faction = "Rebel" end
    if (pCreature:isImperial()) then faction = "Imperial" end

    local rankTitle = ""
    if (faction ~= "Civilian") then
        local rankID = pCreature:getFactionRank()
        if (FactionRanks[rankID]) then
            rankTitle = FactionRanks[rankID]
        else
            rankTitle = "Rank " .. rankID
        end
    end

    local context = "The player's name is " .. name .. "."
    
    if (faction ~= "Civilian") then
        context = context .. " They are a " .. faction .. " " .. rankTitle .. "."
    else
        context = context .. " They are a civilian."
    end

    if (pCreature:hasSkill("force_title_jedi_novice")) then
        context = context .. " They appear to be force sensitive."
    end
    
    return context
end

function AiGlobalChatHandler:getNpcContext(pTarget)
    if (pTarget == nil) then return "" end
    
    local name = SceneObject(pTarget):getDisplayedName()
    local context = "Your name is " .. name .. "."

    local zoneName = SceneObject(pTarget):getZoneName()
    context = context .. " You are currently on the planet " .. zoneName .. "."

    return context
end

----------------------------------------------------------------------
-- 3. NEARBY LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:findNearbyResponder(pPlayer, message, preferredTargetID)
    
    local pScenePlayer = SceneObject(pPlayer)
    if (pScenePlayer == nil) then return nil end

    local nearbyObjects = pScenePlayer:getInRangeObjects()
    if (nearbyObjects == nil) then return nil end

    local bestMatch = nil
    local closestDistance = math.huge -- Use standard Infinity
    local messageLower = string.lower(message)

    for i = 1, #nearbyObjects, 1 do
        local pObj = nearbyObjects[i]
        
        if (pObj ~= nil and pObj ~= pPlayer and SceneObject(pObj):isCreatureObject()) then
            
            -- SYNCED: Use the global AI_RANGE constant
            if (pScenePlayer:isInRangeWithObject(pObj, AI_RANGE)) then
                
                local profile = AiRegistry.getProfile(pObj)
                
                if (profile ~= nil) then
                    local isMatch = false
                    
                    local name = string.lower(SceneObject(pObj):getDisplayedName())
                    if (string.find(messageLower, name)) then isMatch = true end

                    if (not isMatch and profile.call_signs) then
                        for k, sign in pairs(profile.call_signs) do
                            if (string.find(messageLower, sign)) then
                                isMatch = true
                                break
                            end
                        end
                    end

                    if (isMatch) then
                        local owner = CreatureObject(pObj):getOwner()
                        if (owner == pPlayer) then return pObj end 
                        
                        if (preferredTargetID ~= 0 and SceneObject(pObj):getObjectID() == preferredTargetID) then
                            return pObj
                        end

                        local dist = pScenePlayer:getDistanceTo(pObj)
                        
                        if (dist < closestDistance) then
                            closestDistance = dist
                            bestMatch = pObj
                        end
                    end
                end
            end
        end
    end

    return bestMatch
end

----------------------------------------------------------------------
-- 4. CHAT LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:notifySpatialChatSent(pPlayer, pChatMessage, nothing)
    
    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    local spatialMsg = getChatMessage(pChatMessage)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    local pCreature = CreatureObject(pPlayer)
    local targetID = pCreature:getTargetID()
    local pTarget = nil

    -- 1. Scan for Keyword Match first (Highest Priority)
    pTarget = self:findNearbyResponder(pPlayer, spatialMsg, targetID)

    if (pTarget ~= nil) then
        print("[AI Global] Auto-detected responder via keyword.")
    else
        -- 2. Fallback to Hard Target if no keyword found
        if (targetID ~= 0) then
            local pPossibleTarget = getSceneObject(targetID)
            
            if (pPossibleTarget ~= nil and SceneObject(pPossibleTarget):isCreatureObject()) then
                if (AiRegistry.getProfile(pPossibleTarget) ~= nil) then
                    pTarget = pPossibleTarget
                    print("[AI Global] Using Hard Target (No keyword detected).")
                end
            end
        end
    end

    if (pTarget == nil) then
        return 0
    end

    -- 3. FINAL DISTANCE CHECK
    -- SYNCED: Uses the same AI_RANGE (20) as the scanner
    if (not SceneObject(pPlayer):isInRangeWithObject(pTarget, AI_RANGE)) then
        print("[AI Debug] Ignored: Target found, but out of range (" .. AI_RANGE .. "m).")
        return 0
    end

    -- 4. LOAD PROFILE
    local profile = AiRegistry.getProfile(pTarget)
    if (profile == nil) then 
        print("[AI Debug] Ignored: Registry returned nil profile.")
        return 0 
    end

    -- 5. TRIGGER
    print("[AI Global] Processing Chat for: " .. profile.name)
    
    local playerContext = self:getPlayerContext(pPlayer)
    local npcContext = self:getNpcContext(pTarget)
    
    local aiResponse = AiBrain.askBrain(spatialMsg, profile, playerContext, npcContext)
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