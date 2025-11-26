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
    print("[PADAWAN] Notify screen Triggered!" .. screen)
    print("[PADAWAN] Notify screenID Triggered!" .. screenID)

    if (screenID == "init") then
        
        -- DEBUG CHECK: Is the constant nil?
        if (SPATIALCHATRECEIVED == nil) then
            print("CRITICAL ERROR: SPATIALCHATRECEIVED is nil! Defaulting to 15.")
            SPATIALCHATRECEIVED = 15 -- Force the value (15 is standard for Chat Received)
        else
            print("DEBUG: SPATIALCHATRECEIVED Value is: " .. tostring(SPATIALCHATRECEIVED))
        end

        if (readData(SceneObject(pNpc):getObjectID() .. ":brain_active") == 1) then
            screen:setCustomDialogText("System: Neural Link is ALREADY active.")
        else
            writeData(SceneObject(pNpc):getObjectID() .. ":brain_active", 1)
            
            -- NOW CREATE IT
            createObserver(SPATIALCHATRECEIVED, "padawanConvoHandler", "notifySpatialChatReceived", pNpc)
            
            screen:setCustomDialogText("System: AI Neural Link Established.")
            print("[PADAWAN] Brain attached to NPC: " .. SceneObject(pNpc):getObjectID())
        end
    end

    return pConvScreen
end

-- 6. The Brain Logic (The Listener)
function padawanConvoHandler:notifySpatialChatReceived(pNpc, pObserver, pChatMessage)
    print("[PADAWAN] Notify pNpc Triggered!" .. pNpc)
    print("[PADAWAN] Notify pChatMessage Triggered!" .. pChatMessage)

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