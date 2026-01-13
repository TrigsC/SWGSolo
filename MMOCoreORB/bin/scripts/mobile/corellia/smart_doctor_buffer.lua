-- Smart Doctor Buffer NPC
-- Minimal, stationary, non-combat NPC used by SmartDoctorBuffer screenplay.

smart_doctor_buffer = Creature:new {
    objectName = "@mob/creature_names:scientist",
    customName = "Doctor",
    socialGroup = "townsperson",
    faction = "neutral",
    level = 1,
    chanceHit = 0.0,
    damageMin = 0,
    damageMax = 0,
    baseXp = 0,
    baseHAM = 1000,
    baseHAMmax = 1000,
    armor = 0,
    resists = {0,0,0,0,0,0,0,0,0},
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
    optionsBitmask = INVULNERABLE + CONVERSABLE,
    diet = HERBIVORE,

    templates = {
        -- Swap if your TRE doesn't include this exact IFF:
        "object/mobile/dressed_doctor_trainer_human_female_01.iff",
    },

    weapons = {},
    conversationTemplate = "",
    attacks = {},
}

CreatureTemplates:addCreatureTemplate(smart_doctor_buffer, "smart_doctor_buffer")