-- MMOCoreORB/bin/scripts/screenplays/custom/aiGlobalChatHandler.lua
--
-- Adds SmartDoctorBuffer routing while preserving your existing recruiter + normal LLM paths.
-- Key behavior:
--   - If profile.role == "smart_doctor": route to SmartDoctorBuffer:handleChat(...) (deterministic).
--   - Otherwise keep your existing recruiter logic and standard AiBrain chat logic intact.

local ObjectManager = require("managers.object.object_manager")
local okRegistry, AiRegistry = pcall(require, "custom_scripts.ai_registry")
if not okRegistry or AiRegistry == nil then
    AiRegistry = {
        getProfile = function() return nil end,
        getProfileByTemplate = function() return nil end
    }
end

local okRecruiter, recruiterScreenplay = pcall(require, "screenplays.gcw.recruiters.recruiterScreenplay")
if not okRecruiter then
    recruiterScreenplay = nil
end

local AiLogger = nil
do
    local ok, logger = pcall(require, "custom_scripts.ai_logger")
    if ok and logger ~= nil then
        AiLogger = logger
    else
        AiLogger = {
            error = function() end,
            warn = function() end,
            info = function() end,
            debug = function() end,
            trace = function() end
        }
    end
end

local AiBrain = nil
do
    local ok, brain = pcall(require, "custom_scripts.ai_brain")
    if ok and brain ~= nil then
        AiBrain = brain
    else
        AiLogger.warn("chat", "custom_scripts.ai_brain unavailable; using deterministic AI fallbacks.")
        AiBrain = {
            getChatResponse = function()
                return "..."
            end,
            getRecruiterIntent = function()
                return { intent = "chat", reply = "I'm having trouble understanding you, soldier." }
            end,
            getDoctorFlavorLine = function()
                return nil
            end
        }
    end
end

-- Smart Doctor Buffer deterministic handler
--local SmartDoctorBuffer = require("screenplays.custom.smartDoctorBuffer")

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

local function safeCall(defaultValue, fn)
    local ok, result = pcall(fn)
    if ok then
        return result
    end

    AiLogger.debug("chat", "Safe call failed: " .. tostring(result))
    return defaultValue
end

AiGlobalChatHandler = ScreenPlay:new {}

registerScreenPlay("AiGlobalChatHandler", true)

----------------------------------------------------------------------
-- 1. STARTUP & LOGIN LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:start()
    AiLogger.info("chat", "Global chat handler started.")
end

-- 2. LOGIN HANDLER
function AiGlobalChatHandler:onPlayerLoggedIn(pPlayer)

    -- Safety Check 1: Did we get a valid object?
    if (pPlayer == nil) then
        AiLogger.error("chat", "onPlayerLoggedIn received nil player.")
        return 0
    end

    -- Safety Check 2: Is it actually a scene object?
    local pSceneObject = safeCall(nil, function() return LuaSceneObject(pPlayer) end)
    if (pSceneObject == nil) then
        return 0
    end

    -- Call the internal function using COLON because we are inside Lua now
    safeCall(nil, function() AiGlobalChatHandler:registerObservers(pPlayer) end)
    local playerName = safeCall("unknown", function() return pSceneObject:getCustomObjectName() end)
    AiLogger.debug("chat", "Chat observer attached to " .. tostring(playerName))

    return 0
end

function AiGlobalChatHandler:registerObservers(pPlayer)
    if (pPlayer == nil) then return end

    -- Drop then Create to avoid duplicates or crashes
    safeCall(nil, function() dropObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer) end)
    safeCall(nil, function() createObserver(SPATIALCHATSENT, "AiGlobalChatHandler", "notifySpatialChatSent", pPlayer) end)
end

function AiGlobalChatHandler:getWorldDistance(pObj1, pObj2)
    if (pObj1 == nil or pObj2 == nil) then return math.huge end

    -- Force cast to SceneObject safely
    local scno1 = safeCall(nil, function() return LuaSceneObject(pObj1) end)
    local scno2 = safeCall(nil, function() return LuaSceneObject(pObj2) end)

    if (scno1 == nil or scno2 == nil) then return math.huge end

    local x1 = safeCall(nil, function() return scno1:getWorldPositionX() end)
    local y1 = safeCall(nil, function() return scno1:getWorldPositionY() end)
    local z1 = safeCall(nil, function() return scno1:getWorldPositionZ() end)

    local x2 = safeCall(nil, function() return scno2:getWorldPositionX() end)
    local y2 = safeCall(nil, function() return scno2:getWorldPositionY() end)
    local z2 = safeCall(nil, function() return scno2:getWorldPositionZ() end)

    if (x1 == nil or y1 == nil or z1 == nil or x2 == nil or y2 == nil or z2 == nil) then
        return math.huge
    end

    local dx = x1 - x2
    local dy = y1 - y2
    local dz = z1 - z2

    return math.sqrt(dx*dx + dy*dy + dz*dz)
end

function AiGlobalChatHandler:getPlayerContext(pPlayer)
    if (pPlayer == nil) then return "" end

    local pCreature = safeCall(nil, function() return CreatureObject(pPlayer) end)
    if (pCreature == nil) then return "" end

    local name = safeCall("unknown", function() return pCreature:getFirstName() end)
    if (name == nil or name == "") then name = "unknown" end

    -- 1. FACTION
    local faction = "Civilian"
    if (safeCall(false, function() return pCreature:isRebel() end)) then faction = "Rebel" end
    if (safeCall(false, function() return pCreature:isImperial() end)) then faction = "Imperial" end

    -- 2. RANK
    local rankTitle = ""
    if (faction ~= "Civilian") then
        local rankID = safeCall(0, function() return pCreature:getFactionRank() end)
        if (FactionRanks[rankID]) then
            rankTitle = FactionRanks[rankID]
        else
            rankTitle = "Rank " .. tostring(rankID)
        end
    end

    -- 3. CONSTRUCT SENTENCE
    local context = "The player's name is " .. name .. "."

    if (faction ~= "Civilian") then
        context = context .. " They are a " .. faction .. " " .. rankTitle .. "."
    else
        context = context .. " They are a civilian."
    end

    -- 4. JEDI CHECK
    if (safeCall(false, function() return pCreature:hasSkill("force_title_jedi_novice") end)) then
        context = context .. " They appear to be force sensitive."
    end

    return context
end

function AiGlobalChatHandler:getNpcContext(pTarget)
    if (pTarget == nil) then return "" end

    local name = safeCall("unknown", function() return SceneObject(pTarget):getDisplayedName() end)
    if (name == nil or name == "") then name = "unknown" end
    local context = "Your name is " .. name .. "."

    local zoneName = safeCall("unknown", function() return SceneObject(pTarget):getZoneName() end)
    if (zoneName == nil or zoneName == "") then zoneName = "unknown" end
    context = context .. " You are currently on the planet " .. zoneName .. "."

    return context
end

----------------------------------------------------------------------
-- 2. Nearby LOGIC
----------------------------------------------------------------------
function AiGlobalChatHandler:findNearbyResponder(pPlayer, message, preferredTargetID)

    local pScenePlayer = safeCall(nil, function() return SceneObject(pPlayer) end)
    if (pScenePlayer == nil) then return nil end

    local nearbyObjects = safeCall(nil, function() return pScenePlayer:getInRangeObjects() end)
    if (nearbyObjects == nil) then return nil end

    local bestMatch = nil
    local closestDistance = math.huge
    local messageLower = string.lower(tostring(message or ""))
    local playerID = safeCall(0, function() return SceneObject(pPlayer):getObjectID() end)

    for i = 1, #nearbyObjects, 1 do
        local pObj = nearbyObjects[i]

        -- Check if it's a creature and not the player
        local objID = safeCall(0, function() return SceneObject(pObj):getObjectID() end)
        local isCreature = safeCall(false, function() return SceneObject(pObj):isCreatureObject() end)
        if (pObj ~= nil and objID ~= 0 and objID ~= playerID and isCreature) then
            local dist = self:getWorldDistance(pPlayer, pObj)

            if (dist <= AI_RANGE) then
                local profile = AiRegistry.getProfile(pObj)

                if (profile ~= nil) then
                    AiLogger.trace("chat", "AI profile found while scanning nearby responder.")
                    local isMatch = false

                    local name = safeCall("", function() return SceneObject(pObj):getDisplayedName() end)
                    name = string.lower(tostring(name or ""))
                    if (name ~= "" and string.find(messageLower, name, 1, true)) then isMatch = true end

                    if (not isMatch and type(profile.call_signs) == "table") then
                        for k, sign in pairs(profile.call_signs) do
                            sign = tostring(sign or "")
                            if (sign ~= "" and string.find(messageLower, sign, 1, true)) then
                                isMatch = true
                                break
                            end
                        end
                    end

                    if (isMatch) then
                        -- PRIORITY 1: Ownership
                        local owner = safeCall(nil, function() return CreatureObject(pObj):getOwner() end)
                        if (owner == pPlayer) then return pObj end

                        -- PRIORITY 2: Preferred Target
                        if (preferredTargetID ~= 0 and objID == preferredTargetID) then
                            return pObj
                        end

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
-- 3. CHAT LOGIC (Includes Recruiter & Normal Handling + Smart Doctor)
----------------------------------------------------------------------
function AiGlobalChatHandler:notifySpatialChatSent(pPlayer, pChatMessage, nothing)

    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    local spatialMsg = safeCall(nil, function() return getChatMessage(pChatMessage) end)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    -- B. GET TARGET ID
    local pCreature = safeCall(nil, function() return CreatureObject(pPlayer) end)
    if (pCreature == nil) then return 0 end

    local targetID = safeCall(0, function() return pCreature:getTargetID() end)
    local pTarget = nil

    -- C. STRATEGY: KEYWORDS FIRST
    pTarget = self:findNearbyResponder(pPlayer, spatialMsg, targetID)

    if (pTarget ~= nil) then
        AiLogger.debug("chat", "Auto-detected responder via keyword.")
    else
        -- D. FALLBACK: TARGET
        if (targetID ~= 0) then
            local pPossibleTarget = getSceneObject(targetID)

            if (pPossibleTarget ~= nil and safeCall(false, function() return SceneObject(pPossibleTarget):isCreatureObject() end)) then
                -- Only use if they have a valid profile
                if (AiRegistry.getProfile(pPossibleTarget) ~= nil) then
                    pTarget = pPossibleTarget
                    AiLogger.debug("chat", "Using hard target responder.")
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
        AiLogger.debug("chat", "Ignored target out of range (" .. tostring(finalDist) .. "m > " .. tostring(AI_RANGE) .. "m).")
        return 0
    end

    -- F. GET PROFILE
    local profile = AiRegistry.getProfile(pTarget)
    if (type(profile) ~= "table") then
        AiLogger.trace("chat", "Ignored target because registry returned nil profile.")
        return 0
    end

    ----------------------------------------------------------------------
    -- G. DECISION TREE: SMART DOCTOR vs RECRUITER vs NORMAL
    ----------------------------------------------------------------------
    if (profile.role == "smart_doctor") then
        if (SmartDoctorBuffer ~= nil and SmartDoctorBuffer.handleChat ~= nil) then
            AiLogger.debug("doctor", "Routing chat to SmartDoctorBuffer.")
            local handled = false
            local ok, err = pcall(function()
                handled = SmartDoctorBuffer:handleChat(pTarget, pPlayer, spatialMsg)
            end)
    
            if (not ok) then
                AiLogger.error("doctor", "handleChat exception: " .. tostring(err))
                return 0
            end
    
            -- If the doctor handled the message, stop here.
            -- If it returned false, fall through to normal AI response (optional).
            if (handled) then
                AiLogger.debug("doctor", "SmartDoctorBuffer handled chat.")
                return 0
            end
        else
            AiLogger.warn("doctor", "SmartDoctorBuffer not loaded; smart doctor chat ignored.")
            return 0
        end

        -- If not handled (message unrelated), allow fall-through to normal NPC response if desired:
        -- (But in practice, SmartDoctorBuffer returns false for non-buff lines and this might cause LLM chatter.
        --  If you want the doctor to ONLY respond to buff lines, leave this fall-through as-is or return 0 here.)
        -- return 0

    elseif (profile.role == "recruiter") then
        -- === PATH 1: RECRUITER AI (JSON LOGIC) ===
        if (recruiterScreenplay == nil) then
            AiLogger.warn("chat", "Recruiter screenplay unavailable; recruiter AI route skipped.")
            return 0
        end

        local function gcwDiscount()
            if type(getGCWDiscount) ~= "function" then
                return 0
            end

            return safeCall(0, function() return getGCWDiscount(pPlayer) end)
        end

        -- A. STUCK CHECK: If the server restarted while the timer was running, the data might be gone.
        -- This logic (borrowed from Core3) fixes the player so they aren't stuck forever.
        local playerObjectID = safeCall(0, function() return CreatureObject(pPlayer):getObjectID() end)
        if (safeCall(false, function() return CreatureObject(pPlayer):isChangingFactionStatus() end) and readData(playerObjectID .. ":changingFactionStatus") ~= 1) then
            safeCall(nil, function() recruiterScreenplay:handleGoCovert(pPlayer) end)
        end

        -- B. LOCKDOWN CHECK: If they are actively waiting for status change, BLOCK interaction.
        if (safeCall(false, function() return CreatureObject(pPlayer):isChangingFactionStatus() end)) then
            safeCall(nil, function() spatialChat(pTarget, "Greetings. I see that your status is currently being processed. I won't be able to help you until that is complete. It should not take much longer.") end)
            return 0
        end

        -- 1. Get Game Context specifically for Recruiters (Rank, Points)
        local recruiterContext = safeCall("", function() return recruiterScreenplay:getPlayerStatusContext(pPlayer, pTarget) end)

        -- 2. Ask Brain for INTENT (Returns a Lua table from JSON)
        local aiData = safeCall(nil, function() return AiBrain.getRecruiterIntent(spatialMsg, recruiterContext) end)

        -- 3. Act on the Result
        if type(aiData) == "table" then
            -- Speak the flavor text
            if aiData.reply then
                safeCall(nil, function() spatialChat(pTarget, tostring(aiData.reply)) end)
            end

            -- Trigger Game Mechanics
            if aiData.intent == "promote" then
                safeCall(nil, function() recruiterScreenplay:attemptPromotion(pPlayer, pTarget) end)

            elseif aiData.intent == "buy_armor" then
                local discount = gcwDiscount()
                safeCall(nil, function() recruiterScreenplay:sendPurchaseSui(pTarget, pPlayer, "fp_weapons_armor", discount) end)

            elseif aiData.intent == "buy_furniture" then
                local discount = gcwDiscount()
                safeCall(nil, function() recruiterScreenplay:sendPurchaseSui(pTarget, pPlayer, "fp_furniture", discount) end)

            elseif aiData.intent == "buy_structures" then
                local discount = gcwDiscount()
                safeCall(nil, function() recruiterScreenplay:sendPurchaseSui(pTarget, pPlayer, "fp_installations", discount) end)

            elseif aiData.intent == "buy_hirelings" then
                local discount = gcwDiscount()
                safeCall(nil, function() recruiterScreenplay:sendPurchaseSui(pTarget, pPlayer, "fp_hirelings", discount) end)

            elseif aiData.intent == "buy_schematics" then
                local discount = gcwDiscount()
                safeCall(nil, function() recruiterScreenplay:sendPurchaseSui(pTarget, pPlayer, "fp_schematics", discount) end)

            elseif aiData.intent == "buy_uniforms" then
                local discount = gcwDiscount()
                safeCall(nil, function() recruiterScreenplay:sendPurchaseSui(pTarget, pPlayer, "fp_uniforms", discount) end)

            elseif aiData.intent == "check_war_status" then
                safeCall(nil, function() recruiterScreenplay:announceGCWScore(pTarget) end)

            elseif aiData.intent == "go_overt" then
                safeCall(nil, function() recruiterScreenplay:attemptToggleStatus(pPlayer, pTarget, 2) end)

            elseif aiData.intent == "go_covert" then
                safeCall(nil, function() recruiterScreenplay:attemptToggleStatus(pPlayer, pTarget, 1) end)

            elseif aiData.intent == "go_on_leave" then
                safeCall(nil, function() recruiterScreenplay:attemptToggleStatus(pPlayer, pTarget, 0) end)
            end
        else
            safeCall(nil, function() spatialChat(pTarget, "I... I'm not sure what you mean, soldier. Please ask in a different way!") end)
        end

    else
        -- === PATH 2: STANDARD NPC AI (FLAVOR TEXT) ===
        local playerContext = self:getPlayerContext(pPlayer)
        local npcContext = self:getNpcContext(pTarget)

        -- Use the standard Chat Response function
        local aiResponse = safeCall("...", function() return AiBrain.getChatResponse(spatialMsg, profile, playerContext, npcContext) end)
        if aiResponse == nil or aiResponse == "" then
            aiResponse = "..."
        end
        safeCall(nil, function() spatialChat(pTarget, tostring(aiResponse)) end)
    end

    ----------------------------------------------------------------------
    -- H. LEGACY SKILL HANDLER (Optional extras defined in Registry)
    ----------------------------------------------------------------------
    if type(profile.skills) == "table" then
        for keyword, skillData in pairs(profile.skills) do
            local keywordText = tostring(keyword or "")
            if keywordText ~= "" and string.find(string.lower(tostring(spatialMsg or "")), keywordText, 1, true) then
                if type(skillData) == "table" and skillData.animation then
                    safeCall(nil, function() CreatureObject(pTarget):doAnimation(skillData.animation) end)
                end
                if type(skillData) == "table" and skillData.cpp_function == "healCreatureTarget" then
                    safeCall(nil, function()
                        if SceneObject(pTarget):isAiAgent() then
                            LuaAiAgent(pTarget):healCreatureTarget(pPlayer)
                        end
                    end)
                end
            end
        end
    end

    return 0
end

return AiGlobalChatHandler
