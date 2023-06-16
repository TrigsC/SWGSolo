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
    local command = "python3 ./my_python.py"
    local handle = io.popen("pwd")
    local output = handle:read("*a")
    handle:close()
    print("**************** PYTHON !!!!!!! " .. output)
    CreatureObject(pPlayer):clearBuffs(true, false)
	CreatureObject(pPlayer):enhanceCharacter()

    Glowing:onPlayerLoggedIn(pPlayer)

    --Glowing:onPlayerLoggedIn(pPlayer)
    --self:registerObservers(pPlayer)
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
