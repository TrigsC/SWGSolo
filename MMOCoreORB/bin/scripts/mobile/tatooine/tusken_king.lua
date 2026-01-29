tusken_king = Creature:new {
	objectName = "@mob/creature_names:tusken_king",
	socialGroup = "tusken_raider",
	faction = "tusken_raider",
	mobType = MOB_NPC,
	level = 100,
	--statistics = {
    --    attack_accuracy = 150,
    --    melee_accuracy = 60,
	--	ranged_accuracy = 160,
	--	melee_defense = 121,
	--	ranged_defense = 146,
    --    dodge_attack = 105,
    --    block = 80,
    --    --counter_attack = 40, 
    --    --lightsaber_toughness = 55,
	--	--jedi_toughness = 45,
    --    --saber_block = 85,
	--	posture_change_defense = 50,
    --    --intimidate_defense = 60,
    --    stun_defense = 10,
    --    blind_defense = 50,
    --},
	--chanceHit = 1,
	damageMin = 645,
	damageMax = 1000,
	baseXp = 9522,
	baseHAM = 3000,
	baseHAMmax = 4000,
	armor = 2,
	resists = {45,35,5,40,-1,50,5,5,-1},
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
	creatureBitmask = PACK + STALKER,
	optionsBitmask = AIENABLED,
	diet = HERBIVORE,

	templates = {"object/mobile/tusken_raider.iff"},
	lootGroups = {
		{
			groups = {
				{group = "tusken_raider_tier_4", chance = 10000000}
			}
		}
	},

	-- Primary and secondary weapon should be different types (rifle/carbine, carbine/pistol, rifle/unarmed, etc)
	-- Unarmed should be put on secondary unless the mobile doesn't use weapons, in which case "unarmed" should be put primary and "none" as secondary
	primaryWeapon = "tusken_ranged",
	secondaryWeapon = "tusken_melee",
	conversationTemplate = "",

	-- primaryAttacks and secondaryAttacks should be separate skill groups specific to the weapon type listed in primaryWeapon and secondaryWeapon
	-- Use merge() to merge groups in creatureskills.lua together. If a weapon is set to "none", set the attacks variable to empty brackets
	primaryAttacks = merge(marksmanmaster,riflemanmaster),
	secondaryAttacks = merge(brawlermaster,fencermaster)
}

CreatureTemplates:addCreatureTemplate(tusken_king, "tusken_king")
