-- Shared helpers for custom entertainer service NPC screenplays.

SmartEntertainerHelper = SmartEntertainerHelper or {}

function SmartEntertainerHelper.safeSetCustomName(pNpc, name)
    if pNpc == nil or name == nil or name == "" then return end
    SceneObject(pNpc):setCustomObjectName(name)
end

function SmartEntertainerHelper.isValidAudienceMember(pPlayer, pEntertainer, range)
    if pPlayer == nil or pEntertainer == nil then return false end
    if not SceneObject(pPlayer):isPlayerCreature() then return false end

    local maxRange = tonumber(range) or 0
    if SceneObject(pEntertainer).isInRangeWithObject ~= nil then
        return SceneObject(pEntertainer):isInRangeWithObject(pPlayer, maxRange)
    end

    local dist = SceneObject(pEntertainer):getDistanceTo(pPlayer)
    return dist ~= nil and dist <= maxRange
end

function SmartEntertainerHelper.scheduleHeartbeat(screenplayName, methodName, pNpc, delayMs)
    if screenplayName == nil or screenplayName == "" then return end
    if methodName == nil or methodName == "" then return end
    if pNpc == nil then return end

    createEvent(delayMs, screenplayName, methodName, pNpc, "")
end

return SmartEntertainerHelper
