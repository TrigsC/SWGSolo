includeFile("ai/ai.lua")

padawan_brain = {
    -- This name must be unique
    aiName = "padawan_brain",
}

-- 1. TRIGGERS: When does this brain wake up?
function padawan_brain:trigger(pAgent)
    if (pAgent == nil) then return end
    
    -- Listen for "Spatial Chat" (Regular talking)
    createObserver(SPATIALCHATRECEIVED, "padawan_brain", "onSpatialChat", pAgent)
end

-- 2. THE LOGIC: What happens when someone talks?
function padawan_brain:onSpatialChat(pAgent, pGameState, pChatMessage)
    if (pAgent == nil or pChatMessage == nil) then return 0 end

    -- A. Get the Actors
    local pPlayer = pChatMessage:getOriginator() -- The person talking
    local message = pChatMessage:getString() -- What they said
    local npc = AiAgent(pAgent) -- The Padawan

    -- B. Safety Checks
    if (pPlayer == nil) then return 0 end
    
    -- Only listen to the Master (The owner of the pet)
    -- We get the owner ID from the Pet mechanics
    local ownerID = npc:getOwnerID()
    local speakerID = SceneObject(pPlayer):getObjectID()
    
    if (ownerID ~= speakerID) then
        -- Optional: Ignore strangers or have a generic "I only serve my master" reply
        return 0 
    end

    -- C. The "RAG" Hook (Placeholder for now)
    -- This is where we will eventually send the HTTP request to Python
    
    -- LOGIC: If the player mentions the NPC's name or says "Padawan"
    if (string.find(string.lower(message), "padawan") or string.find(string.lower(message), "apprentice")) then
        
        -- D. Simple Response (to prove it works)
        local response = "Yes, Master? I await your command."
        
        -- Make the NPC speak
        spatialChat(pAgent, response)
        
        -- Make the NPC do an animation
        npc:doAnimation("bow")
    end

    return 0
end