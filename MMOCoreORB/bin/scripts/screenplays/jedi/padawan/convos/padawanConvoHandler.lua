padawanConvoHandler = Object:new {
}

function padawanConvoHandler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
    -- 1. Get the Screen ID
    local screen = LuaConversationScreen(pConvScreen)
    local screenID = screen:getScreenID()

    -- 2. The Injection Logic
    if (screenID == "init_chat") then
        -- This runs every time you right click him.
        
        -- Check if we already attached the observer to avoid duplicates
        if (readData(SceneObject(pNpc):getObjectID() .. ":brain_active") == 1) then
            screen:setCustomDialogText("I am still listening, Master.")
        else
            -- MARK AS ACTIVE
            writeData(SceneObject(pNpc):getObjectID() .. ":brain_active", 1)
            
            -- ATTACH THE LISTENER (The Brain)
            createObserver(SPATIALCHATRECEIVED, "padawanConvoHandler", "notifySpatialChatReceived", pNpc)
            
            screen:setCustomDialogText("Connection established. I am now listening to spatial chat.")
            print("[PADAWAN] Brain attached to NPC: " .. SceneObject(pNpc):getObjectID())
        end
    end

    return pConvScreen
end

-- 3. THE BRAIN LOGIC (Moved here from the old file)
function padawanConvoHandler:notifySpatialChatReceived(pNpc, pObserver, pChatMessage)
    if (pNpc == nil or pChatMessage == nil) then return 0 end

    local pSpeaker = pChatMessage:getOriginator()
    if (pSpeaker == nil) then return 0 end

    -- Don't listen to myself
    if (SceneObject(pSpeaker):getObjectID() == SceneObject(pNpc):getObjectID()) then return 0 end

    local message = pChatMessage:getString()
    
    -- Debug Print
    print("[PADAWAN] Heard: " .. message)

    if string.find(string.lower(message), "padawan") then
        local safeMessage = string.gsub(message, "\"", "")
        local pythonScript = "/home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py"
        local command = "python3.9 " .. pythonScript .. " \"" .. safeMessage .. "\""
        
        print("[PADAWAN] Running Python: " .. command)
        
        local handle = io.popen(command)
        if handle then
            local output = handle:read("*a")
            handle:close()
            if output and output ~= "" then
                spatialChat(pNpc, string.gsub(output, "\n", ""))
                CreatureObject(pNpc):doAnimation("conversation_1")
            end
        end
    end

    return 0
end