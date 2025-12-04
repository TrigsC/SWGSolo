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

local AI_RANGE = 20

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

function AiGlobalChatHandler:getNpcContext(pTarget)
    if (pTarget == nil) then return "" end
    
    -- Get the visible name (e.g., "Ra'He Fiwo" or "Stormtrooper")
    local name = SceneObject(pTarget):getDisplayedName()
    
    local context = "Your name is " .. name .. "."

    -- Optional: Add Location (Planet) so they know where they are
    local zoneName = SceneObject(pTarget):getZoneName()
    context = context .. " You are currently on the planet " .. zoneName .. "."

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
-- 2. Nearby LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:findNearbyResponder(pPlayer, message, preferredTargetID)
    
    local pScenePlayer = SceneObject(pPlayer)
    if (pScenePlayer == nil) then return nil end

    local nearbyObjects = pScenePlayer:getInRangeObjects()
    if (nearbyObjects == nil) then return nil end

    local bestMatch = nil
    local closestDistance = math.huge
    local messageLower = string.lower(message)

    print("[AI Debug] Scanning " .. #nearbyObjects .. " nearby objects for keywords...")

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
                        -- PRIORITY 1: Ownership
                        local owner = CreatureObject(pObj):getOwner()
                        if (owner == pPlayer) then return pObj end 
                        
                        -- PRIORITY 2: Preferred Target
                        if (preferredTargetID ~= 0 and SceneObject(pObj):getObjectID() == preferredTargetID) then
                            return pObj
                        end
                
                        -- PRIORITY 3: Proximity (THE FIX)
                        -- Old Line: local dist = pScenePlayer:getDistanceTo(pObj)  <-- THIS WAS BUGGED
                        
                        -- New Line: Calculate True World Distance
                        local dist = self:getWorldDistance(pPlayer, pObj)
                        
                        -- print("[AI Debug] Candidate: " .. name .. " WorldDist: " .. dist)
                
                        if (dist < closestDistance) then
                            closestDistance = dist
                            bestMatch = pObj
                        end
                    end
                end
            end
        end
    end
    
    if (bestMatch ~= nil) then
        local winnerName = SceneObject(bestMatch):getDisplayedName()
        print("[AI Debug] Scanner Winner: " .. winnerName .. " Dist: " .. closestDistance)
    end

    return bestMatch
end

function AiGlobalChatHandler:getWorldDistance(pObj1, pObj2)
    if (pObj1 == nil or pObj2 == nil) then return math.huge end
    
    local obj1 = SceneObject(pObj1)
    local obj2 = SceneObject(pObj2)
    
    local x1 = obj1:getWorldPositionX()
    local y1 = obj1:getWorldPositionY()
    local z1 = obj1:getWorldPositionZ()
    
    local x2 = obj2:getWorldPositionX()
    local y2 = obj2:getWorldPositionY()
    local z2 = obj2:getWorldPositionZ()
    
    local dx = x1 - x2
    local dy = y1 - y2
    local dz = z1 - z2
    
    -- standard 3D distance formula
    return math.sqrt(dx*dx + dy*dy + dz*dz)
end

----------------------------------------------------------------------
-- 2. CHAT LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:notifySpatialChatSent(pPlayer, pChatMessage, nothing)
    
    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    local spatialMsg = getChatMessage(pChatMessage)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    -- B. GET TARGET ID (But don't decide yet)
    local pCreature = CreatureObject(pPlayer)
    local targetID = pCreature:getTargetID()
    local pTarget = nil

    -- C. STRATEGY: KEYWORDS FIRST
    -- We pass the targetID so the scanner can use it as a tie-breaker, 
    -- but if the message says "Padawan", the scanner will return the Padawan 
    -- even if 'targetID' points to a SpecForce Marine.
    pTarget = self:findNearbyResponder(pPlayer, spatialMsg, targetID)

    if (pTarget ~= nil) then
        print("[AI Global] Auto-detected responder via keyword.")
    else
        -- D. FALLBACK: TARGET
        -- No keyword matched? Okay, talk to the person we are looking at.
        if (targetID ~= 0) then
            local pPossibleTarget = getSceneObject(targetID)
            
            if (pPossibleTarget ~= nil and SceneObject(pPossibleTarget):isCreatureObject()) then
                -- Only use if they have a valid profile
                if (AiRegistry.getProfile(pPossibleTarget) ~= nil) then
                    pTarget = pPossibleTarget
                    print("[AI Global] Using Hard Target (No keyword detected).")
                end
            end
        end
    end

    -- If still no target, we are done.
    if (pTarget == nil) then
        return 0
    end

    -- E. CHECK DISTANCE
    local finalDist = self:getWorldDistance(pPlayer, pTarget)
    
    if (finalDist > AI_RANGE) then
        print("[AI Debug] Ignored: Target found, but out of range (" .. finalDist .. "m > " .. AI_RANGE .. "m).")
        return 0
    end

    -- F. EXECUTE AI
    local profile = AiRegistry.getProfile(pTarget)
    if (profile == nil) then 
        print("[AI Debug] Ignored: Registry returned nil profile.")
        return 0 
    end

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