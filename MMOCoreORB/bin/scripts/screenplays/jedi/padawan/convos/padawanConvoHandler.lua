local ObjectManager = require("managers.object.object_manager")

padawanConvoHandler = Object:new {
}
print("###################################################")
print("CRITICAL DEBUG: padawanConvoHandler LOADED")
print("###################################################")
-- ... (getInitialScreen and getNextConversationScreen remain the same) ...

function padawanConvoHandler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
    local screen = LuaConversationScreen(pConvScreen)
    local screenID = screen:getScreenID()

    if (screenID == "padawan_init") then
        
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

function padawanConvoHandler:notifySpatialChatReceived(pNpc, pObserver, pChatMessage)
    -- DEBUG: Prove we entered the function
    print("[PADAWAN] Notify Function Triggered!") 

    if (pNpc == nil or pChatMessage == nil) then return 0 end

    local message = pChatMessage:getString()
    print("[PADAWAN] Heard: " .. message)

    if string.find(string.lower(message), "padawan") then
        spatialChat(pNpc, "Yes Master? I heard: " .. message)
        CreatureObject(pNpc):doAnimation("conversation_1")
    end

    return 0
end