sim_playerbot_novice = Creature:new {
	objectName = "@mob/creature_names:commoner",
	randomNameType = NAME_GENERIC,
	randomNameTag = true,
	mobType = MOB_NPC,
	socialGroup = "townsperson",
	faction = "",
	-- Calibrated to a STARTING PLAYER CHARACTER, not a level-1 critter. The
	-- first cut (1-2 damage, 100 HAM) was below every creature in the game: it
	-- needed ~750s to kill a dwarf_nuna while the nuna killed it in ~17s, so
	-- scenario unarmed_novice_kills_nuna timed out against a corpse
	-- (LIVE_VERIFICATION_FAIL, run 20260906-123119). Reference points: a real
	-- fresh character carries a few hundred per HAM pool, and comparable
	-- low-level humanoids sit at bomarr_monk (level 3) 35-45 damage and
	-- diseased_bocatt (level 8) 405 HAM. A novice brawler beating a dwarf nuna
	-- is the correct outcome - nunas are exactly what starting players hunt.
	level = 3,
	chanceHit = 0.32,
	damageMin = 25,
	damageMax = 40,
	baseXp = 1,
	baseHAM = 400,
	baseHAMmax = 500,
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
	creatureBitmask = KILLER,
	optionsBitmask = AIENABLED,
	diet = HERBIVORE,
	-- Appearance templates must exist in the client TRE data; LuaMobileTest
	-- asserts every one resolves. These four are proven by heavy use across the
	-- shipped commoner mobiles, giving the novice roster a plausible mix rather
	-- than one repeated face.
	templates = {
		"object/mobile/dressed_commoner_naboo_human_male_03.iff",
		"object/mobile/dressed_commoner_naboo_human_male_08.iff",
		"object/mobile/dressed_commoner_naboo_human_female_07.iff",
		"object/mobile/dressed_commoner_old_human_female_02.iff",
	},
	lootGroups = {},
	primaryWeapon = "unarmed",
	secondaryWeapon = "none",
	conversationTemplate = "",
	primaryAttacks = {},
	secondaryAttacks = {}
}

CreatureTemplates:addCreatureTemplate(sim_playerbot_novice, "sim_playerbot_novice")
