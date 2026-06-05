-- Shared helpers for custom entertainer service NPC screenplays.

SmartEntertainerHelper = SmartEntertainerHelper or {}

function SmartEntertainerHelper.safeSetCustomName(pNpc, name)
    if pNpc == nil or name == nil or name == "" then return end
    SceneObject(pNpc):setCustomObjectName(name)
end

function SmartEntertainerHelper.safeGetObjectID(pObj)
    if pObj == nil then return 0 end

    local ok, objectID = pcall(function()
        return SceneObject(pObj):getObjectID()
    end)

    if not ok or objectID == nil then
        return 0
    end

    return objectID
end

function SmartEntertainerHelper.isValidAudienceMember(pPlayer, pEntertainer, range)
    if pPlayer == nil or pEntertainer == nil then return false end

    local okPlayer, isPlayer = pcall(function()
        return SceneObject(pPlayer):isPlayerCreature()
    end)
    if not okPlayer or not isPlayer then return false end

    local maxRange = tonumber(range) or 0
    local okRange, inRange = pcall(function()
        if SceneObject(pEntertainer).isInRangeWithObject ~= nil then
            return SceneObject(pEntertainer):isInRangeWithObject(pPlayer, maxRange)
        end

        local dist = SceneObject(pEntertainer):getDistanceTo(pPlayer)
        return dist ~= nil and dist <= maxRange
    end)

    if not okRange then
        return false
    end

    return inRange == true
end

function SmartEntertainerHelper.scheduleHeartbeat(screenplayName, methodName, pNpc, delayMs)
    if screenplayName == nil or screenplayName == "" then return end
    if methodName == nil or methodName == "" then return end
    if pNpc == nil then return end

    createEvent(delayMs, screenplayName, methodName, pNpc, "")
end

function SmartEntertainerHelper.scheduleAudienceEvent(screenplayName, methodName, pEntertainer, pPlayer, delayMs)
    if screenplayName == nil or screenplayName == "" then return false end
    if methodName == nil or methodName == "" then return false end
    if pEntertainer == nil or pPlayer == nil then return false end

    local playerID = SmartEntertainerHelper.safeGetObjectID(pPlayer)
    if playerID == 0 then return false end

    createEvent(delayMs or 100, screenplayName, methodName, pEntertainer, tostring(playerID))
    return true
end

return SmartEntertainerHelper
