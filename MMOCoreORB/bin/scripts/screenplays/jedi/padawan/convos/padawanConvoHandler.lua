local ObjectManager = require("managers.object.object_manager")

padawanConvoHandler = Object:new {
}

-- 1. REQUIRED: Tells the engine which screen to start with
--function padawanConvoHandler:getInitialScreen(pPlayer, pNpc, pConvTemplate)
--    local convoTemplate = LuaConversationTemplate(pConvTemplate)
--    return convoTemplate:getScreen("init")
--end

-- 2. REQUIRED: Tells the engine what to do when options are clicked
-- (We were missing this one, causing the error!)
function padawanConvoHandler:getNextConversationScreen(pConversationTemplate, pPlayer, selectedOption, pConversationScreen)
    return self:getInitialScreen(pPlayer, pNpc, pConvTemplate)
end

-- 3. REQUIRED: Logic that runs when the screen opens
function padawanConvoHandler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
    local screen = LuaConversationScreen(pConvScreen)
    local screenID = screen:getScreenID()

    if (screenID == "init") then
        -- Logic to attach the AI Brain
        if (readData(SceneObject(pNpc):getObjectID() .. ":brain_active") == 1) then
            screen:setCustomDialogText("System: Neural Link is ALREADY active.\n(Chat with me in spatial chat)")
        else
            -- MARK AS ACTIVE
            writeData(SceneObject(pNpc):getObjectID() .. ":brain_active", 1)
            
            -- ATTACH THE LISTENER
            createObserver(SPATIALCHATSENT, "padawanConvoHandler", "notifySpatialChatReceived", pNpc)
            
            screen:setCustomDialogText("System: AI Neural Link Established.\n(I am now listening to spatial chat...)")
            print("[PADAWAN] Brain attached to NPC: " .. SceneObject(pNpc):getObjectID())
        end
    end

    return pConvScreen
end

-- 4. THE BRAIN LOGIC (The Listener)
function padawanConvoHandler:notifySpatialChatReceived(pNpc, pObserver, pChatMessage)
    if (pNpc == nil or pChatMessage == nil) then return 0 end

    local pSpeaker = pChatMessage:getOriginator()
    if (pSpeaker == nil) then return 0 end

    -- Don't listen to myself
    if (SceneObject(pSpeaker):getObjectID() == SceneObject(pNpc):getObjectID()) then return 0 end

    local message = pChatMessage:getString()
    
    -- Debug Print (Check your console for this!)
    print("[PADAWAN] Heard: " .. message)

    -- KEYWORD CHECK
    if string.find(string.lower(message), "padawan") then
        print("[PADAWAN] Keyword Detected! Sending response...")
        
        -- ECHO TEST (Python commented out for now)
        spatialChat(pNpc, "Yes Master? I heard you say: " .. message)
        CreatureObject(pNpc):doAnimation("conversation_1")
        
        -- PYTHON SECTION (Uncomment later when Echo Test works)
        --[[
        local safeMessage = string.gsub(message, "\"", "")
        local pythonScript = "/home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py"
        local command = "python3.9 " .. pythonScript .. " \"" .. safeMessage .. "\""
        
        local handle = io.popen(command)
        if handle then
            local output = handle:read("*a")
            handle:close()
            if output and output ~= "" then
                spatialChat(pNpc, string.gsub(output, "\n", ""))
                CreatureObject(pNpc):doAnimation("conversation_1")
            end
        end
        ]]--
    end

    return 0
end