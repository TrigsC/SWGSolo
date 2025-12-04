local http = require("socket.http")
local ltn12 = require("ltn12") 
local json = require("cjson")

local AiBrain = {}

-- CONFIG
local brain_url = "http://ollama_brain:11434/api/generate"
local model_name = "llama3.2"

-- PRIVATE HELPER: Handles the raw HTTP request
local function sendToOllama(final_prompt, json_mode)
    local payload = {
        model = model_name,
        prompt = final_prompt,
        stream = false
    }

    -- If we need strictly structured data, tell Ollama to use JSON mode
    if json_mode then
        payload.format = "json"
    end

    local request_body = json.encode(payload)
    local response_body = {}

    local res, code, response_headers = http.request{
        url = brain_url,
        method = "POST",
        headers = {
            ["Content-Type"] = "application/json",
            ["Content-Length"] = #request_body
        },
        source = ltn12.source.string(request_body),
        sink = ltn12.sink.table(response_body)
    }

    if code ~= 200 then
        print("[AiBrain] Error: HTTP " .. tostring(code))
        return nil
    end

    local response_string = table.concat(response_body)
    
    -- Protected call to decode JSON to prevent server crashes on bad AI output
    local status, response_data = pcall(json.decode, response_string)
    
    if not status or not response_data or not response_data.response then
        print("[AiBrain] JSON Decode Error: " .. tostring(response_string))
        return nil
    end

    return response_data.response
end

--------------------------------------------------------------------------------
-- PUBLIC FUNCTION 1: Standard Chat (Flavor Text)
--------------------------------------------------------------------------------
function AiBrain.getChatResponse(player_input, npc_profile, player_context, npc_context)
    local system_instruction = npc_profile.system_prompt or "You are a Star Wars character."
    
    -- Global formatting rules for CHAT ONLY
    local formatting_rules = " Do not describe actions or use asterisks (*). Speak only the dialogue. Keep the response brief (under 2 sentences)."

    local full_prompt = system_instruction .. 
                        (npc_context and (" " .. npc_context) or "") .. 
                        (player_context and (" " .. player_context) or "") .. 
                        formatting_rules .. 
                        " The player says: '" .. player_input .. "'."

    local result = sendToOllama(full_prompt, false)
    return result or "..."
end

--------------------------------------------------------------------------------
-- PUBLIC FUNCTION 2: Recruiter Logic (Intent Classification)
--------------------------------------------------------------------------------
local systemPrompt = [[
    You are a Star Wars Rebel Recruiter.
    Current Player Stats: ]] .. player_stats_context .. [[
    
    Analyze the player's message and determine their intent.
    Valid intents: 
    - "promote" (Player asks for promotion)
    - "buy_armor" (Player wants weapons/armor)
    - "buy_furniture" (Player wants furniture)
    - "buy_structures" (Player wants installations/bases)
    - "buy_hirelings" (Player wants faction pets/troopers)          <-- NEW
    - "buy_schematics" (Player wants crafting schematics)           <-- NEW
    - "buy_uniforms" (Player wants clothing/uniforms)               <-- NEW
    - "check_war_status" (Player asks about the war score/control)  <-- NEW
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

    if result_raw then
        -- Decode the inner JSON content returned by the AI
        local status, result_table = pcall(json.decode, result_raw)
        if status then
            return result_table
        end
    end

    -- Fallback if AI fails
    return { intent = "chat", reply = "I'm having trouble understanding you, soldier." }
end

return AiBrain