local http = require("socket.http")
local ltn12 = require("ltn12") -- Helper for handling data streams
local json = require("cjson")   -- Helper for reading the AI's JSON format

local AiBrain = {}

-- logic: The URL of our Docker container
local brain_url = "http://ollama_brain:11434/api/generate"

function AiBrain.askPadawan(player_input)
    -- 1. Setup the instructions for the AI
    local payload = {
        model = "llama3.2",
        -- We give the AI a 'persona' so it knows how to act
        prompt = "You are a loyal Star Wars Padawan in a game. The player says: '" .. player_input .. "'. Reply briefly (under 20 words). If the player asks for a heal, say 'Yes Master, healing you now!'",
        stream = false -- We want the whole answer at once, not piece by piece
    }

    -- 2. Convert the table to JSON text
    local request_body = json.encode(payload)
    local response_body = {}

    -- 3. Send the request across the Docker network
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

    -- 4. Check if the Brain is alive
    if code ~= 200 then
        print("AI Error: Could not connect to Brain. Code: " .. tostring(code))
        return "I sense a disturbance in the Force... (AI Connection Failed)"
    end

    -- 5. Decode the answer
    local response_string = table.concat(response_body)
    local response_data = json.decode(response_string)

    return response_data.response
end

return AiBrain