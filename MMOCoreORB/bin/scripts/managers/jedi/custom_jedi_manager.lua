JediManager = require("managers.jedi.jedi_manager")
local Logger = require("utils.logger")
local QuestManager = require("managers.quest.quest_manager")

jediManagerName = "CustomJediManager"

NOTINABUILDING = 0

CustomJediManager = JediManager:new {
	screenplayName = jediManagerName,
	jediManagerName = jediManagerName,
	jediProgressionType = CUSTOMJEDIPROGRESSION,
	startingEvent = nil,
}

-- Handling of the onPlayerLoggedIn event. The progression of the player will be checked and observers will be registered.
-- @param pPlayer pointer to the creature object of the player who logged in.
function CustomJediManager:onPlayerLoggedIn(pPlayer)
	if (pPlayer == nil) then
		return
	end
    
    CreatureObject(pPlayer):clearBuffs(true, false)
	CreatureObject(pPlayer):enhanceCharacter()

    Glowing:onPlayerLoggedIn(pPlayer)
    createObserver(SPATIALCHATSENT, "CustomJediManager", "notifyChatSent", pCreature)

    --Glowing:onPlayerLoggedIn(pPlayer)
    --self:registerObservers(pPlayer)
end

function CustomJediManager:onPlayerLoggedOut(pPlayer)
	if (pPlayer == nil) then
		return
	end

    dropObserver(SPATIALCHATSENT, "CustomJediManager", "notifyChatSent", pCreature)
end

function CustomJediManager:notifyChatSent(pPlayer, pChatMessage)
	if (pPlayer == nil or not SceneObject(pPlayer):isPlayerCreature() or pChatMessage == nil) then
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

	local tokenizer = {}
	for word in spatialMsg:gmatch("%w+") do table.insert(tokenizer, word) end

	local spatialCommand = tokenizer[1]

	if (spatialCommand == nil or spatialCommand == "") then
		printLuaError("Invalid spatial command from player " .. SceneObject(pPlayer):getDisplayedName() .. ", spatial message: " .. spatialMsg)
		return 0
	end

    local python_location = "python3 /home/swgemu/Core3/MMOCoreORB/bin/scripts/managers/jedi/my_python.py "
    local command = python_location .. spatialCommand
    local handle = io.popen(command)
    local output = handle:read("*a")
    handle:close()
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

-- Handling of the useItem event.
-- @param pSceneObject pointer to the item object.
-- @param itemType the type of item that is used.
-- @param pPlayer pointer to the creature object that used the item.
function CustomJediManager:useItem(pSceneObject, itemType, pPlayer)
	if (pSceneObject == nil or pPlayer == nil) then
		return
	end

	Logger:log("useItem called with item type " .. itemType, LT_INFO)
	if itemType == ITEMHOLOCRON then
        VillageJediManagerHolocron.useHolocron(pSceneObject, pPlayer)
        --self:sendSuiWindow(pPlayer)
        --TODO: SET HEIGHT OF PLAYER and then TELEPORT SOMETHING LIKE
        --SceneObject(pPlayer):teleport(79.7, -60, -43.3, 8575749)
        --CreatureObject(pPlayer):setHeight(pPlayer)
	end
end

---- Sui window ok pressed callback function.
--function CustomJediManager:notifyOkPressed()
--    -- Do nothing.
--end
--
---- Send a sui window to the player about unlocking jedi and award jedi status and force sensitive skill.
---- @param pCreatureObject pointer to the creature object of the player who unlocked jedi.
--function CustomJediManager:sendSuiWindow(pPlayer)
--    --local pGhost = CreatureObject(pPlayer):getPlayerObject()
--	local suiManager = LuaSuiManager()
--	--suiManager:sendMessageBox(pCreatureObject, pCreatureObject, "@quest/force_sensitive/intro:force_sensitive", "Perhaps you should meditate somewhere alone...", "@ok", "HologrindJediManager", "notifyOkPressed")
--	suiManager:sendMessageBox(pPlayer, pPlayer, "@quest/force_sensitive/intro:force_sensitive", "Perhaps you should meditate somewhere alone... Make sure to drop all skills before meditating.", "@ok", "CustomJediManager", "notifyOkPressed")
--    --local currHeight = CreatureObject(pGhost):getHeight(pGhost)
--    --CreatureObject(pGhost):setHeight(pPlayer)
--end

-- Handling of the checkForceStatus command.
-- @param pPlayer pointer to the creature object of the player who performed the command
function CustomJediManager:checkForceStatusCommand(pPlayer)
	if (pPlayer == nil) then
		return
	end

	Glowing:checkForceStatusCommand(pPlayer)
end

--Check to ensure force skill prerequisites are maintained
function CustomJediManager:canSurrenderSkill(pPlayer, skillName)

	if skillName == "force_title_jedi_rank_02" or skillName == "force_title_jedi_novice" then
		CreatureObject(pPlayer):sendSystemMessage("@jedi_spam:revoke_force_title")
		return false
	end

	return true
end

--Check for force skill prerequisites
function CustomJediManager:canLearnSkill(pPlayer, skillName)
	return true
end

registerScreenPlay("CustomJediManager", true)

return CustomJediManager
