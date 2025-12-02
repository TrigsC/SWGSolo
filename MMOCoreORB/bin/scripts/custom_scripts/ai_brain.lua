local http = require("socket.http")
local ltn12 = require("ltn12") -- Helper for handling data streams
local json = require("cjson")   -- Helper for reading the AI's JSON format

local AiBrain = {}

-- logic: The URL of our Docker container
local brain_url = "http://ollama_brain:11434/api/generate"

-- Change function name from askPadawan to askBrain
function AiBrain.askBrain(player_input, npc_profile)
    
    -- Default to a generic prompt if the profile is missing
    local system_instruction = "You are a Star Wars character."
    if npc_profile and npc_profile.system_prompt then
        system_instruction = npc_profile.system_prompt
    end

    -- 1. Setup the instructions for the AI
    local payload = {
        model = "llama3.2",
        -- SYSTEM PROMPT: Who the NPC is
        -- USER PROMPT: What the player said
        prompt = system_instruction .. " The player says: '" .. player_input .. "'.",
        stream = false
    }

    -- ... (The rest of the HTTP code stays exactly the same) ...
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
        return "I have a bad feeling about this... (AI Error)"
    end

    local response_string = table.concat(response_body)
    local response_data = json.decode(response_string)

    return response_data.response
end

return AiBrain