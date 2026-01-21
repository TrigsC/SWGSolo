-- MMOCoreORB/bin/scripts/custom_scripts/smart_doctor_dialogue.lua
--
-- Smart Doctor dialogue helper.
-- IMPORTANT:
--   - This module must NEVER drive state transitions, pricing, queue, payment, or buff application.
--   - It returns a single line of dialogue only.
--   - It must NEVER invent numbers (price/queue/eta). If it mentions them, it must use the provided slots.
--
-- Usage:
--   local SmartDoctorDialogue = require("custom_scripts.smart_doctor_dialogue")
--   local line = SmartDoctorDialogue.getLine("quote", slots, memoryTopic)
--
-- slots:
--   {
--     playerName, doctorName, price, queuePos, etaSeconds, currentTargetName
--   }
--
-- memoryTopic: optional short string (ephemeral), e.g. "Tusken hunt"

local SmartDoctorDialogue = {}

-- Toggle LLM flavor usage.
-- If false or AiBrain isn't available, we use deterministic lines.
local USE_LLM_FLAVOR = true

local AiBrain = nil
do
    local ok, brain = pcall(require, "custom_scripts.ai_brain")
    -- print("Called custom_scripts.ai_brain")
    if ok and brain then
        --print("AiBrain = brain")
        AiBrain = brain
    end
end

local function safeStr(v)
    if v == nil then return "" end
    return tostring(v)
end

-- Deterministic fallback lines (always safe)
local function deterministic(key, slots, memoryTopic)
    local playerName = safeStr(slots.playerName)
    local price = safeStr(slots.price)
    local queuePos = safeStr(slots.queuePos)
    local eta = safeStr(slots.etaSeconds)
    local currentTargetName = safeStr(slots.currentTargetName)

    if key == "quote" then
        return string.format("%s, that'll be %s credits for a full set. Sound good?", playerName, price)
    elseif key == "busy" then
        if currentTargetName ~= "" then
            return string.format("I'm buffing %s right now you're #%s in line. ETA ~%s seconds.", currentTargetName, queuePos, eta)
        end
        return string.format("Hang tight you're #%s in line. ETA ~%s seconds.", queuePos, eta)
    elseif key == "start" then
        return string.format("Alright hold still %s. Starting your buffs.", playerName)
    elseif key == "step" then
        return "Hold still."
    elseif key == "almost" then
        return "Almost done."
    elseif key == "done" then
        if memoryTopic ~= nil and memoryTopic ~= "" then
            return string.format("All set, %s. How'd that %s go?", playerName, memoryTopic)
        end
        return string.format("All set, %s. Stay safe out there!", playerName)
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

-- LLM flavor (optional). Must not invent numbers.
local function llmFlavor(key, slots, memoryTopic)
    print("llmflavor: key: "..tostring(key) .. " price: " ..tostring(slots.price))
    if not USE_LLM_FLAVOR then return nil end
    if AiBrain == nil then return nil end
    if AiBrain.getDoctorFlavorLine == nil then return nil end
    print("llmflavor: past brain")

    -- Provide a "phase" label so the model knows the moment.
    local phase = key

    -- Hard guard: if any numeric slot is missing and this phase expects numbers, skip LLM.
    if (key == "quote" and (slots.price == nil or slots.price == "")) then return nil end
    if (key == "busy" and ((slots.queuePos == nil or slots.queuePos == "") or (slots.etaSeconds == nil or slots.etaSeconds == ""))) then return nil end

    local ok, line = pcall(AiBrain.getDoctorFlavorLine, phase, slots, memoryTopic or "")
    if not ok then return nil end
    if line == nil then return nil end

    line = tostring(line)
    print("llFlavor: Line " .. line)
    line = line:gsub("^%s+", ""):gsub("%s+$", "")
    print("llFlavor: Line " .. line)

    if line == "" then return nil end

    -- Safety filter: block obvious action formatting or multiline
    if string.find(line, "%*") then return nil end

    -- Reject quotes (your sample output included quotes)
    local trimmed = line:gsub("^%s+", ""):gsub("%s+$", "")
    if trimmed:match('^".*"$') then return nil end
    if trimmed:match("^'.*'$") then return nil end

    -- Reject common placeholder tokens or out-of-world words
    -- local lower = string.lower(line)
    -- if string.find(lower, "playername") or string.find(lower, "doctorname") then return nil end
    -- if string.find(lower, "system") or string.find(lower, "timeout") or string.find(lower, "ollama") or string.find(lower, "ai") then
    --     return nil
    -- end

    -- Reject calling player "doc"
    -- if string.find(lower, " doc") or string.match(lower, "^doc[%p%s]") then return nil end

    -- Extra safety: if phase includes numbers...
    local function collectDigits(s)
        local d = {}
        for digit in string.gmatch(s, "%d") do
            d[digit] = true
        end
        return d
    end

    local slotDigits = {}
    local function mergeDigits(s)
        for digit in string.gmatch(tostring(s or ""), "%d") do
            slotDigits[digit] = true
        end
    end

    mergeDigits(slots.price)
    mergeDigits(slots.queuePos)
    mergeDigits(slots.etaSeconds)

    if (key == "quote" or key == "busy") then
        local lineDigits = collectDigits(line)
        for digit, _ in pairs(lineDigits) do
            if not slotDigits[digit] then
                -- model introduced a digit we didn't provide → reject
                return nil
            end
        end
    end

    return line
end

-- Public API
function SmartDoctorDialogue.getLine(key, slots, memoryTopic)
    -- Try LLM flavor first (if available and safe), then fall back deterministic.
    -- TODO: UNCOMMENT THIS LINE FOR LLM USE
    --local line = llmFlavor(key, slots, memoryTopic)
    --if line ~= nil and line ~= "" then
    --    return line
    --end
    return deterministic(key, slots, memoryTopic)
end

return SmartDoctorDialogue