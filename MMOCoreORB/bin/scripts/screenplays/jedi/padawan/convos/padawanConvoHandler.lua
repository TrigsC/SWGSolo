local ObjectManager = require("managers.object.object_manager")

padawanConvoHandler = conv_handler:new {}

print("###################################################")
print("CRITICAL DEBUG: padawanConvoHandler LOADED")
print("###################################################")

function padawanConvoHandler:getInitialScreen(pPlayer, pNpc, pConvTemplate)
    local convoTemplate = LuaConversationTemplate(pConvTemplate)
    return convoTemplate:getScreen("init")
end

function padawanConvoHandler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
    local screen = LuaConversationScreen(pConvScreen)
    local screenID = screen:getScreenID()
    local pClonedScreen = screen:cloneScreen()
    local clonedScreen = LuaConversationScreen(pClonedScreen)

    if (screenID == "init") then
        
        -- Use the ID 15 directly if SPATIALCHAT isn't found, but the Bartender uses SPATIALCHAT
        -- which is typically mapped to 15 in server setups. 
        local observerID = 15

        if (readData(SceneObject(pNpc):getObjectID() .. ":brain_active") == 1) then
            clonedScreen:setCustomDialogText("System: Neural Link is ALREADY active.\n(Chat with me in spatial chat)")
        else
            writeData(SceneObject(pNpc):getObjectID() .. ":brain_active", 1)
            
            -- ATTACH OBSERVER
            createObserver(observerID, "padawanConvoHandler", "notifySpatialChatReceived", pNpc)
            
            clonedScreen:setCustomDialogText("System: AI Neural Link Established.\n(I am now listening to spatial chat...)")
            print("[PADAWAN] Brain attached to NPC: " .. SceneObject(pNpc):getObjectID())
        end
    end

    return pClonedScreen
end

-- UPDATED FUNCTION SIGNATURE AND EXTRACTION METHOD
-- Matching the BartenderScreenPlay signature: (pNpc, pChatMessage, objectID)
function padawanConvoHandler:notifySpatialChatReceived(pNpc, pChatMessage, objectID)
    print("[PADAWAN] Made it to Padawan notifySpatialChatReceived")
    print("[PADAWAN] pNpc " .. tostring(pNpc))
    print("[PADAWAN]pChatMessage " .. tostring(pChatMessage))
    print("[PADAWAN] objectID " .. tostring(objectID))
    if (pNpc == nil or pChatMessage == nil) then return 0 end

    -- 1. EXTRACT MESSAGE (The User's Fix)
    -- We pass the userdata pointer to the global C++ helper function
    local spatialMsg = getChatMessage(pChatMessage)
    print("[PADAWAN] spatialMsg: " .. spatialMsg)

    if (spatialMsg == nil or spatialMsg == "") then 
        return 0 
    end

    -- 2. GET ORIGINATOR (From ID)
    -- The observer passes the Object ID of the speaker as the 3rd argument
    local pSpeaker = getSceneObject(objectID)
    
    if (pSpeaker == nil) then return 0 end

    -- Infinite Loop Protection
    if (SceneObject(pSpeaker):getObjectID() == SceneObject(pNpc):getObjectID()) then return 0 end

    print("[PADAWAN] Heard: " .. spatialMsg)

    -- 3. KEYWORD CHECK
    if string.find(string.lower(spatialMsg), "padawan") then
        print("[PADAWAN] Keyword Detected! Responding...")
        
        -- ECHO RESPONSE
        spatialChat(pNpc, "Yes Master? I heard: " .. spatialMsg)
        CreatureObject(pNpc):doAnimation("conversation_1")
        
        -- UNCOMMENT THIS TO ENABLE PYTHON WHEN READY
        --[[
        local safeMessage = string.gsub(spatialMsg, "\"", "")
        local pythonScript = "/home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py"
        local command = "python3.9 " .. pythonScript .. " \"" .. safeMessage .. "\""
        
        local handle = io.popen(command)
        if handle then
            local output = handle:read("*a")
            handle:close()
            if output and output ~= "" then
                spatialChat(pNpc, string.gsub(output, "\n", ""))
            end
        end
        ]]--
    end

    return 0
end