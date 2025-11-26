padawan_brain = {
}

-- 1. TRIGGER: Runs when the NPC is spawned or loaded
function padawan_brain:trigger(pObject)
    print("[PADAWAN-DEBUG] trigger() fired. Script attached to object.")
    
    if (pObject == nil) then
        print("[PADAWAN-DEBUG] ERROR: pObject is nil in trigger()")
        return 0
    end

    -- Attempt to attach the listener
    -- Note: 0 is the ID for SPATIALCHATRECEIVED
    createObserver(SPATIALCHATRECEIVED, "padawan_brain", "notifySpatialChatReceived", pObject)
    print("[PADAWAN-DEBUG] Observer attached to Object ID: " .. SceneObject(pObject):getObjectID())
    
    return 0
end

-- 2. CALLBACK: Runs when ANY chat is heard nearby
function padawan_brain:notifySpatialChatReceived(pObject, pObserver, pChatMessage)
    -- Uncomment this only if you want spam for every single chat message in the area
    -- print("[PADAWAN-DEBUG] Chat event received.")

    if (pObject == nil or pChatMessage == nil) then 
        return 0 
    end

    local pSpeaker = pChatMessage:getOriginator()
    if (pSpeaker == nil) then 
        return 0 
    end

    local speakerID = SceneObject(pSpeaker):getObjectID()
    local myID = SceneObject(pObject):getObjectID()
    local message = pChatMessage:getString()

    -- Filter out self-talk early to clean up logs
    if (speakerID == myID) then
        return 0
    end

    print("[PADAWAN-DEBUG] Heard message: '" .. message .. "' from SpeakerID: " .. speakerID)

    -- Check for the Keyword
    if string.find(string.lower(message), "padawan") then
        print("[PADAWAN-DEBUG] Keyword 'padawan' DETECTED. Preparing Python...")

        local safeMessage = string.gsub(message, "\"", "")
        local pythonScript = "/home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py"
        
        -- DEBUG: Check if file exists (simple Lua check)
        local f = io.open(pythonScript, "r")
        if f ~= nil then 
            io.close(f) 
            print("[PADAWAN-DEBUG] Python script file found.")
        else 
            print("[PADAWAN-DEBUG] ERROR: Python script NOT found at: " .. pythonScript)
            return 0
        end

        local command = "python3.9 " .. pythonScript .. " \"" .. safeMessage .. "\""
        print("[PADAWAN-DEBUG] Executing Command: " .. command)
        
        local handle = io.popen(command)
        
        if (handle == nil) then
            print("[PADAWAN-DEBUG] ERROR: io.popen returned nil.")
        else
            local output = handle:read("*a")
            handle:close()
            
            print("[PADAWAN-DEBUG] Raw Python Output: [" .. tostring(output) .. "]")

            if (output ~= nil and output ~= "") then
                local cleanOutput = string.gsub(output, "\n", "")
                spatialChat(pObject, cleanOutput)
                CreatureObject(pObject):doAnimation("conversation_1")
                print("[PADAWAN-DEBUG] Chat sent to game.")
            else
                print("[PADAWAN-DEBUG] WARNING: Python returned empty string.")
            end
        end
    else
        print("[PADAWAN-DEBUG] Keyword 'padawan' NOT found. Ignoring.")
    end

    return 0
end