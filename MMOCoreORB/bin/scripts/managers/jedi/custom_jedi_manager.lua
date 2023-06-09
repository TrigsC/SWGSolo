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


-- Handling of the checkForceStatus command.
-- @param pPlayer pointer to the creature object of the player who performed the command
function CustomJediManager:checkForceStatusCommand(pPlayer)
	if (pPlayer == nil) then
		return
	end

	Glowing:checkForceStatusCommand(pPlayer)
end

-- Event handler for the BADGEAWARDED event.
-- @param pCreatureObject pointer to the creature object of the player who was awarded with a badge.
-- @param pCreatureObject2 pointer to the creature object of the player who was awarded with a badge.
-- @param badgeNumber the badge number that was awarded.
-- @return 0 to keep the observer active.
function CustomJediManager:badgeAwardedEventHandler(pCreatureObject, pCreatureObject2, badgeNumber)
	if (pCreatureObject == nil) then
		return 0
	end

	self:checkIfProgressedToJedi(pCreatureObject)

	return 0
end

-- Check if the player has mastered all hologrind professions and send sui window and award skills.
-- @param pCreatureObject pointer to the creature object of the player to check the jedi progression on.
function CustomJediManager:checkIfProgressedToJedi(pCreatureObject)
    if Glowing:hasRequiredBadgeCount(pCreatureObject)
	--if self:getNumberOfMasteredProfessions(pCreatureObject) >= NUMBEROFPROFESSIONSTOMASTER and not self:isJedi(pCreatureObject) then
		self:sendSuiWindow(pCreatureObject)
		self:awardJediStatusAndSkill(pCreatureObject)
	end
end

-- Award skill and jedi status to the player.
-- @param pCreatureObject pointer to the creature object of the player who unlocked jedi.
function CustomJediManager:awardJediStatusAndSkill(pCreatureObject)
	local pGhost = CreatureObject(pCreatureObject):getPlayerObject()

	if (pGhost == nil) then
		return
	end

	awardSkill(pCreatureObject, "force_title_jedi_novice")
	PlayerObject(pGhost):setJediState(1)
end

-- Sui window ok pressed callback function.
function CustomJediManager:notifyOkPressed()
    -- Do nothing.
end

-- Send a sui window to the player about unlocking jedi and award jedi status and force sensitive skill.
-- @param pCreatureObject pointer to the creature object of the player who unlocked jedi.
function CustomJediManager:sendSuiWindow(pPlayer)
	local suiManager = LuaSuiManager()
	--suiManager:sendMessageBox(pCreatureObject, pCreatureObject, "@quest/force_sensitive/intro:force_sensitive", "Perhaps you should meditate somewhere alone...", "@ok", "HologrindJediManager", "notifyOkPressed")
	suiManager:sendMessageBox(pPlayer, pPlayer, "@quest/force_sensitive/intro:force_sensitive", "Perhaps you should meditate somewhere alone... Make sure to drop all skills before meditating.", "@ok", "CustomJediManager", "notifyOkPressed")
end

-- Register observer on the player for observing badge awards.
-- @param pCreatureObject pointer to the creature object of the player to register observers on.
function CustomJediManager:registerObservers(pPlayer)
    --dropObserver(BADGEAWARDED, "CustomJediManager", "badgeAwardedEventHandler", pCreatureObject)
	createObserver(BADGEAWARDED, "CustomJediManager", "badgeAwardedEventHandler", pPlayer)
end

-- Handling of the onPlayerLoggedIn event. The progression of the player will be checked and observers will be registered.
-- @param pPlayer pointer to the creature object of the player who logged in.
function CustomJediManager:onPlayerLoggedIn(pPlayer)
	if (pPlayer == nil) then
		return
	end
    CreatureObject(pPlayer):clearBuffs(true, false)
	CreatureObject(pPlayer):enhanceCharacter()

    Glowing:onPlayerLoggedIn(pPlayer)

    --Glowing:onPlayerLoggedIn(pPlayer)
    self:registerObservers(pPlayer)
end

--Check to ensure force skill prerequisites are maintained
function CustomJediManager:canSurrenderSkill(pPlayer, skillName)

	if skillName == "force_title_jedi_rank_02" or skillName == "force_title_jedi_novice" then
		CreatureObject(pPlayer):sendSystemMessage("@jedi_spam:revoke_force_title")
		return false
	end

	return true
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
        self:sendSuiWindow(pPlayer)
		--CustomJediManagerHolocron.useHolocron(pSceneObject, pPlayer)
	end
end

--Check for force skill prerequisites
function CustomJediManager:canLearnSkill(pPlayer, skillName)
	return true
end

registerScreenPlay("CustomJediManager", true)

return CustomJediManager
