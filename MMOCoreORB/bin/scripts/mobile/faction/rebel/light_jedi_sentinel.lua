light_jedi_sentinel = Creature:new {
	objectName = "@mob/creature_names:light_jedi_sentinel",
	randomNameType = NAME_GENERIC,
	randomNameTag = true,
	mobType = MOB_NPC,
	socialGroup = "self",
	faction = "",
	level = 88,
	chanceHit = 0.75,
	statistics = {
        attack_accuracy = 100,
        melee_accuracy = 60,
		melee_defense = 20,
        --dodge_attack = 40,
        --block = 40,
        --counter_attack = 40, 
        lightsaber_toughness = 55,
		--jedi_toughness = 45,
        saber_block = 85,
		--posture_change_defense = 60,
        --intimidate_defense = 60,
        --stun_defense = 60,
        --blind_defense = 60,
    },
	damageMin = 500,
	damageMax = 600,
	baseXp = 45,
	baseHAM = 4000,
	baseHAMmax = 5000,
	armor = 1,
	resists = {0,0,0,0,0,0,0,0,-1},
	meatType = "",
	meatAmount = 0,
	hideType = "",
	hideAmount = 0,
	boneType = "",
	boneAmount = 0,
	milk = 0,
	tamingChance = 0,
	ferocity = 0,
	pvpBitmask = AGGRESSIVE + ATTACKABLE + ENEMY,
	creatureBitmask = KILLER + PACK + HERD,
	optionsBitmask = AIENABLED,
	diet = HERBIVORE,
	healerType = "force",
	jediArchetype = "random",
	frsCouncil = "light",
	frsRankMin = 1,
	frsRankMax = 4,
	frsRankOutfits = lightJediFrsRankOutfits,
	lightsaberColors = lightJediSaberColors,
	customAiMap = "enclaveSentinel",

	templates = { "ranked_light_jedi" },
	lootGroups = {},

	-- Primary and secondary weapon should be different types (rifle/carbine, carbine/pistol, rifle/unarmed, etc)
	-- Unarmed should be put on secondary unless the mobile doesn't use weapons, in which case "unarmed" should be put primary and "none" as secondary
	primaryWeapon = "light_jedi_weapons",
	secondaryWeapon = "none",
	conversationTemplate = "",

	-- primaryAttacks and secondaryAttacks should be separate skill groups specific to the weapon type listed in primaryWeapon and secondaryWeapon
	-- Use merge() to merge groups in creatureskills.lua together. If a weapon is set to "none", set the attacks variable to empty brackets
	primaryAttacks = lightsabermaster,
	secondaryAttacks = {}
}

CreatureTemplates:addCreatureTemplate(light_jedi_sentinel, "light_jedi_sentinel")
