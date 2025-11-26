light_jedi_padawan = Creature:new {
    objectName = "Jedi Padawan", -- Changed from string_id to raw text for testing
    randomNameType = NAME_GENERIC,
    randomNameTag = true,
    mobType = MOB_NPC,
    socialGroup = "rebel", -- Changed to rebel (or whatever your faction is)
    faction = "rebel",
    level = 20, -- Downgraded from 88. Padawans shouldn't be masters yet.
    
    statistics = {
        attack_accuracy = 50, -- Lowered from 100
        melee_accuracy = 50,
        melee_defense = 20,
        lightsaber_toughness = 20, -- Lowered toughness
        saber_block = 25, -- Padawans aren't great at blocking yet
    },

    damageMin = 150, -- Lowered from 500
    damageMax = 250, -- Lowered from 600
    baseXp = 2000,
    baseHAM = 2500, -- Lowered health from 4000
    baseHAMmax = 3000,
    armor = 0, -- Jedi usually rely on evasion/block, not armor
    
    resists = {0,0,0,0,0,0,0,0,-1},
    meatType = "",
    meatAmount = 0,
    hideType = "",
    hideAmount = 0,
    boneType = "",
    boneAmount = 0,
    milk = 0,
    tamingChance = 0, -- Keep 0. We buy the deed; we don't tame them in the wild.
    ferocity = 0,
    
    -- IMPORTANT CHANGES HERE --
    
    -- 1. Bitmasks: Removed AGGRESSIVE. Added ATTACKABLE (so enemies can hit it).
    pvpBitmask = ATTACKABLE, 
    
    -- 2. Creature Bitmask: PACK + HERD helps them follow you better.
    creatureBitmask = PACK + HERD, 
    
    -- 3. Options: AIENABLED is standard. CONVERSABLE allows right-click chatting.
    optionsBitmask = AIENABLED + CONVERSABLE, 
    
    diet = HERBIVORE,
    healerType = "force",
    
    -- 4. REMOVED customAiMap = "enclaveSentinel"
    -- We don't want it standing guard. We want it following.
    
    -- 5. SCRIPTS: This is what makes it a pet + your AI hook.
    scripts = {
        "ai.pet_advance",   -- This enables Follow/Store/Group commands
        "ai.padawan_brain"  -- This is your RAG Logic (We will create this next)
    },

    -- VISUALS
    templates = { "object/mobile/shared_light_jedi_sentinel.iff" },
    lootGroups = {},

    -- WEAPONS
    -- Kept these, but ensure "light_jedi_weapons" exists in your weapon groups file
    primaryWeapon = "light_jedi_weapons",
    secondaryWeapon = "light_jedi_weapons_ranged",
    
    conversationTemplate = "",

    -- ATTACKS
    -- Downgraded from "lightsabermaster" to standard skills
    -- You can create a custom group later called "padawan_attacks"
    primaryAttacks = { 
        {"lightsaber_1h_04", ""},
        {"forceknockdown1", ""} 
    },
    secondaryAttacks = {
        {"force_rank_light_rank1", ""} -- Basic force powers
    }
}

CreatureTemplates:addCreatureTemplate(jedi_padawan, "light_jedi_padawan")