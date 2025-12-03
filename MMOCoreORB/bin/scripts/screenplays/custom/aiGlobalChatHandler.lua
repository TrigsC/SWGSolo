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
    [8] = "Warrent Officer II",
    [9] = "Warrent Officer I",
    [10] = "Second Lieutenant",
    [11] = "Lieutenant",
    [12] = "Captain",
    [13] = "Major",
    [14] = "Lieutenant Colonel",
    [15] = "Colonel",
}

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
function AiGlobalChatHandler:findNearbyResponder(pPlayer, message)
    
    local pScenePlayer = SceneObject(pPlayer)
    if (pScenePlayer == nil) then return nil end

    -- THIS NOW WORKS THANKS TO YOUR C++ EDIT!
    local nearbyObjects = pScenePlayer:getInRangeObjects()
    
    if (nearbyObjects == nil) then return nil end

    local bestMatch = nil
    local messageLower = string.lower(message)

    for i = 1, #nearbyObjects, 1 do
        local pObj = nearbyObjects[i]
        
        -- Filter: Must be a Creature (NPC/Pet), Not the Player, and Close by
        if (pObj ~= nil and pObj ~= pPlayer and SceneObject(pObj):isCreatureObject()) then
            
            -- Manual Distance Check (CloseObjects can include things 100m+ away)
            if (pScenePlayer:isInRangeWithObject(pObj, 20)) then
                
                -- Check Registry
                local profile = AiRegistry.getProfile(pObj)
                
                if (profile ~= nil) then
                    local isMatch = false
                    
                    -- Check A: Name
                    local name = string.lower(SceneObject(pObj):getDisplayedName())
                    if (string.find(messageLower, name)) then isMatch = true end

                    -- Check B: Call Sign
                    if (not isMatch and profile.call_signs) then
                        for k, sign in pairs(profile.call_signs) do
                            if (string.find(messageLower, sign)) then
                                isMatch = true
                                break
                            end
                        end
                    end

                    -- Check C: Ownership (The "Smart" Check)
                    if (isMatch) then
                        local owner = CreatureObject(pObj):getOwner()
                        if (owner == pPlayer) then return pObj end -- Priority
                        
                        if (bestMatch == nil) then bestMatch = pObj end
                    end
                end
            end
        end
    end

    return bestMatch
end

----------------------------------------------------------------------
-- 2. CHAT LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:notifySpatialChatSent(pPlayer, pChatMessage, nothing)
    
    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    local spatialMsg = getChatMessage(pChatMessage)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    -- B. DETERMINE THE TARGET
    local pCreature = CreatureObject(pPlayer)
    local targetID = pCreature:getTargetID()
    local pTarget = nil

    -- Priority 1: Use Hard Target
    if (targetID ~= 0) then
        local pPossibleTarget = getSceneObject(targetID)
        if (pPossibleTarget ~= nil and SceneObject(pPossibleTarget):isCreatureObject()) then
            -- Only use target if it has a valid AI Profile
            if (AiRegistry.getProfile(pPossibleTarget) ~= nil) then
                pTarget = pPossibleTarget
                print("[AI Global] Using Selected Target.")
            end
        end
    end

    -- Priority 2: Scan for Keyword/Name match if no valid target found
    if (pTarget == nil) then
        pTarget = self:findNearbyResponder(pPlayer, spatialMsg)
        if (pTarget ~= nil) then
            print("[AI Global] Auto-detected responder via keyword/name.")
        end
    end

    -- If we still have no target, we give up
    if (pTarget == nil) then
        return 0
    end

    -- C. CHECK DISTANCE (15 meters)
    if (not SceneObject(pPlayer):isInRangeWithObject(pTarget, 15)) then
        return 0
    end

    -- D. LOAD PROFILE
    local profile = AiRegistry.getProfile(pTarget)
    if (profile == nil) then return 0 end -- Should catch this above, but safety first

    -- E. TRIGGER THE AI
    local playerContext = self:getPlayerContext(pPlayer)
    local npcContext = self:getNpcContext(pTarget)
    
    -- Send to Brain
    local aiResponse = AiBrain.askBrain(spatialMsg, profile, playerContext, npcContext)
    spatialChat(pTarget, aiResponse)
    
    -- Handle Skills
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