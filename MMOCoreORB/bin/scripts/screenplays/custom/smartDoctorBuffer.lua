-- MMOCoreORB/bin/scripts/screenplays/custom/smartDoctorBuffer.lua
--
-- Smart Doctor Buffer NPC (Mode 2 "gold standard")
-- Deterministic transactional flow (price/confirm/queue/payment/buff steps) + optional LLM flavor.
--
-- NOTE (stability hardening):
--  1) Core3 "IllegalArgumentException" is commonly thrown by C++-bound funcs when arguments are invalid
--     (and Lua pcall will NOT reliably catch it).
--  2) This drop-in version removes ALL uses of setCustomObjectName(name, true) and uses name-only.
--  3) This drop-in version wraps ALL createEvent calls to ensure delay_ms >= 1 and args are strings.

local ObjectManager = require("managers.object.object_manager")
local AiAgentBridge = require("custom_scripts.ai_agent_bridge")
local AiLogger = nil
do
    local ok, logger = pcall(require, "custom_scripts.ai_logger")
    if ok and logger ~= nil then
        AiLogger = logger
    else
        AiLogger = {
            warn = function() end,
            info = function() end,
            debug = function() end
        }
    end
end

SmartDoctorConfig = SmartDoctorConfig or {}
do
    local ok, cfg = pcall(require, "custom_scripts.smart_doctor_config")
    if ok and cfg ~= nil then
        SmartDoctorConfig = cfg
    elseif not ok then
        AiLogger.warn("doctor", "Failed to load smart_doctor_config.lua; using embedded fallback defaults.")
    end
end

-- ===== Script identity / reload detection =====
_G.__smartDocLoadCount = (_G.__smartDocLoadCount or 0) + 1
_G.__smartDocChunkId = _G.__smartDocChunkId or tostring(os.time()) .. "-" .. tostring(math.random(100000, 999999))

if SmartDoctorBuffer == nil then
    --print("### [SmartDoctor][DEBUG] Creating ScreenPlay SmartDoctorBuffer ###")
    SmartDoctorBuffer = ScreenPlay:new { numberOfActs = 1 }
else
    --print("### [SmartDoctor][DEBUG] SmartDoctorBuffer already exists; type=" .. type(SmartDoctorBuffer) .. " ###")
    SmartDoctorBuffer = { numberOfActs = 1 }
end
-- 
-- print("### [SmartDoctor][DEBUG] _G.SmartDoctorBuffer (post-create) = " .. tostring(SmartDoctorBuffer) .. " type=" .. type(SmartDoctorBuffer) .. " ###")

-- ========= Config normalization (safe defaults) =========
SmartDoctorConfig.buff_steps = SmartDoctorConfig.buff_steps or {
    "health", "strength", "constitution", "action", "quickness", "stamina"
}
SmartDoctorConfig.total_steps = SmartDoctorConfig.total_steps or #SmartDoctorConfig.buff_steps
SmartDoctorConfig.step_delay_ms = SmartDoctorConfig.step_delay_ms or 4500
SmartDoctorConfig.confirm_timeout_ms = SmartDoctorConfig.confirm_timeout_ms or 15000
SmartDoctorConfig.pause_grace_ms = SmartDoctorConfig.pause_grace_ms or 8000
SmartDoctorConfig.max_range = SmartDoctorConfig.max_range or 10
SmartDoctorConfig.face_target = (SmartDoctorConfig.face_target ~= false)
SmartDoctorConfig.min_seconds_between_requests = SmartDoctorConfig.min_seconds_between_requests or 2
SmartDoctorConfig.max_queue_length = SmartDoctorConfig.max_queue_length or 10
SmartDoctorConfig.price = SmartDoctorConfig.price or 5000
SmartDoctorConfig.doctor_custom_name = SmartDoctorConfig.doctor_custom_name or "Doctor"

SmartDoctorConfig.spawn_points = SmartDoctorConfig.spawn_points or {
    -- coronet
    {
        key = "coronet_medcenter",
        planet = "corellia",
        x = -18.54,
        z = 0.26,
        y = 3.33,
        heading = 90,
        cell = 1855535,
        customName = "Doctor Buffer"
    },
    -- moenia
    {
        key = "moenia_medcenter",
        planet = "naboo",
        x = -18.57,
        z = 0.26,
        y = 3.02,
        heading = 0,
        cell = 1717506,
        customName = "Doctor Buffer"
    },
    -- theed
    {
        key = "theed_medcenter",
        planet = "naboo",
        x = -18.46,
        z = 0.26,
        y = 3.39,
        heading = 0,
        cell = 1697364,
        customName = "Doctor Buffer"
    },
    -- mos eisly
    {
        key = "mos_eisley_medcenter",
        planet = "tatooine",
        x = 7.45,
        z = 0.18,
        y = 2.29,
        heading = 180,
        cell = 9655496,
        customName = "Doctor Buffer"
    }
}

SmartDoctorConfig._spawn_registry_prefix = SmartDoctorConfig._spawn_registry_prefix or "SmartDoctorBuffer:spawned:"

-- Optional dialogue module hook
local SmartDoctorDialogue = nil
do
    local ok, dlg = pcall(require, "custom_scripts.smart_doctor_dialogue")
    if ok and dlg then SmartDoctorDialogue = dlg end
end

-- ========= Helpers =========
local function logInfo(msg) AiLogger.info("doctor", msg) end
local function logWarn(msg) AiLogger.warn("doctor", msg) end
local function logDebug(msg) AiLogger.debug("doctor", msg) end
local function nowSec() return os.time() end

local function spawnRegistryKey(sp)
    return SmartDoctorConfig._spawn_registry_prefix .. tostring(sp.key or "")
end

local function getSpawnedDoctorId(sp)
    if sp == nil or sp.key == nil or sp.key == "" then return 0 end
    return readData(spawnRegistryKey(sp)) or 0
end

local function setSpawnedDoctorId(sp, did)
    if sp == nil or sp.key == nil or sp.key == "" then return end
    writeData(spawnRegistryKey(sp), did or 0)
end
-- ===== Persistence (C++ datastore) =====
local STATE_IDLE        = 0
local STATE_NEGOTIATING = 1
local STATE_BUFFING     = 2
local STATE_PAUSED      = 3

local function stateToInt(s)
    if s == "NEGOTIATING" then return STATE_NEGOTIATING end
    if s == "BUFFING" then return STATE_BUFFING end
    if s == "PAUSED" then return STATE_PAUSED end
    return STATE_IDLE
end

local function intToState(i)
    if i == STATE_NEGOTIATING then return "NEGOTIATING" end
    if i == STATE_BUFFING then return "BUFFING" end
    if i == STATE_PAUSED then return "PAUSED" end
    return "IDLE"
end

local function k(did, suffix)
    return "SmartDoctorBuffer:" .. tostring(did) .. ":" .. suffix
end

local function persistSave(did, st)
    if did == 0 or st == nil then return end

    writeData(k(did, "state"), stateToInt(st.state))

    local negPid = (st.negotiating and st.negotiating.playerId) or 0
    writeData(k(did, "negPid"), negPid)

    local negExp = (st.negotiating and st.negotiating.expiresAtSec) or 0
    writeData(k(did, "negExp"), negExp)

    local curPid = (st.current and st.current.playerId) or 0
    writeData(k(did, "curPid"), curPid)

    local step = (st.current and st.current.stepIndex) or 0
    writeData(k(did, "step"), step)

    local pauseStart = (st.current and st.current.pauseStartSec) or 0
    writeData(k(did, "pauseStart"), pauseStart)

    -- The sim-bot request token (generation + expiry) must survive across the
    -- per-thread Lua states that run the deferred buff steps, so persist it with
    -- the current target. botTokens (in-memory) is only valid same-thread.
    local curBotGen = (st.current and st.current.botGeneration) or 0
    writeData(k(did, "curBotGen"), curBotGen)

    local curBotExp = (st.current and st.current.botExpiresAtSec) or 0
    writeData(k(did, "curBotExp"), curBotExp)
end

local function persistLoad(did, st)
    if did == 0 or st == nil then return end

    local s = readData(k(did, "state")) or 0
    st.state = intToState(s)

    local negPid = readData(k(did, "negPid")) or 0
    local negExp = readData(k(did, "negExp")) or 0
    if negPid ~= 0 then
        st.negotiating = { playerId = negPid, expiresAtSec = negExp }
    else
        st.negotiating = nil
    end

    local curPid = readData(k(did, "curPid")) or 0
    local step = readData(k(did, "step")) or 0
    local pauseStart = readData(k(did, "pauseStart")) or 0
    local curBotGen = readData(k(did, "curBotGen")) or 0
    local curBotExp = readData(k(did, "curBotExp")) or 0
    if curPid ~= 0 then
        st.current = {
            playerId = curPid,
            stepIndex = (step ~= 0 and step or 1),
            pauseStartSec = (pauseStart ~= 0 and pauseStart or nil),
            lastValidSec = nowSec(),
            botGeneration = (curBotGen ~= 0 and curBotGen or nil),
            botExpiresAtSec = (curBotExp ~= 0 and curBotExp or nil),
        }
    else
        st.current = nil
    end
end

local function getId(pObj)
    if pObj == nil then return 0 end
    return SceneObject(pObj):getObjectID()
end

local function getCreatureById(objectID)
    if objectID == nil or objectID == 0 then return nil end
    local pObj = getSceneObject(objectID)
    if pObj == nil then return nil end
    if not SceneObject(pObj):isCreatureObject() then return nil end
    return pObj
end

local function isSimPlayerBot(pObj)
    if pObj == nil then return false end

    local ok, isSimBot = pcall(function()
        return AiAgent(pObj):isSimPlayerBot()
    end)

    return ok and isSimBot == true
end

local function getPlayerName(pPlayer)
    if pPlayer == nil then return "someone" end
    local c = CreatureObject(pPlayer)
    local n = c:getFirstName()
    if n == nil or n == "" then return "friend" end
    return n
end

local function getDoctorName(pDoctor)
    if pDoctor == nil then return SmartDoctorConfig.doctor_custom_name end
    local name = CreatureObject(pDoctor):getFirstName()
    if name == nil or name == "" then
        return SmartDoctorConfig.doctor_custom_name
    end
    return name
end

local function say(pDoctor, text)
    if pDoctor == nil or text == nil or text == "" then return end
    spatialChat(pDoctor, text)
end

local function inRange(pDoctor, pPlayer)
    if pDoctor == nil or pPlayer == nil then return false end
    if SceneObject(pDoctor).isInRangeWithObject ~= nil then
        return SceneObject(pDoctor):isInRangeWithObject(pPlayer, SmartDoctorConfig.max_range)
    end
    local dist = SceneObject(pDoctor):getDistanceTo(pPlayer)
    return dist ~= nil and dist <= SmartDoctorConfig.max_range
end

local function isValidBuffTarget(pDoctor, pPlayer)
    --logInfo("isValidBuffTarget")
    if pDoctor == nil or pPlayer == nil then return false, "invalid" end
    if CreatureObject(pDoctor):isDead() then return false, "doctor_dead" end
    if CreatureObject(pPlayer):isDead() then return false, "player_dead" end
    if not inRange(pDoctor, pPlayer) then return false, "out_of_range" end
    if CreatureObject(pPlayer):isInCombat() then return false, "in_combat" end
    --logInfo("isValidBuffTarget true")
    return true, "ok"
end

--local function faceTarget(pDoctor, pPlayer)
--    if not SmartDoctorConfig.face_target then return end
--    if pDoctor == nil or pPlayer == nil then return end
--    if SceneObject(pDoctor).faceObject ~= nil then
--        SceneObject(pDoctor):faceObject(pPlayer)
--    end
--end

-- Re-resolve the target from OID before calling faceObject to avoid SIGSEGV
local function faceTarget(pDoctor, pPlayer)
    if not SmartDoctorConfig.face_target then return end
    if pDoctor == nil or pPlayer == nil then return end

    -- get ids (these can exist even if the underlying pointer is not currently valid)
    local did = 0
    local pid = 0

    local okDid, vDid = pcall(function() return SceneObject(pDoctor):getObjectID() end)
    if okDid and vDid then did = vDid end

    local okPid, vPid = pcall(function() return SceneObject(pPlayer):getObjectID() end)
    if okPid and vPid then pid = vPid end

    if did == 0 or pid == 0 then return end

    -- IMPORTANT: resolve fresh pointers from the IDs
    local pDoctorLive = getSceneObject(did)
    local pPlayerLive = getSceneObject(pid)

    -- If either isn't actually loaded/valid, DO NOT call faceObject (Core3 can segfault)
    if pDoctorLive == nil or pPlayerLive == nil then
        return
    end

    -- Extra safety: only face real scene objects
    local okFace = pcall(function()
        if SceneObject(pDoctorLive).faceObject ~= nil then
            SceneObject(pDoctorLive):faceObject(pPlayerLive)
        end
    end)

    if not okFace then
        -- swallow: pcall protects Lua-side errors, but we avoid the C++ null case above
        return
    end
end

local function normalizeMsg(msg)
    if msg == nil then return "" end
    msg = string.lower(msg)
    msg = msg:gsub("^%s+", ""):gsub("%s+$", "")
    logDebug("normalizeMsg player says: " .. tostring(msg))
    return msg
end

local function isBuffRequest(msg)
    --logInfo("isBuffRequest")
    msg = normalizeMsg(msg)
    if msg == "" then return false end
    if string.find(msg, "need a buff") then return true end
    if string.find(msg, "want a buff") then return true end
    if string.find(msg, "buff please") then return true end
    if string.find(msg, "doc buffs") then return true end
    if string.find(msg, "doctor buff") then return true end
    if msg == "buff" or msg == "buffs" then return true end
    if string.find(msg, "can i get") and string.find(msg, "buff") then return true end
    --logInfo("isBuffRequest false")
    return false
end

local function isConfirm(msg)
    msg = normalizeMsg(msg)
    if msg == "" then return false end
    return (msg == "yes" or msg == "y" or msg == "ok" or msg == "okay" or msg == "do it" or msg == "sure" or msg == "sounds good" or msg == "yep")
end

local function isDecline(msg)
    msg = normalizeMsg(msg)
    if msg == "" then return false end
    return (msg == "no" or msg == "n" or msg == "nope" or msg == "nah" or msg == "dont" or msg == "don't" or msg == "never mind" or msg == "nevermind")
end

local function isCancel(msg)
    msg = normalizeMsg(msg)
    if msg == "" then return false end
    return (msg == "cancel" or msg == "stop" or msg == "abort" or msg == "nevermind" or msg == "never mind")
end

-- ========= HARDENING WRAPPERS =========
-- These prevent C++ exceptions caused by bad args.
local function safeCreateEvent(delayMs, screenplayName, methodName, pObj, arg)
    local ms = tonumber(delayMs) or 0
    if ms < 1 then
        logWarn("safeCreateEvent SKIP: " .. tostring(methodName) .. " delayMs=" .. tostring(delayMs))
        return
    end
    local args = arg
    if args == nil then args = "" end
    if type(args) ~= "string" then args = tostring(args) end
    --logInfo("createEvent")
    createEvent(ms, screenplayName, methodName, pObj, args)
end

local function safeSetCustomObjectName(pMob, name)
    if pMob == nil then return end
    local n = name
    if n == nil or n == "" then n = SmartDoctorConfig.doctor_custom_name end
    if type(n) ~= "string" then n = tostring(n) end

    -- IMPORTANT: do NOT call setCustomObjectName(name, true) here.
    -- That signature is a common cause of IllegalArgumentException in some Core3 bindings.
    CreatureObject(pMob):setCustomObjectName(n)
end

-- ========= Deterministic flavor / LLM slots =========
local function buildSlots(pDoctor, pPlayer, doctorState, extra)
    local slots = {
        playerName = getPlayerName(pPlayer),
        doctorName = getDoctorName(pDoctor),
        price = tostring(SmartDoctorConfig.price),
        queuePos = extra and extra.queuePos and tostring(extra.queuePos) or "0",
        etaSeconds = extra and extra.etaSeconds and tostring(extra.etaSeconds) or "0",
        currentTargetName = extra and extra.currentTargetName or "",
    }
    return slots
end

local function pickDeterministicLine(key, slots, memoryTopic)
    --logInfo("pickDeterministicLine")
    if key == "quote" then
        return string.format("%s, that'll be %s credits for a full set. Sound good?", slots.playerName, slots.price)
    elseif key == "busy" then
        if slots.currentTargetName ~= nil and slots.currentTargetName ~= "" then
            return string.format("I'm buffing %s right now you're #%s in line. ETA ~%s seconds.",
                slots.currentTargetName, slots.queuePos, slots.etaSeconds)
        end
        return string.format("Hang tight you're #%s in line. ETA ~%s seconds.", slots.queuePos, slots.etaSeconds)
    elseif key == "start" then
        return string.format("Alright hold still %s. Starting your buffs.", slots.playerName)
    elseif key == "step" then
        return "Hold still."
    elseif key == "mid" then
        return "Half way done."
    elseif key == "almost" then
        return "Almost done."
    elseif key == "done" then
        if memoryTopic ~= nil and memoryTopic ~= "" then
            return string.format("All set, %s. How'd that %s go?", slots.playerName, memoryTopic)
        end
        return string.format("All set, %s. Stay safe out there!", slots.playerName)
    elseif key == "decline" then
        return "No worries come back if you change your mind."
    elseif key == "timeout" then
        return "Alright if you still want buffs, just ask again."
    elseif key == "poor" then
        return "Looks like you're short on credits. Come back when you've got enough."
    elseif key == "cancel" then
        return "Got it stopping."
    elseif key == "range" then
        return "Hey stay close, I can't buff you from over there."
    elseif key == "combat" then
        return "Not while you're fighting come back out of combat and we'll finish up."
    end
    return ""
end

local function dialogueLine(key, pDoctor, pPlayer, doctorState, extra, memoryTopic)
    --logInfo("dialogueLine " .. tostring(key))
    local slots = buildSlots(pDoctor, pPlayer, doctorState, extra)
    --logInfo("dialogueLine slots: " .. tostring(slots))

    if SmartDoctorDialogue ~= nil and SmartDoctorDialogue.getLine ~= nil then
        --logInfo("dialogueLine if SmartDoctorDialogue ~= nil and SmartDoctorDialogue.getLine ~= nil then")
        local ok, line = pcall(SmartDoctorDialogue.getLine, key, slots, memoryTopic)
        --logInfo("dialogueLine slots: " .. tostring(line))
        if ok and line ~= nil and line ~= "" then
            --logInfo("dialogueLine line: " .. tostring(line))
            return line
        end
    end
    --logInfo("dialogueLine pickDeterministicLine")
    return pickDeterministicLine(key, slots, memoryTopic)
end

-- ========= Ephemeral memory (per player) =========
local MEMORY_TTL_SEC = SmartDoctorConfig.memory_ttl_sec or 3600
--local playerMemory = {} -- [playerId] = { topic, ts }
--_G.SmartDoctorBuffer_State = _G.SmartDoctorBuffer_State or {}
_G.SmartDoctorBuffer_Memory = _G.SmartDoctorBuffer_Memory or {}

--local Doctor = _G.SmartDoctorBuffer_State
local playerMemory = _G.SmartDoctorBuffer_Memory

local function getMemoryTopic(playerId)
    local rec = playerMemory[playerId]
    if rec == nil then return "" end
    if nowSec() - (rec.ts or 0) > MEMORY_TTL_SEC then
        playerMemory[playerId] = nil
        return ""
    end
    return rec.topic or ""
end

local function setMemoryTopic(playerId, text)
    if playerId == nil or playerId == 0 then return end
    if text == nil then return end
    text = text:gsub("^%s+", ""):gsub("%s+$", "")
    if text == "" then return end
    if #text > 60 then text = string.sub(text, 1, 60) end
    playerMemory[playerId] = { topic = text, ts = nowSec() }
end

-- ========= State (per doctor objectID) =========
--local Doctor = {} -- [doctorId] = state table
if _G.SmartDoctorGlobalState == nil then
    _G.SmartDoctorGlobalState = {}
end

-- FIX: Make sure this matches the variable name above
local Doctor = _G.SmartDoctorGlobalState

local function getDoctorState(pDoctor)
    local did = getId(pDoctor)
    if did == 0 then return nil end

    if Doctor[did] == nil then
        --print("[SmartDoctor][DEBUG] getDoctorState INIT did=" .. tostring(did))
        Doctor[did] = {
            state = "IDLE",
            queue = {},
            queueSet = {},
            negotiating = nil,
            current = nil,
            lastRequestAt = {},
            lastSpokeAt = 0,
            botTokens = {},
        }
    end

    Doctor[did].botTokens = Doctor[did].botTokens or {}

    -- IMPORTANT: Always refresh from datastore so different Lua states stay in sync
    persistLoad(did, Doctor[did])

    --print("[SmartDoctor][DEBUG] getDoctorState SYNC did=" .. tostring(did) ..
    --      " state=" .. tostring(Doctor[did].state) ..
    --      " negPid=" .. tostring(Doctor[did].negotiating and Doctor[did].negotiating.playerId or 0) ..
    --      " curPid=" .. tostring(Doctor[did].current and Doctor[did].current.playerId or 0) ..
    --      " step=" .. tostring(Doctor[did].current and Doctor[did].current.stepIndex or 0))

    return Doctor[did]
end

local function removeFromQueue(st, playerId)
    if st == nil or playerId == nil or playerId == 0 then return end

    local newQ = {}
    for _, pid in ipairs(st.queue) do
        if pid ~= playerId then table.insert(newQ, pid) end
    end
    st.queue = newQ
    st.queueSet[playerId] = nil
end

local function isValidBotRequest(st, pDoctor, pPlayer)
    if not isSimPlayerBot(pPlayer) then return true end
    if st == nil or pDoctor == nil or pPlayer == nil then return false end

    local pid = getId(pPlayer)
    -- Prefer the token persisted with the current target (survives the
    -- per-thread Lua states that run deferred steps); fall back to the
    -- in-memory botTokens for same-thread pre-current checks.
    local generation = 0
    local expiresAtSec = 0
    if st.current ~= nil and st.current.playerId == pid and
            st.current.botGeneration ~= nil then
        generation = tonumber(st.current.botGeneration) or 0
        expiresAtSec = tonumber(st.current.botExpiresAtSec) or 0
    else
        local token = st.botTokens and st.botTokens[pid] or nil
        if token == nil then return false end
        generation = tonumber(token.generation) or 0
        expiresAtSec = tonumber(token.expiresAtSec) or 0
    end

    if generation == 0 or nowSec() > expiresAtSec then return false end

    local validTarget = isValidBuffTarget(pDoctor, pPlayer)
    return validTarget == true
end

local function popNextValidFromQueue(pDoctor, st)
    if st == nil then return nil end

    while #st.queue > 0 do
        local pid = table.remove(st.queue, 1)
        st.queueSet[pid] = nil
        local p = getCreatureById(pid)
        if p ~= nil then
            if inRange(pDoctor, p) and not CreatureObject(p):isDead() then
                return pid
            end
        end
    end
    return nil
end

local function estimateEtaSeconds(st, queuePos)
    local perStep = math.floor(SmartDoctorConfig.step_delay_ms / 1000)
    if perStep < 1 then perStep = 1 end

    local remainingCurrent = 0
    if st.current ~= nil and st.current.stepIndex ~= nil then
        remainingCurrent = (SmartDoctorConfig.total_steps - st.current.stepIndex + 1)
        if remainingCurrent < 0 then remainingCurrent = 0 end
    end

    local ahead = queuePos - 1
    if ahead < 0 then ahead = 0 end

    local totalSteps = remainingCurrent + (ahead * SmartDoctorConfig.total_steps)
    return totalSteps * perStep
end

-- ========= Credits =========
local function tryChargePlayer(pDoctor, pPlayer, price)
    --logInfo("tryChargePlayer")
    if pPlayer == nil then return false, "invalid" end
    if isSimPlayerBot(pPlayer) then return true, "sim_bot" end
    --local ghost = CreatureObject(pPlayer):getPlayerObject()
    --if ghost == nil then return false, "invalid" end
    if (CreatureObject(pPlayer):getCashCredits() < price) then
        --logInfo("tryChargePlayer insufficient")
        return false, "insufficient" 
    end

    CreatureObject(pPlayer):subtractCashCredits(price)

	CreatureObject(pPlayer):sendSystemMessage("You successfully purchase a Buff for " .. price .. " credits.")
    --logInfo("tryChargePlayer ok")
    return true, "ok"
end

local function applyBuffStep(pDoctor, pPlayer, stepKey)
    if pDoctor == nil or pPlayer == nil then return false end
    if stepKey == nil or stepKey == "" then return false end

    CreatureObject(pDoctor):doAnimation("heal_other")
    return AiAgentBridge.applyMedicalBuffStep(pDoctor, pPlayer, stepKey)
end

local startBuffingNow

local function advanceToNextTarget(pDoctor, st)
    if pDoctor == nil or st == nil then return end
    local did = getId(pDoctor)
    if did == 0 then return end

    st.negotiating = nil
    st.current = nil
    st.state = "IDLE"
    persistSave(did, st)  -- clear persisted current/negotiating first

    local nextPid = popNextValidFromQueue(pDoctor, st)
    if nextPid == nil then
        -- stay IDLE
        persistSave(did, st)
        return
    end

    st.state = "NEGOTIATING"
    st.negotiating = {
        playerId = nextPid,
        expiresAtSec = nowSec() + math.floor(SmartDoctorConfig.confirm_timeout_ms / 1000)
    }
    persistSave(did, st)

    local pNext = getCreatureById(nextPid)
    if pNext ~= nil then
        if isSimPlayerBot(pNext) then
            startBuffingNow(pDoctor, st, pNext)
            return
        end

        local line = dialogueLine("quote", pDoctor, pNext, st, nil, getMemoryTopic(nextPid))
        say(pDoctor, line)
        safeCreateEvent(SmartDoctorConfig.confirm_timeout_ms, "SmartDoctorBuffer", "onConfirmTimeout", pDoctor, tostring(nextPid))
    end
end

function SmartDoctorBuffer_onTick(pDoctor)
end

function SmartDoctorBuffer:tick(pDoctor)
    if pDoctor == nil then return end

    local doctor = CreatureObject(pDoctor)
    if doctor == nil then return end

    -- now use doctor:getZone() etc (on the wrapped object)
    local zone = doctor:getZoneName()
    if zone == nil then return end

    if doctor == nil then return end
    if doctor:isDead() then return end

    --logInfo("In the Tick")

    local st = getDoctorState(pDoctor)
    if st == nil then return end
    --logInfo("Tick: before the Buffing, pauses, idle")
    if st.state ~= "BUFFING" and st.state ~= "PAUSED" then return end
    if st.current == nil then st.state = "IDLE"; return end

    local pid = st.current.playerId
    local pPlayer = getCreatureById(pid)

    if pPlayer == nil then
        --logInfo("tick: current target invalid, cancelling and moving on")
        say(pDoctor, dialogueLine("cancel", pDoctor, nil, st))
        advanceToNextTarget(pDoctor, st)
        return
    end

    local ok, reason = isValidBuffTarget(pDoctor, pPlayer)
    if isSimPlayerBot(pPlayer) and (not ok or not isValidBotRequest(st, pDoctor, pPlayer)) then
        advanceToNextTarget(pDoctor, st)
        return
    end

    if ok then
        st.current.lastValidSec = nowSec()
        if st.state == "PAUSED" then
            st.state = "BUFFING"
            st.current.pauseStartSec = nil
            persistSave(getId(pDoctor), st)
            safeCreateEvent(250, "SmartDoctorBuffer", "applyNextStep", pDoctor, tostring(pid))
        end
        return
    end

    if st.state ~= "PAUSED" then
        --logInfo("PAUSED")
        st.state = "PAUSED"
        st.current.pauseStartSec = nowSec()
        persistSave(getId(pDoctor), st)
        if reason == "out_of_range" then
            say(pDoctor, dialogueLine("range", pDoctor, pPlayer, st))
        elseif reason == "in_combat" then
            say(pDoctor, dialogueLine("combat", pDoctor, pPlayer, st))
        end
    else
        local ps = st.current.pauseStartSec or nowSec()
        if (nowSec() - ps) * 1000 > SmartDoctorConfig.pause_grace_ms then
            --logInfo("pause grace exceeded; cancelling target " .. getPlayerName(pPlayer))
            say(pDoctor, dialogueLine("cancel", pDoctor, pPlayer, st))
            advanceToNextTarget(pDoctor, st)
            return
        end
    end

    safeCreateEvent(1000, "SmartDoctorBuffer", "tick", pDoctor, "")
end

function SmartDoctorBuffer:applyNextStep(pDoctor, playerIdStr)
    --logInfo("applyNextStep")
    local st = getDoctorState(pDoctor)
    if st == nil then return end
    if st.state ~= "BUFFING" then return end
    if st.current == nil then return end

    local pid = tonumber(playerIdStr) or 0
    if pid == 0 or st.current.playerId ~= pid then return end

    local pPlayer = getCreatureById(pid)
    if pPlayer == nil then
        --logInfo("applyNextStep: player invalid, moving on")
        advanceToNextTarget(pDoctor, st)
        return
    end

    local ok, reason = isValidBuffTarget(pDoctor, pPlayer)
    --logInfo("isValidBuffTarget: " .. tostring(ok))
    if isSimPlayerBot(pPlayer) then
        if not ok or not isValidBotRequest(st, pDoctor, pPlayer) then
            advanceToNextTarget(pDoctor, st)
            return
        end
    end

    if not ok then
        st.state = "PAUSED"
        st.current.pauseStartSec = nowSec()
        persistSave(getId(pDoctor), st)
        safeCreateEvent(1000, "SmartDoctorBuffer", "tick", pDoctor, "")
        return
    end

    --faceTarget(pDoctor, pPlayer)
    --logInfo("Would normally face target")

    local idx = st.current.stepIndex or 1
    if idx < 1 then idx = 1 end

    if idx > SmartDoctorConfig.total_steps then
        -- Mark session complete and CLEAR persisted current target
        st.state = "IDLE"
        st.current = nil
        st.negotiating = nil
        persistSave(getId(pDoctor), st)   -- <-- THIS is the missing piece
    
        local topic = getMemoryTopic(pid)
        say(pDoctor, dialogueLine("done", pDoctor, pPlayer, st, nil, topic))
    
        advanceToNextTarget(pDoctor, st)
        return
    end

    local stepKey = SmartDoctorConfig.buff_steps[idx]
    if stepKey == nil then
        --logWarn("Missing buff step at index " .. tostring(idx))
        st.current.stepIndex = idx + 1
        persistSave(getId(pDoctor), st)
        safeCreateEvent(250, "SmartDoctorBuffer", "applyNextStep", pDoctor, tostring(pid))
        return
    end

    if idx == 1 then
        say(pDoctor, dialogueLine("start", pDoctor, pPlayer, st))
    elseif idx == SmartDoctorConfig.total_steps then
        say(pDoctor, dialogueLine("almost", pDoctor, pPlayer, st))
    elseif idx == 4 then
        say(pDoctor, dialogueLine("mid", pDoctor, pPlayer, st))
    end

    local applied = applyBuffStep(pDoctor, pPlayer, stepKey)
    if not applied then
        --logWarn("applyBuffStep FAILED for stepKey=" .. tostring(stepKey) .. "   aborting buff session")
        say(pDoctor, "Uh… my medical droid interface is busted. Hang on a sec.")
        st.state = "IDLE"
        st.current = nil
        st.negotiating = nil
        persistSave(getId(pDoctor), st)
        return
    end

    st.current.stepIndex = idx + 1
    --print("st.current.stepIndex = idx + 1")
    persistSave(getId(pDoctor), st)
    safeCreateEvent(SmartDoctorConfig.step_delay_ms, "SmartDoctorBuffer", "applyNextStep", pDoctor, tostring(pid))
    safeCreateEvent(1000, "SmartDoctorBuffer", "tick", pDoctor, "")
end

-- ========= Negotiation / chat entry =========
function SmartDoctorBuffer:onConfirmTimeout(pDoctor, playerIdStr)
    local st = getDoctorState(pDoctor)
    if st == nil then return end
    if st.state ~= "NEGOTIATING" then return end
    if st.negotiating == nil then return end

    local pid = tonumber(playerIdStr) or 0
    if pid == 0 or st.negotiating.playerId ~= pid then return end

    if st.current ~= nil and st.current.playerId == pid then return end

    st.negotiating = nil
    st.state = "IDLE"
    persistSave(getId(pDoctor), st)

    local pPlayer = getCreatureById(pid)
    if pPlayer ~= nil then
        say(pDoctor, dialogueLine("timeout", pDoctor, pPlayer, st))
    else
        say(pDoctor, "Alright, if you still want buffs, just ask again.")
    end

    advanceToNextTarget(pDoctor, st)
end

local function throttleAllows(st, playerId)
    local last = st.lastRequestAt[playerId] or 0
    if (nowSec() - last) < SmartDoctorConfig.min_seconds_between_requests then
        return false
    end
    st.lastRequestAt[playerId] = nowSec()
    return true
end

local function alreadyQueuedOrCurrent(st, playerId)
    if st.current ~= nil and st.current.playerId == playerId then return true end
    if st.negotiating ~= nil and st.negotiating.playerId == playerId then return true end
    if st.queueSet[playerId] == true then return true end
    return false
end

local function enqueuePlayer(st, playerId)
    --logInfo("enqueuePlayer")
    if #st.queue >= SmartDoctorConfig.max_queue_length then
        --logInfo("enqueuePlayer: full")
        return false, "full"
    end
    if st.queueSet[playerId] == true then
        --logInfo("enqueuePlayer: already")
        return true, "already"
    end
    table.insert(st.queue, playerId)
    st.queueSet[playerId] = true
    --logInfo("enqueuePlayer: ok")
    return true, "ok"
end

startBuffingNow = function(pDoctor, st, pPlayer)
    --logInfo("startBuffingNow")
    local pid = getId(pPlayer)
    if pid == 0 then return end

    local simBot = isSimPlayerBot(pPlayer)
    if simBot and not isValidBotRequest(st, pDoctor, pPlayer) then
        advanceToNextTarget(pDoctor, st)
        return
    end

    --logInfo("startBuffingNow: pid <> 0")
    local okCharge = tryChargePlayer(pDoctor, pPlayer, SmartDoctorConfig.price)
    --logInfo("startBuffingNow: " .. tostring(okCharge))
    if not okCharge then
        --logInfo("startBuffingNow: not okChange")
        say(pDoctor, dialogueLine("poor", pDoctor, pPlayer, st))
        --logInfo("Insufficient credits for " .. getPlayerName(pPlayer))
        advanceToNextTarget(pDoctor, st)
        return
    end

    -- Carry the request token onto the current target so it persists across the
    -- per-thread Lua states that run the deferred buff steps (botTokens is only
    -- valid on the thread that received the request).
    local botToken = simBot and st.botTokens and st.botTokens[pid] or nil

    st.negotiating = nil
    st.state = "BUFFING"
    st.current = {
        playerId = pid,
        stepIndex = 1,
        charged = true,
        pauseStartSec = nil,
        lastValidSec = nowSec(),
        botGeneration = botToken and (tonumber(botToken.generation) or nil) or nil,
        botExpiresAtSec = botToken and
            (tonumber(botToken.expiresAtSec) or nil) or nil,
    }
    persistSave(getId(pDoctor), st)

    if not simBot then
        if not AiAgentBridge.wipeMedicalBuffs(pDoctor, pPlayer) then
            logWarn("wipeEnhanceBuffs failed or binding not present.")
        end
    end
    --logInfo("Starting buffs for " .. getPlayerName(pPlayer) .. " price=" .. tostring(SmartDoctorConfig.price))
    safeCreateEvent(250, "SmartDoctorBuffer", "applyNextStep", pDoctor, tostring(pid))
    safeCreateEvent(1000, "SmartDoctorBuffer", "tick", pDoctor, "")
end

-- Core entry called by aiGlobalChatHandler
function SmartDoctorBuffer:handleChat(pDoctor, pSpeaker, message)
    logInfo("handleChat")
    logDebug(string.format("LUACTX _G=%s SmartDoctorGlobalState=%s",
        tostring(_G),
        tostring(_G.SmartDoctorGlobalState)
    ))

    local did = getId(pDoctor)
    local sid = getId(pSpeaker)
    --print(string.format("[SmartDoctor][DEBUG] handleChat did=%s sid=%s msg='%s'", tostring(did), tostring(sid), tostring(message)))

    local st = getDoctorState(pDoctor)
    --print(string.format("[SmartDoctor][DEBUG] st=%s st.state=%s st.negotiating=%s",
    --    tostring(st), tostring(st and st.state), tostring(st and st.negotiating)))
    if pDoctor == nil or pSpeaker == nil then return false end
    if st == nil then return false end

    local msg = normalizeMsg(message)
    local speakerId = getId(pSpeaker)
    if speakerId == 0 then return false end
    logInfo("handleChat: speakerId <> 0")
    if not inRange(pDoctor, pSpeaker) then return false end
    logInfo("handleChat: inRange")

    if isCancel(msg) then
        logInfo("handleChat: isCancel")
        if st.current ~= nil and st.current.playerId == speakerId then
            say(pDoctor, dialogueLine("cancel", pDoctor, pSpeaker, st))
            logInfo("handleChat:Player cancelled mid-buff: " .. getPlayerName(pSpeaker))
            advanceToNextTarget(pDoctor, st)
            return true
        end

        if st.negotiating ~= nil and st.negotiating.playerId == speakerId then
            --logInfo("handleChat: st.negotiating: " .. getPlayerName(pSpeaker))
            say(pDoctor, dialogueLine("cancel", pDoctor, pSpeaker, st))
            st.negotiating = nil
            st.state = "IDLE"
            --logInfo("handleChat: Player cancelled negotiation: " .. getPlayerName(pSpeaker))
            advanceToNextTarget(pDoctor, st)
            return true
        end

        if st.queueSet[speakerId] == true then
            removeFromQueue(st, speakerId)
            say(pDoctor, "Alright you're off the list.")
            --logInfo("handleChat: Player removed from queue: " .. getPlayerName(pSpeaker))
            return true
        end

        return false
    end

    if st.current ~= nil and st.current.playerId == speakerId then
        if not isBuffRequest(msg) and not isConfirm(msg) and not isDecline(msg) and msg ~= "" then
            logInfo("before setMemoryTopic")
            setMemoryTopic(speakerId, msg)
        end
    end
    --logInfo("before NEGOTIATING")
    --print("[SmartDoctor][DEBUG] STATE=" .. tostring(st and st.state))
    --print("[SmartDoctor][DEBUG] NEGOTIATING=" .. tostring(st and st.negotiating))
    --print("[SmartDoctor][DEBUG] NEGOTIATING.playerId=" .. tostring(st and st.negotiating and st.negotiating.playerId))
    if st.state == "NEGOTIATING" and st.negotiating ~= nil and st.negotiating.playerId == speakerId then
        --logInfo("after if st.state == NEGOTIATING and st.negotiating ~= nil and st.negotiating.playerId == speakerId then")
        if isConfirm(msg) then
            --logInfo("after isConfirm")
            local ok, reason = isValidBuffTarget(pDoctor, pSpeaker)
            if not ok then
                if reason == "out_of_range" then
                    say(pDoctor, dialogueLine("range", pDoctor, pSpeaker, st))
                elseif reason == "in_combat" then
                    say(pDoctor, dialogueLine("combat", pDoctor, pSpeaker, st))
                else
                    say(pDoctor, "Hold up something's off. Try again in a second.")
                end
                return true
            end

            startBuffingNow(pDoctor, st, pSpeaker)
            return true
        end
        --logInfo("before isDecline")
        if isDecline(msg) then
            say(pDoctor, dialogueLine("decline", pDoctor, pSpeaker, st))
            st.negotiating = nil
            st.state = "IDLE"
            advanceToNextTarget(pDoctor, st)
            return true
        end
        --logInfo("before if msg ~= and not isBuffRequest(msg) then")
        if msg ~= "" and not isBuffRequest(msg) then
            setMemoryTopic(speakerId, msg)
            say(pDoctor, "Got it. So still want the buffs?")
            return true
        end

        return false
    end

    if not isBuffRequest(msg) then return false end
    --logInfo("isBuffRequest true")
    if not throttleAllows(st, speakerId) then
        --logInfo("throttleAllows BLOCKED")
        return true
    end
    --logInfo("throttleAllows ALLOWED")

    if alreadyQueuedOrCurrent(st, speakerId) then
        --logInfo("alreadyQueuedOrCurrent true")
        if st.current ~= nil and st.current.playerId == speakerId then
            if st.state == "PAUSED" then
                say(pDoctor, "There you are hold still, picking back up.")
                safeCreateEvent(250, "SmartDoctorBuffer", "applyNextStep", pDoctor, tostring(speakerId))
            else
                say(pDoctor, "You're already being worked on, hold still.")
            end
        elseif st.negotiating ~= nil and st.negotiating.playerId == speakerId then
            say(pDoctor, "Just say 'yes' and I'll start.")
        else
            local pos = 0
            for i, pid in ipairs(st.queue) do
                if pid == speakerId then pos = i break end
            end
            if pos > 0 then
                local eta = estimateEtaSeconds(st, pos)
                say(pDoctor, string.format("You're still #%d in line. ETA ~%d seconds.", pos, eta))
            end
        end
        return true
    end

    if st.state == "IDLE" then
        --logInfo("IDLE true")
        st.state = "NEGOTIATING"
        st.negotiating = {
            playerId = speakerId,
            expiresAtSec = nowSec() + math.floor(SmartDoctorConfig.confirm_timeout_ms / 1000)
        }
        persistSave(did, st)
        --print(string.format("[SmartDoctor][DEBUG] AFTER NEGOTIATING persist did=%s state=%s negPid=%s",
        --    tostring(did), tostring(st.state), tostring(st.negotiating and st.negotiating.playerId)
        --))
        if isSimPlayerBot(pSpeaker) then
            startBuffingNow(pDoctor, st, pSpeaker)
            return true
        end

        say(pDoctor, dialogueLine("quote", pDoctor, pSpeaker, st, nil, getMemoryTopic(speakerId)))
        --faceTarget(pDoctor, pSpeaker)

        safeCreateEvent(SmartDoctorConfig.confirm_timeout_ms, "SmartDoctorBuffer", "onConfirmTimeout", pDoctor, tostring(speakerId))
        return true
    end

    local okEnq, reason = enqueuePlayer(st, speakerId)
    if not okEnq and reason == "full" then
        say(pDoctor, "I'm slammed right now line's full. Try again in a minute.")
        return true
    end

    local queuePos = #st.queue
    local currentName = ""
    if st.current ~= nil then
        local pCur = getCreatureById(st.current.playerId)
        if pCur ~= nil then currentName = getPlayerName(pCur) end
    elseif st.negotiating ~= nil then
        local pCur = getCreatureById(st.negotiating.playerId)
        if pCur ~= nil then currentName = getPlayerName(pCur) end
    end

    local eta = estimateEtaSeconds(st, queuePos)
    local extra = { queuePos = queuePos, etaSeconds = eta, currentTargetName = currentName }
    say(pDoctor, dialogueLine("busy", pDoctor, pSpeaker, st, extra, getMemoryTopic(speakerId)))
    return true
end

function SmartDoctorBuffer:botBuffRequest(pDoctor, argsString)
    if pDoctor == nil or argsString == nil then return false end

    local botOidString, generationString, deadlineString = string.match(
        tostring(argsString), "^([^:]+):([^:]+):([^:]+)$")
    local botOid = tonumber(botOidString) or 0
    local generation = tonumber(generationString) or 0
    local deadlineSec = tonumber(deadlineString) or 0
    if botOid == 0 or generation == 0 or deadlineSec == 0 then return false end
    if nowSec() > deadlineSec then return false end

    local pBot = getCreatureById(botOid)
    if pBot == nil or not isSimPlayerBot(pBot) then return false end

    local st = getDoctorState(pDoctor)
    if st == nil then return false end

    local stored = st.botTokens[botOid]
    if stored ~= nil and generation < (tonumber(stored.generation) or 0) then
        return false
    end

    st.botTokens[botOid] = {
        generation = generation,
        expiresAtSec = deadlineSec,
    }

    return SmartDoctorBuffer:handleChat(pDoctor, pBot, "need a buff") == true
end

function SmartDoctorBuffer:botCancel(pDoctor, argsString)
    if pDoctor == nil or argsString == nil then return false end

    local botOidString, generationString = string.match(
        tostring(argsString), "^([^:]+):([^:]+)$")
    local botOid = tonumber(botOidString) or 0
    local generation = tonumber(generationString) or 0
    if botOid == 0 or generation == 0 then return false end

    local st = getDoctorState(pDoctor)
    if st == nil then return false end

    -- Resolve the authoritative stored generation. The current target's token is
    -- persisted (survives thread-local Lua states); the in-memory botTokens is a
    -- same-thread fast path. Prefer the persisted current token so a stale cancel
    -- for generation N cannot abort a newer persisted request N+1 (code-review
    -- round 1).
    local storedGeneration = 0
    if st.current ~= nil and st.current.playerId == botOid and
            st.current.botGeneration ~= nil then
        storedGeneration = tonumber(st.current.botGeneration) or 0
    else
        local stored = st.botTokens[botOid]
        storedGeneration = stored and (tonumber(stored.generation) or 0) or 0
    end
    if generation < storedGeneration then return false end

    st.botTokens[botOid] = {
        generation = math.max(generation, storedGeneration) + 1,
        expiresAtSec = 0,
    }
    removeFromQueue(st, botOid)

    local wasActive = (st.current ~= nil and st.current.playerId == botOid) or
        (st.negotiating ~= nil and st.negotiating.playerId == botOid)
    if wasActive then
        advanceToNextTarget(pDoctor, st)
    else
        persistSave(getId(pDoctor), st)
    end

    return true
end

-- ========= Registration =========
--print("### [SmartDoctor][DEBUG] About to registerScreenPlay('SmartDoctorBuffer', true). _G.SmartDoctorBuffer=" .. tostring(SmartDoctorBuffer) .. " type=" .. type(SmartDoctorBuffer) .. " ###")
registerScreenPlay("SmartDoctorBuffer", true)

-- ========= Spawn =========
function SmartDoctorBuffer:start()
    --logInfo("SmartDoctorBuffer:start() entered. spawn_points type=" ..
    --    type(SmartDoctorConfig.spawn_points) ..
    --    " count=" .. tostring(SmartDoctorConfig.spawn_points and #SmartDoctorConfig.spawn_points or 0))

    if SmartDoctorConfig.spawn_points == nil or #SmartDoctorConfig.spawn_points == 0 then
        --logInfo("No spawn_points configured; SmartDoctorBuffer loaded (chat-driven) without spawning.")
        return
    end

    for i, sp in ipairs(SmartDoctorConfig.spawn_points) do
        local planet  = sp.planet
        local x       = sp.x or 0
        local z       = sp.z or 0
        local y       = sp.y or 0
        local heading = sp.heading or 0
        local cell    = sp.cell or 0

        --logInfo(string.format(
        --    "SpawnPoint[%d]: planet=%s template=%s respawn=%s x=%.2f z=%.2f y=%.2f heading=%s cell=%s customName=%s",
        --    i, tostring(planet), "smart_doctor_buffer", "0",
        --    x, z, y, tostring(heading), tostring(cell), tostring(sp.customName)
        --))

        local zoneEnabledVal = false
        local okZone, errZone = pcall(function()
            zoneEnabledVal = (planet ~= nil) and isZoneEnabled(planet) or false
        end)
        if not okZone then
            logWarn("SpawnPoint[" .. i .. "]: isZoneEnabled threw: " .. tostring(errZone))
            goto continue
        end
        if not zoneEnabledVal then
            logInfo("SpawnPoint[" .. i .. "]: zone not enabled yet for planet=" .. tostring(planet) .. " (skipping)")
            goto continue
        end

        -- Prevent duplicates if the screenplay reloads
        if sp.key ~= nil and sp.key ~= "" then
            local existingDid = getSpawnedDoctorId(sp)
            if existingDid ~= 0 then
                local existingObj = getSceneObject(existingDid)
                if existingObj ~= nil then
                    --logInfo("SpawnPoint[" .. i .. "] key=" .. tostring(sp.key) ..
                    --    " already spawned did=" .. tostring(existingDid) .. " (skipping)")
                    goto continue
                else
                    -- stale id (despawned/restart), clear and respawn
                    setSpawnedDoctorId(sp, 0)
                end
            end
        end

        local pMob = nil
        local okSpawn, errSpawn = pcall(function()
            pMob = spawnMobile(planet, "smart_doctor_buffer", 0, x, z, y, heading, cell)
        end)

        if not okSpawn then
            logWarn("SpawnPoint[" .. i .. "]: spawnMobile threw: " .. tostring(errSpawn))
            goto continue
        end

        --logInfo("SpawnPoint[" .. i .. "]: spawnMobile returned pMob=" .. tostring(pMob))

        if pMob == nil then
            logWarn("SpawnPoint[" .. i .. "]: Failed to spawn smart_doctor_buffer (pMob nil)")
            goto continue
        end

        -- record spawned id for reload-safety
        if sp.key ~= nil and sp.key ~= "" then
            setSpawnedDoctorId(sp, getId(pMob))
        end

        -- Name ONCE, using the safe wrapper (NO boolean arg).
        local desiredName = sp.customName or SmartDoctorConfig.doctor_custom_name
        --logInfo("SpawnPoint[" .. i .. "]: setting custom name to '" .. tostring(desiredName) .. "' (safe)")
        safeSetCustomObjectName(pMob, desiredName)

        ::continue::
    end
end
