local AiBrain = {}

local DEFAULT_LLM_CONFIG = {
    enabled = false,
    url = nil,
    model = nil,
    timeoutSeconds = 3,
}

local Config = nil
do
    local ok, cfg = pcall(require, "custom_scripts.ai_config")
    if ok and cfg ~= nil then
        Config = cfg
    elseif AiConfig ~= nil then
        Config = AiConfig
    else
        Config = { llm = DEFAULT_LLM_CONFIG }
    end
end

local AiLogger = nil
do
    local ok, logger = pcall(require, "custom_scripts.ai_logger")
    if ok and logger ~= nil then
        AiLogger = logger
    else
        AiLogger = {
            warn = function() end,
            info = function() end,
            debug = function() end,
            trace = function() end
        }
    end
end

local http = nil
local ltn12 = nil
local json = nil

do
    local okHttp, httpModule = pcall(require, "socket.http")
    if okHttp then http = httpModule end

    local okLtn12, ltn12Module = pcall(require, "ltn12")
    if okLtn12 then ltn12 = ltn12Module end

    local okJson, jsonModule = pcall(require, "cjson")
    if okJson then json = jsonModule end
end

if http == nil then
    AiLogger.warn("llm", "socket.http unavailable; LLM responses will use fallback text.")
end
if ltn12 == nil then
    AiLogger.warn("llm", "ltn12 unavailable; LLM responses will use fallback text.")
end
if json == nil then
    AiLogger.warn("llm", "cjson unavailable; LLM responses will use fallback text.")
end

local loggedLlmDisabled = false
local loggedMissingDependencies = false

local function getLlmConfig()
    local llm = (Config and Config.llm) or {}
    local url = llm.url or DEFAULT_LLM_CONFIG.url
    local model = llm.model or DEFAULT_LLM_CONFIG.model
    return {
        enabled = (llm.enabled ~= false and url ~= nil and model ~= nil),
        url = url,
        model = model,
        timeoutSeconds = tonumber(llm.timeoutSeconds) or DEFAULT_LLM_CONFIG.timeoutSeconds,
    }
end

local function fallbackChatResponse()
    return "..."
end

local function fallbackRecruiterIntent()
    return { intent = "chat", reply = "I'm having trouble understanding you, soldier." }
end

local function dependenciesAvailable()
    return http ~= nil and ltn12 ~= nil and json ~= nil
end

local function logLlmDisabledOnce()
    if loggedLlmDisabled then return end
    loggedLlmDisabled = true
    AiLogger.debug("llm", "LLM disabled or missing URL/model; using deterministic fallback.")
end

local function logMissingDependenciesOnce()
    if loggedMissingDependencies then return end
    loggedMissingDependencies = true
    AiLogger.warn("llm", "LLM dependencies unavailable; using deterministic fallback.")
end

-- PRIVATE HELPER: Handles the raw HTTP request
local function sendToOllama(final_prompt, json_mode)
    local llmConfig = getLlmConfig()
    if not llmConfig.enabled then
        logLlmDisabledOnce()
        return nil
    end

    if not dependenciesAvailable() then
        logMissingDependenciesOnce()
        return nil
    end

    local payload = {
        model = llmConfig.model,
        prompt = final_prompt,
        stream = false
    }

    -- If we need strictly structured data, tell Ollama to use JSON mode
    if json_mode then
        payload.format = "json"
    end

    local okEncode, request_body = pcall(json.encode, payload)
    if not okEncode or request_body == nil then
        AiLogger.warn("llm", "Failed to encode Ollama request payload.")
        return nil
    end

    local response_body = {}

    if llmConfig.timeoutSeconds ~= nil and llmConfig.timeoutSeconds > 0 then
        pcall(function() http.TIMEOUT = llmConfig.timeoutSeconds end)
    end

    local okRequest, res, code, response_headers = pcall(http.request, {
        url = llmConfig.url,
        method = "POST",
        headers = {
            ["Content-Type"] = "application/json",
            ["Content-Length"] = #request_body
        },
        source = ltn12.source.string(request_body),
        sink = ltn12.sink.table(response_body)
    })

    if not okRequest then
        AiLogger.warn("llm", "Ollama HTTP request failed: " .. tostring(res))
        return nil
    end

    if code ~= 200 then
        AiLogger.warn("llm", "Ollama HTTP response code " .. tostring(code) .. ".")
        return nil
    end

    local response_string = table.concat(response_body)
    
    -- Protected call to decode JSON to prevent server crashes on bad AI output
    local status, response_data = pcall(json.decode, response_string)
    
    if not status or not response_data or not response_data.response then
        AiLogger.warn("llm", "Failed to parse Ollama response JSON: " .. tostring(response_string):sub(1, 200))
        return nil
    end

    return response_data.response
end

--------------------------------------------------------------------------------
-- PUBLIC FUNCTION 1: Standard Chat (Flavor Text)
--------------------------------------------------------------------------------
function AiBrain.getChatResponse(player_input, npc_profile, player_context, npc_context)
    npc_profile = npc_profile or {}
    local system_instruction = npc_profile.system_prompt or "You are a Star Wars character."
    
    -- Global formatting rules for CHAT ONLY
    local formatting_rules = " Do not describe actions or use asterisks (*). Speak only the dialogue. Keep the response brief (under 2 sentences)."

    local full_prompt = system_instruction .. 
                        (npc_context and (" " .. npc_context) or "") .. 
                        (player_context and (" " .. player_context) or "") .. 
                        formatting_rules .. 
                        " The player says: '" .. player_input .. "'."

    local result = sendToOllama(full_prompt, false)
    return result or fallbackChatResponse()
end

--------------------------------------------------------------------------------
-- PUBLIC FUNCTION 2: Recruiter Logic (Intent Classification)
--------------------------------------------------------------------------------
function AiBrain.getRecruiterIntent(player_input, player_stats_context)
    local systemPrompt = [[
    You are a Star Wars Rebel Recruiter.
    Current Player Stats: ]] .. player_stats_context .. [[
    
    Analyze the player's message and determine their intent.
    Valid intents: 
    - "promote" (Player asks for promotion)
    - "buy_armor" (Player wants weapons/armor)
    - "buy_furniture" (Player wants furniture)
    - "buy_structures" (Player wants installations/bases)
    - "buy_hirelings" (Player wants faction pets/troopers)
    - "buy_schematics" (Player wants crafting schematics)
    - "buy_uniforms" (Player wants clothing/uniforms)
    - "check_war_status" (Player asks about the war score/control)
    - "go_overt" (Player wants to be Special Forces/Declared)
    - "go_covert" (Player wants to be Combatant/Hidden)
    - "go_on_leave" (Player wants to resign/go on leave/be civilian)
    - "chat" (General questions or if they don't qualify)

    CRITICAL RULES FOR "reply":
    1. If the intent is an action (promote, buy, go_overt, etc), your reply MUST be a neutral confirmation that you are processing the request.
    2. Do NOT roleplay the outcome (do not say "Here is the score"). The system will handle that.
    
    Return JSON ONLY with this format:
    { "intent": "intent_name", "reply": "Your in-character response" }
    ]]

    local full_prompt = systemPrompt .. " Player Input: " .. player_input
    
    -- Send with json_mode = true
    local result_raw = sendToOllama(full_prompt, true)

    if result_raw and json ~= nil then
        -- Decode the inner JSON content returned by the AI
        local status, result_table = pcall(json.decode, result_raw)
        if status then
            return result_table
        end

        AiLogger.warn("llm", "Failed to parse recruiter intent JSON: " .. tostring(result_raw):sub(1, 200))
    end

    -- Fallback if AI fails
    return fallbackRecruiterIntent()
end

--------------------------------------------------------------------------------
-- OPTIONAL: Doctor Flavor (Non-deterministic, no numbers allowed)
--------------------------------------------------------------------------------
function AiBrain.getDoctorFlavorLine(phase, slots, memoryTopic)
    -- slots: {playerName, doctorName, price, queuePos, etaSeconds, currentTargetName}
    -- IMPORTANT: we will instruct the model that numbers are provided and must not be invented.
    local systemPrompt = [[
        You are the NPC doctor speaking to the player in Star Wars Galaxies.
        You are NOT the player. The player is NOT the doctor.
        
        Always speak as the doctor (first-person).
        Address the player by their playerName when appropriate.
        Never call the player "Doc".
        Never output placeholder tokens like "PlayerName" or "doctorName".
        Never mention "system", "timeout", "AI", "LLM", or anything out-of-world.
        The Player will ask for a buff or enhancement, your response should include the appropriate price
        and you should ask if they are ok with that amount.
        
        CRITICAL: You MUST NOT invent any numbers, prices, queue positions, or ETAs.
        If you mention a price/queue/ETA, you must copy it EXACTLY from the provided slots.
        Speak only dialogue. No quotes. No asterisks. 1-2 short sentences.
        ]]

    local slotText = string.format(
        "SLOTS: playerName=%s, doctorName=%s, price=%s, queuePos=%s, etaSeconds=%s, currentTargetName=%s.",
        tostring(slots.playerName or ""),
        tostring(slots.doctorName or ""),
        tostring(slots.price or ""),
        tostring(slots.queuePos or ""),
        tostring(slots.etaSeconds or ""),
        tostring(slots.currentTargetName or "")
    )

    local memoryText = ""
    if memoryTopic and memoryTopic ~= "" then
        memoryText = "MEMORY_TOPIC: " .. memoryTopic .. "."
    end

    local full_prompt = systemPrompt ..
        " PHASE: " .. tostring(phase) .. ". " ..
        slotText .. " " .. memoryText

    local result = sendToOllama(full_prompt, false)
    return result or nil
end

return AiBrain
