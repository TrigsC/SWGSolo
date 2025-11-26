-- Define the table (Must match the filename)
padawan_brain = {
}

-- 1. TRIGGER: This runs when the NPC loads into the world
function padawan_brain:trigger(pObject)
    if (pObject == nil) then return 0 end

    -- Attach a listener to THIS specific NPC. 
    -- SPATIALCHATRECEIVED (0) means "Notify me when someone talks nearby"
    createObserver(SPATIALCHATRECEIVED, "padawan_brain", "notifySpatialChatReceived", pObject)
    
    return 0
end

-- 2. CALLBACK: This runs when chat is heard
function padawan_brain:notifySpatialChatReceived(pObject, pObserver, pChatMessage)
    -- pObject = The Padawan (The NPC)
    -- pChatMessage = The message object

    if (pObject == nil or pChatMessage == nil) then return 0 end

    local pSpeaker = pChatMessage:getOriginator()
    if (pSpeaker == nil) then return 0 end
    
    -- OPTIONAL: Only listen to the owner?
    -- For now, let's let him talk to anyone to make testing easier.
    -- local ownerID = CreatureObject(pObject):getOwnerID()
    -- if (SceneObject(pSpeaker):getObjectID() ~= ownerID) then return 0 end

    -- Prevent the NPC from talking to itself (Infinite Loop protection)
    if (SceneObject(pSpeaker):getObjectID() == SceneObject(pObject):getObjectID()) then
        return 0
    end

    local message = pChatMessage:getString()

    -- 3. THE PYTHON HOOK (From your old code)
    -- Only trigger if they say "Padawan" or talk directly to it, otherwise it chats too much
    if string.find(string.lower(message), "padawan") then
        
        -- Sanitize the message so quotes don't break the command
        local safeMessage = string.gsub(message, "\"", "") 
        
        -- WARNING: io.popen blocks the server. It will lag slightly while Python thinks.
        local pythonScript = "/home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py"
        local command = "python3.9 " .. pythonScript .. " \"" .. safeMessage .. "\""
        
        local handle = io.popen(command)
        if (handle ~= nil) then
            local output = handle:read("*a")
            handle:close()

            -- Clean up output (remove newlines)
            if (output ~= nil and output ~= "") then
                output = string.gsub(output, "\n", "")
                
                -- Make the NPC speak the result
                spatialChat(pObject, output)
                
                -- Optional: Play an animation
                CreatureObject(pObject):doAnimation("conversation_1")
            end
        else
            print("PADAWAN BRAIN ERROR: Could not execute Python.")
        end
    end

    return 0
end