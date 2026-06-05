-- Stable Lua wrapper layer around custom LuaAiAgent bindings.
-- Behavior scripts should call this module instead of reaching directly into
-- custom C++ bridge methods.

local AiAgentBridge = {}

AiAgentBridge.WIPE_MEDICAL = 1
AiAgentBridge.WIPE_DANCE = 2
AiAgentBridge.WIPE_MUSIC = 4

local AiLogger = nil
do
    local ok, logger = pcall(require, "custom_scripts.ai_logger")
    if ok and logger ~= nil then
        AiLogger = logger
    else
        AiLogger = {
            warn = function() end,
            debug = function() end,
            trace = function() end
        }
    end
end

local function getAgent(pNpc)
    if pNpc == nil then return nil end

    local ok, agent = pcall(LuaAiAgent, pNpc)
    if not ok or agent == nil then
        AiLogger.warn("bridge", "LuaAiAgent construction failed: " .. tostring(agent))
        return nil
    end

    return agent
end

function AiAgentBridge.hasMethod(pNpc, methodName)
    if pNpc == nil or methodName == nil or methodName == "" then
        return false
    end

    local agent = getAgent(pNpc)
    if agent == nil then
        return false
    end

    local ok, fn = pcall(function() return agent[methodName] end)
    if not ok then
        AiLogger.debug("bridge", "Failed to inspect LuaAiAgent method " .. tostring(methodName) .. ".")
    end
    return ok and fn ~= nil
end

local function callAgentMethod(pNpc, methodName, ...)
    if pNpc == nil or methodName == nil or methodName == "" then
        return false
    end

    local agent = getAgent(pNpc)
    if agent == nil then
        return false
    end

    local okGet, fn = pcall(function() return agent[methodName] end)
    if not okGet or fn == nil then
        AiLogger.debug("bridge", "LuaAiAgent method unavailable: " .. tostring(methodName))
        return false
    end

    local okCall, err = pcall(fn, agent, ...)
    if not okCall then
        AiLogger.warn("bridge", "LuaAiAgent method failed: " .. tostring(methodName) .. " error=" .. tostring(err))
    end
    return okCall == true
end

function AiAgentBridge.wipeBuffs(pNpc, pTarget, flags)
    if pNpc == nil or pTarget == nil or flags == nil then
        return false
    end

    return callAgentMethod(pNpc, "wipeEnhanceBuffs", pTarget, flags)
end

function AiAgentBridge.wipeMedicalBuffs(pNpc, pTarget)
    return AiAgentBridge.wipeBuffs(pNpc, pTarget, AiAgentBridge.WIPE_MEDICAL)
end

function AiAgentBridge.wipeDanceBuffs(pNpc, pTarget)
    return AiAgentBridge.wipeBuffs(pNpc, pTarget, AiAgentBridge.WIPE_DANCE)
end

function AiAgentBridge.wipeMusicBuffs(pNpc, pTarget)
    return AiAgentBridge.wipeBuffs(pNpc, pTarget, AiAgentBridge.WIPE_MUSIC)
end

function AiAgentBridge.applyMedicalBuffStep(pDoctor, pPlayer, stepKey)
    if pDoctor == nil or pPlayer == nil or stepKey == nil or stepKey == "" then
        return false
    end

    return callAgentMethod(pDoctor, "healEnhanceCreatureTarget", pPlayer, stepKey)
end

function AiAgentBridge.startDance(pDancer, danceName)
    if pDancer == nil then
        return false
    end

    return callAgentMethod(pDancer, "startDancingByName", danceName)
end

function AiAgentBridge.applyDanceMindBuff(pDancer, pPlayer, amount, duration)
    if pDancer == nil or pPlayer == nil then
        return false
    end

    return callAgentMethod(pDancer, "applyDanceMindBuff", pPlayer, amount, duration)
end

function AiAgentBridge.startMusic(pMusician, songName)
    if pMusician == nil then
        return false
    end

    return callAgentMethod(pMusician, "startPlayingMusicByName", songName)
end

function AiAgentBridge.applyMusicBuffs(pMusician, pPlayer, amount, duration)
    if pMusician == nil or pPlayer == nil then
        return false
    end

    return callAgentMethod(pMusician, "applyMusicBuffs", pPlayer, amount, duration)
end

return AiAgentBridge
