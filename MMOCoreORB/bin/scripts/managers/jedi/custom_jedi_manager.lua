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

    --Glowing:onPlayerLoggedIn(pPlayer)
    --self:registerObservers(pPlayer)
end
