ChatAIScreenPlay = ScreenPlay:new {
	numberOfActs = 1,

	screenplayName = "ChatAIScreenPlay",

	--states = {
	--	1, -- accept_task
	--	2, -- completed_task
	--	4, -- completed_quest
	--},
}



function ChatAIScreenPlay:start()
	if (isZoneEnabled("tatooine")) then
		Logger:log("Starting the CHAT AI Screenplay.", LT_INFO)
		print("**************** CHAT AI Screenplay !!!!!!! ")
		--self:spawnSceneObjects()
		self:spawnMobiles()

	end
end

function ChatAIScreenPlay:onPlayerLoggedIn(pPlayer)
	if (pPlayer == nil) then
		return
	end

    createObserver(SPATIALCHATSENT, "ChatAIScreenPlay", "notifyChatSent", pPlayer)
	print("OBSERVER CREATED")

    --Glowing:onPlayerLoggedIn(pPlayer)
    --self:registerObservers(pPlayer)
end

function ChatAIScreenPlay:onPlayerLoggedOut(pPlayer)
	if (pPlayer == nil) then
		return
	end

    dropObserver(SPATIALCHATSENT, "ChatAIScreenPlay", "notifyChatSent", pPlayer)
	print("OBSERVER DROPPED")
end

function ChatAIScreenPlay:notifyChatSent(pPlayer, pChatMessage)
	print("Made it to Notify")
	if (pPlayer == nil or not SceneObject(pPlayer):isPlayerCreature() or pChatMessage == nil) then
		return 0
	end

    local pID = readData("ai:c4p4")
	print(pID)
	local pSceneOb = getSceneObject(pID)
	print(pSceneOb)

	if (pSceneOb == nil or not SceneObject(pSceneOb):isInRangeWithObject(pPlayer, 10)) then
		print("NOPE1")
		return 0
	elseif (not SceneObject(pSceneOb):isInRangeWithObject(pPlayer, 3)) then
		print("NOPE2")
		CreatureObject(pPlayer):sendSystemMessage("You to far sucka")
		return 0
	end

    

	--local terminalID = readData("dwb:voiceControlTerminal")
	--local pTerminal = getSceneObject(terminalID)

	--if (pTerminal == nil or not SceneObject(pTerminal):isInRangeWithObject(pPlayer, 10)) then
	--	return 0
	--elseif (not SceneObject(pTerminal):isInRangeWithObject(pPlayer, 3)) then
	--	CreatureObject(pPlayer):sendSystemMessage("@dungeon/death_watch:too_far_from_terminal")
	--	return 0
	--end

	--local bombDroidHandlerID = readData("dwb:bombDroidHandler")
	--local terminalUserID = CreatureObject(pPlayer):getObjectID()
	--if (bombDroidHandlerID == 0) then
	--	writeData("dwb:bombDroidHandler", terminalUserID)
	--	writeData("dwb:bombDroidHandlerLastUse", os.time())
	--elseif (bombDroidHandlerID ~= terminalUserID) then
	--	local lastTerminalUse = readData("dwb:bombDroidHandlerLastUse")
	--	if (os.difftime(os.time(), lastTerminalUse) < 120) then
	--		CreatureObject(pPlayer):sendSystemMessage("@dungeon/death_watch:terminal_in_use")
	--		return 0
	--	else
	--		writeData("dwb:bombDroidHandler", terminalUserID)
	--		writeData("dwb:bombDroidHandlerLastUse", os.time())
	--	end
	--end

	local spatialMsg = getChatMessage(pChatMessage)

	if (spatialMsg == nil or spatialMsg == "") then
		printLuaError("Invalid spatial message from player " .. SceneObject(pPlayer):getDisplayedName())
		return 0
	end

	--local tokenizer = {}
	--for word in spatialMsg:gmatch("%w+") do table.insert(tokenizer, word) end
	--local spatialCommand = tokenizer[1]
	--if (spatialCommand == nil or spatialCommand == "") then
	--	printLuaError("Invalid spatial command from player " .. SceneObject(pPlayer):getDisplayedName() .. ", spatial message: " .. spatialMsg)
	--	return 0
	--end

    local command = "python3 /home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py \"" .. spatialMsg .. "\""
    --local command = python_location .. spatialMsg
    local handle = io.popen(command)
    local output = handle:read("*a")
    handle:close()

	--local greetingString = LuaStringIdChatParameter(OLD_MAN_GREETING_STRING)
	--greetingString:setTT(CreatureObject(pPlayer):getFirstName())
	spatialChat(pSceneOb, output)

    --CreatureObject(pPlayer):sendSystemMessage(" \\#FFFF00\\PYTHON:  \\#FFFFFF\\ " .. output)
    print("**************** PYTHON !!!!!!! " .. output)

	--writeStringData("dwb:bombDroidHandlerLastSpatialCommand", spatialCommand)
	--local moveDistance = 0
	--if (tokenizer[2] ~= nil) then
	--	moveDistance = tonumber(tokenizer[2])
	--	if (moveDistance == nil) then
	--		return 0
	--	end
	--	if (moveDistance > 10) then
	--		moveDistance = 10
	--	elseif (moveDistance <= 0) then
	--		moveDistance = 1
	--	end
	--end
	--writeData("dwb:bombDroidHandlerLastMoveDistance", moveDistance)
	--local bombDroidID = readData("dwb:bombDroid")
	--local pBombDroid = getSceneObject(bombDroidID)
	--createEvent(500, "DeathWatchBunkerScreenPlay", "doBombDroidAction", pBombDroid, "")

	return 0
end

--function theTutorial0001ScreenPlay:spawnSceneObjects()
--
--	local pActiveAreaOne = spawnActiveArea("tatooine", "object/active_area.iff", 3863, 5, -4856, 40, 0)
--	if pActiveAreaOne ~= nil then
--
--		createObserver(ENTEREDAREA, "theTutorial0001ScreenPlay", "notifyEnteredAreaOne", pActiveAreaOne)
--	end
--
--	spawnSceneObject("tatooine", "object/static/vehicle/static_yt_1300.iff", 3863, 5, -4856, 0, math.rad(180) )-- planet, template, x, z, y, cellID, yaw
--end

function ChatAIScreenPlay:spawnMobiles()

    local pC4P4 = spawnMobile("tatooine", "chat_ai_c4p4", 300, 3517.78, 5, -4817.56, 0, 0)-- planet, template, x, z, y, yaw, cellID

    if (pC4P4 ~= nil) then
        writeData("ai:c4p4", SceneObject(pC4P4):getObjectID())
    end

    --for i,v in ipairs(deathWatchStaticSpawns) do
	--	local pMobile = spawnMobile("endor", v[1], v[2], v[3], v[4], v[5], v[6], v[7])
	--	if (pMobile ~= nil) then
	--		writeData("dwb:staticMobile" .. i, SceneObject(pMobile):getObjectID())
	--	end
	--end
    ---- Voice Recognition Terminal
	--spawnedPointer = spawnSceneObject("endor", "object/tangible/dungeon/terminal_free_s1.iff",74.7941,-54,-143.444,5996348,-0.707107,0,0.707107,0)
	--if (spawnedPointer ~= nil) then
	--	spawnedSceneObject:_setObject(spawnedPointer)
	--	spawnedSceneObject:setCustomObjectName("Voice Control Terminal")
	--	writeData("dwb:voiceControlTerminal", spawnedSceneObject:getObjectID())
	--end
	---- Voice Terminal Instruction message
	--local pActiveArea = spawnActiveArea("endor", "object/active_area.iff", -4588, -41.6, 4182.3, 10, 5996348)
	--if pActiveArea ~= nil then
	--	createObserver(ENTEREDAREA, "DeathWatchBunkerScreenPlay", "notifyEnteredVoiceTerminalArea", pActiveArea)
	--end
	----Blastromech
	--local spawn = deathWatchSpecialSpawns["bombdroid"]
	--local spawnedPointer = spawnMobile("endor", spawn[1], spawn[2], spawn[3], spawn[4], spawn[5], spawn[6], spawn[7])
	--if (spawnedPointer ~= nil) then
	--	CreatureObject(spawnedPointer):setPvpStatusBitmask(0)
	--	CreatureObject(spawnedPointer):setCustomObjectName("R2-M2")
	--	writeData("dwb:bombDroid", SceneObject(spawnedPointer):getObjectID())
	--	createEvent(100, "DeathWatchBunkerScreenPlay", "setBombDroidTemplate", spawnedPointer, "")
	--end
    
	--self:setMoodString(pHan, "npc_sitting_chair")

	--local pChewie = spawnMobile("tatooine", "npe_chewbacca", 1, -8.4, -0.9, 22.9, -103, 1082883)
	--self:setMoodString(pChewie, "npc_sitting_chair")
end

--function theTutorial0001ScreenPlay:notifyEnteredAreaOne(pActiveAreaOne, pPlayer)
--
--	if (not SceneObject(pPlayer):isPlayerCreature()) then
--		return 0
--	end
--
--	if (CreatureObject(pPlayer):hasScreenPlayState(1, "tutorial_one")) then
--
--		CreatureObject(pPlayer):sendSystemMessage(" \\#FFFF00\\<Communicator>  \\#FFFFFF\\Ok Kid, looks like she's all in one piece, come on back to the cantina.")
--		CreatureObject(pPlayer):playMusicMessage("sound/ui_quest_waypoint_patrol.snd")
--		CreatureObject(pPlayer):removeScreenPlayState(1, "tutorial_one")
--		CreatureObject(pPlayer):setScreenPlayState(2, "tutorial_one")
--		return 0
--	end
--	return 0
--end

--tutorial_0001_han_solo_convo_handler = conv_handler:new {}

--function tutorial_0001_han_solo_convo_handler:getInitialScreen(pPlayer, pNpc, pConvTemplate)
--
--	local convoTemplate = LuaConversationTemplate(pConvTemplate)
--
--	if (CreatureObject(pPlayer):hasScreenPlayState(1, "tutorial_one")) then
--
--        return convoTemplate:getScreen("task_one_active")
--
--	elseif (CreatureObject(pPlayer):hasScreenPlayState(2, "tutorial_one")) then
--
--		CreatureObject(pPlayer):addCashCredits(10000, true)
--		CreatureObject(pPlayer):playMusicMessage("sound/ui_npe2_quest_credits.snd")
--		CreatureObject(pPlayer):sendSystemMessage(" \\#FFFF00\\Quest completed:  \\#FFFFFF\\Checking the Falcon.")
--		CreatureObject(pPlayer):removeScreenPlayState(2, "tutorial_one")
--		CreatureObject(pPlayer):setScreenPlayState(4, "tutorial_one")
--		return convoTemplate:getScreen("task_one_complete")
--
--	elseif (CreatureObject(pPlayer):hasScreenPlayState(4, "tutorial_one")) then
--		return convoTemplate:getScreen("quest_complete")
--
--	else	
--		return convoTemplate:getScreen("first_screen")
--	end
--end

--function tutorial_0001_han_solo_convo_handler:runScreenHandlers(pConvTemplate, pPlayer, pNpc, selectedOption, pConvScreen)
--	local screen = LuaConversationScreen(pConvScreen)
--	local screenID = screen:getScreenID()
--
--    if (screenID == "accept_task_one") then
--		CreatureObject(pPlayer):setScreenPlayState(1, "tutorial_one")
--		CreatureObject(pPlayer):playMusicMessage("sound/ui_npe2_quest_received.snd")
--		CreatureObject(pPlayer):sendSystemMessage(" \\#FFFF00\\Quest received:  \\#FFFFFF\\Checking the Falcon.")
--    end
--    return pConvScreen
--end

registerScreenPlay("ChatAIScreenPlay", true)

return ChatAIScreenPlay