-- 1. Include the base class (REQUIRED for the C++ bindings to work right)
local ObjectManager = require("managers.object.object_manager")

-- 2. Define the Handler inheriting from conv_handler
padawanConvoHandler = conv_handler:new {}

-- 3. Debug Print to prove file loaded
print("###################################################")
print("CRITICAL DEBUG: padawanConvoHandler LOADED")
print("###################################################")

-- 4. Initial Screen
function padawanConvoHandler:getInitialScreen(pPlayer, pNpc, pConvTemplate)
    local convoTemplate = LuaConversationTemplate(pConvTemplate)
    return convoTemplate:getScreen("init")
end

-- 5. Screen Handler (The Trigger)
function padawanConvoHandler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
    local screen = LuaConversationScreen(pConvScreen)
    local screenID = screen:getScreenID()
    local pConvScreen = screen:cloneScreen()
    local clonedConversation = LuaConversationScreen(pConvScreen)

    if (screenID == "init") then
        print("[PADAWAN] Initial Screen Triggered via conv_handler")

        -- Check if brain is active
        if (readData(SceneObject(pNpc):getObjectID() .. ":brain_active") == 1) then
             clonedConversation:setCustomDialogText("System: Neural Link is ALREADY active.\n(Chat with me in spatial chat)")
        else
            -- MARK AS ACTIVE
            writeData(SceneObject(pNpc):getObjectID() .. ":brain_active", 1)
            
            -- ATTACH THE LISTENER
            createObserver(SPATIALCHATSENT, "padawanConvoHandler", "notifySpatialChatReceived", pNpc)
            
            clonedConversation:setCustomDialogText("System: AI Neural Link Established.\n(I am now listening to spatial chat...)")
            print("[PADAWAN] Brain attached to NPC: " .. SceneObject(pNpc):getObjectID())
        end
    end

    return pConvScreen
end

-- 6. The Brain Logic (The Listener)
function padawanConvoHandler:notifySpatialChatReceived(pNpc, pObserver, pChatMessage)
    if (pNpc == nil or pChatMessage == nil) then return 0 end

    local pSpeaker = pChatMessage:getOriginator()
    if (pSpeaker == nil) then return 0 end

    -- Don't listen to myself
    if (SceneObject(pSpeaker):getObjectID() == SceneObject(pNpc):getObjectID()) then return 0 end

    local message = pChatMessage:getString()
    
    -- DEBUG PROOF
    print("[PADAWAN] Heard: " .. message)

    -- KEYWORD CHECK
    if string.find(string.lower(message), "padawan") then
        print("[PADAWAN] Keyword Detected! responding...")
        
        -- ECHO RESPONSE
        spatialChat(pNpc, "Yes Master? I heard: " .. message)
        CreatureObject(pNpc):doAnimation("conversation_1")
    end

    return 0
end