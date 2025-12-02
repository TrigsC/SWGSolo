local ObjectManager = require("managers.object.object_manager")
local AiBrain = require("custom_scripts.ai_brain")

padawanConvoHandler = conv_handler:new {}

print("###################################################")
print("CRITICAL DEBUG: padawanConvoHandler LOADED")
print("###################################################")
registerScreenPlay("padawanConvoHandler", true)

function padawanConvoHandler:start()
    print("###################################################")
    print("CRITICAL DEBUG: padawanConvoHandler START")
    print("###################################################")
end

function padawanConvoHandler:getInitialScreen(pPlayer, pNpc, pConvTemplate)
    local convoTemplate = LuaConversationTemplate(pConvTemplate)
    print("###################################################")
    print("CRITICAL DEBUG: padawanConvoHandler getInitialScreen")
    print("###################################################")
    return convoTemplate:getScreen("init")
end

function padawanConvoHandler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
    local screen = LuaConversationScreen(pConvScreen)
    local screenID = screen:getScreenID()
    local pClonedScreen = screen:cloneScreen()
    local clonedScreen = LuaConversationScreen(pClonedScreen)

    if (screenID == "init") then
        
        -- EVENT ID 50 = SPATIALCHATSENT
        -- This event triggers on the PLAYER when they send a message.
        -- We use this because the Pet's listener (90) is often blocked by Pet AI.
        local observerID = 50 
        if (SPATIALCHATSENT ~= nil) then observerID = SPATIALCHATSENT end

        local playerID = SceneObject(pPlayer):getObjectID()
        local petID = SceneObject(pNpc):getObjectID()

        -- Check if we are already linked
        if (readData(playerID .. ":padawan_link_active") == 1) then
             -- Update the Pet ID (in case you called a different pet)
             writeData(playerID .. ":linked_padawan_id", petID)
             clonedScreen:setCustomDialogText("System: Neural Link UPDATED to this body.\n(Chat with me in spatial chat)")
        else
            -- MARK PLAYER AS ACTIVE
            writeData(playerID .. ":padawan_link_active", 1)
            writeData(playerID .. ":linked_padawan_id", petID)
            
            -- ATTACH OBSERVER TO THE PLAYER (pPlayer), NOT THE NPC
            createObserver(observerID, "padawanConvoHandler", "notifySpatialChatSent", pPlayer)
            
            clonedScreen:setCustomDialogText("System: AI Neural Link Established.\n(I am now listening to YOU...)")
            print("[PADAWAN] Observer attached to PLAYER: " .. playerID)
        end
    end

    return pClonedScreen
end

function padawanConvoHandler:notifySpatialChatSent(pPlayer, pChatMessage, nothing)
    
    if (pPlayer == nil or pChatMessage == nil) then return 0 end

    -- 1. DECODE MESSAGE
    local spatialMsg = getChatMessage(pChatMessage)
    if (spatialMsg == nil or spatialMsg == "") then return 0 end

    -- 2. FIND THE PADAWAN
    -- We look up the Pet ID we stored on the player during the conversation
    local playerID = SceneObject(pPlayer):getObjectID()
    local padawanID = readData(playerID .. ":linked_padawan_id")
    
    if (padawanID == 0) then return 0 end

    local pPadawan = getSceneObject(padawanID)

    -- 3. VALIDATION CHECKS
    -- Does the Padawan exist? Is it spawned? Is it close?
    if (pPadawan == nil) then 
        -- Optional: clean up data if pet is gone
        return 0 
    end

    if (not SceneObject(pPlayer):isInRangeWithObject(pPadawan, 20)) then
        -- Too far away to hear you
        return 0
    end

    -- DEBUG
    -- print("[PADAWAN] Player Spoke: " .. spatialMsg .. " | Padawan is listening.")

    -- 4. KEYWORD CHECK
    if string.find(string.lower(spatialMsg), "padawan") then
        print("[PADAWAN] Sending request to AI Brain: " .. spatialMsg)
        
        -- A. Ask the AI for a response
        -- This calls the function we wrote in ai_brain.lua
        local aiResponse = AiBrain.askPadawan(spatialMsg)

        -- B. Speak the AI's response in game
        spatialChat(pPadawan, aiResponse)
        CreatureObject(pPadawan):doAnimation("conversation_1")

        -- C. LOGIC: CHECK FOR HEAL
        -- We check if the PLAYER asked for a heal.
        if string.find(string.lower(spatialMsg), "heal") then
            print("[PADAWAN] Heal command detected! calling C++ Logic...")
            -- Note: pPadawan is the 'Agent', pPlayer is the 'Target'
            LuaAiAgent(pPadawan):healCreatureTarget(pPlayer)
        end
    end

    return 0
end

registerScreenPlay("padawanConvoHandler", true)