chat_ai_c4p4 = Creature:new {
	customName = "C4-P4",
	randomNameTag = false,
	socialGroup = "townsperson",
	faction = "townsperson",
	level = 300,
	chanceHit = 0.24,
	damageMin = 40,
	damageMax = 45,
	baseXp = 62,
	baseHAM = 113,
	baseHAMmax = 118,
	armor = 0,
	resists = {0,0,0,0,0,0,0,-1,-1},
	meatType = "",
	meatAmount = 0,
	hideType = "",
	hideAmount = 0,
	boneType = "",
	boneAmount = 0,
	milk = 0,
	tamingChance = 0,
	ferocity = 0,
	pvpBitmask = NONE,
	creatureBitmask = NONE,
	optionsBitmask = AIENABLED + INTERESTING + CONVERSABLE,
	diet = HERBIVORE,

	templates = {"object/mobile/3po_protocol_droid_red.iff"},
	lootGroups = {},
	weapons = {},
	conversationTemplate = "",
	attacks = {
	}
}

CreatureTemplates:addCreatureTemplate(chat_ai_c4p4, "chat_ai_c4p4")