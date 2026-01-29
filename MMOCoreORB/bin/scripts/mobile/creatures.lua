Creature = {
    objectName = "",
    socialGroup = "",
    faction = "",
    level = 0,
    chanceHit = 0.000000,
    damageMin = 0,
    damageMax = 0,
    range = 0,
    baseXp = 0,
    baseHAM = 0,
    armor = 0,
    resists = {0,0,0,0,0,0,0,0,0},
    meatType = "",
    meatAmount = 0,
    hideType = "",
    hideAmount = 0,
    boneType = "",
    boneAmount = 0,
    milk = 0,
    tamingChance = 0.000000,
    ferocity = 0,
    pvpBitmask = NONE,
    creatureBitmask = NONE,
    diet = 0,
    scale = 1.0,

    templates = {},
    lootGroups = {},

    primaryWeapon = "unarmed",
    secondaryWeapon = "none",

    primaryAttacks = {},
    secondaryAttacks = {},
    conversationTemplate = "",
    personalityStf = "",
    optionsBitmask = AIENABLED
}

function Creature:new(o)
    o = o or {}
    setmetatable(o, self)
    self.__index = self
    return o
end

-- =========================================================
-- Core registry
-- =========================================================
CreatureTemplates = {}

function CreatureTemplates:addCreatureTemplate(obj, file)
    if obj == nil then
        print("null template specified for " .. file)
    else
        addTemplate(file, obj)
    end
end

function getCreatureTemplate(crc)
    return CreatureTemplates[crc]
end

-- =========================================================
-- merge() that carries __tiers metadata
-- =========================================================
local TIER_RANK = { novice = 1, mid = 2, master = 3 }

local function mergeTiers(dst, src)
    if type(src) ~= "table" then return end
    local st = rawget(src, "__tiers")
    if type(st) ~= "table" then return end

    local dt = rawget(dst, "__tiers")
    if type(dt) ~= "table" then
        dt = {}
        rawset(dst, "__tiers", dt)
    end

    for prof, tier in pairs(st) do
        local cur = dt[prof]
        if cur == nil or (TIER_RANK[tier] or 0) > (TIER_RANK[cur] or 0) then
            dt[prof] = tier
        end
    end
end

function merge(a, ...)
    local r = {}

    local function addTable(t)
        if type(t) ~= "table" then return end

        -- attacks only
        for _, v in ipairs(t) do
            table.insert(r, v)
        end

        -- tier metadata
        mergeTiers(r, t)
    end

    addTable(a)
    for _, t in ipairs({...}) do
        addTable(t)
    end

    return r
end

-- =========================================================
-- Profession->tier stats table (you maintain this)
-- =========================================================
PROF_TIER_STATS = {
    rifleman = {
        -- Added rifle_speed to all tiers
        novice = { melee_defense = 20, ranged_accuracy = 100, ranged_defense = 2 , block = 10, rifle_speed = 50 },
        mid    = { melee_defense = 40, ranged_accuracy = 120, ranged_defense = 12 , block = 25, rifle_speed = 70 },
        master = { melee_defense = 40, ranged_accuracy = 160, ranged_defense = 72 , block = 80, rifle_speed = 100, blind_defense = 10, stun_defense = 10, dizzy_defense = 10 },
    },
    fencer = {
        -- Added onehandmelee_speed
        novice = { melee_defense = 15, ranged_accuracy = 0, ranged_defense = 15 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 10, dodge = 15, posture_change_down_defense = 20, onehandmelee_speed=50, onehandmelee_speed=100, onehandmelee_toughness=24 },
        mid    = { melee_defense = 27, ranged_accuracy = 0, ranged_defense = 27 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 50, dodge = 45, posture_change_down_defense = 50, onehandmelee_speed=70, onehandmelee_speed=130, onehandmelee_toughness=24 },
        master = { melee_defense = 74, ranged_accuracy = 0, ranged_defense = 69 , block = 0, rifle_speed = 0, blind_defense = 40, stun_defense = 0, dizzy_defense = 40, knockdown_defense = 50, dodge = 105, posture_change_down_defense = 50, onehandmelee_speed=100, onehandmelee_speed=150, onehandmelee_toughness=32 },
    },
    swordsman = {
        -- Added twohandmelee_speed AND onehandmelee_speed (Tuskens often use 1h weapons with Swordsman/Brawler skills)
        novice = { melee_defense = 5, ranged_accuracy = 0, ranged_defense = 0 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 0, dodge = 0, posture_change_down_defense = 0, twohandmelee_speed = 50 },
        mid    = { melee_defense = 10, ranged_accuracy = 0, ranged_defense = 5 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 10, dodge = 0, posture_change_down_defense = 10, twohandmelee_speed = 70 },
        master = { melee_defense = 20, ranged_accuracy = 0, ranged_defense = 15 , block = 0, rifle_speed = 0, blind_defense = 40, stun_defense = 50, dizzy_defense = 20, knockdown_defense = 25, dodge = 0, posture_change_down_defense = 10, twohandmelee_speed = 100 },
    },
    pikeman = {
        -- Added polearm_speed
        novice = { melee_defense = 5, ranged_accuracy = 0, ranged_defense = 0 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 0, dodge = 0, posture_change_down_defense = 0, polearm_speed = 50 },
        mid    = { melee_defense = 10, ranged_accuracy = 0, ranged_defense = 5 , block = 0, rifle_speed = 0, blind_defense = 10, stun_defense = 10, dizzy_defense = 10, knockdown_defense = 10, dodge = 0, posture_change_down_defense = 10, polearm_speed = 70 },
        master = { melee_defense = 30, ranged_accuracy = 0, ranged_defense = 25 , block = 115, rifle_speed = 0, blind_defense = 40, stun_defense = 50, dizzy_defense = 20, knockdown_defense = 25, dodge = 0, posture_change_down_defense = 10, polearm_speed = 100 },
    },
    tka = {
        -- Added unarmed_speed
        novice = { melee_defense = 47, ranged_accuracy = 0, ranged_defense = 30 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 0, dodge = 0, posture_change_down_defense = 0, unarmed_speed = 50 },
        mid    = { melee_defense = 62, ranged_accuracy = 0, ranged_defense = 45 , block = 0, rifle_speed = 0, blind_defense = 10, stun_defense = 10, dizzy_defense = 0, knockdown_defense = 0, dodge = 0, posture_change_down_defense = 0, unarmed_speed = 70 },
        master = { melee_defense = 62, ranged_accuracy = 0, ranged_defense = 45 , block = 0, rifle_speed = 0, blind_defense = 10, stun_defense = 15, dizzy_defense = 5, knockdown_defense = 20, dodge = 0, posture_change_down_defense = 0, unarmed_speed = 100 },
    },
    pistoleer = {
        -- Added pistol_speed
        novice = { melee_defense = 40, ranged_accuracy = 80, ranged_defense = 2 , block = 0, rifle_speed = 0, blind_defense = 0, stun_defense = 0, dizzy_defense = 0, knockdown_defense = 0, dodge = 10, posture_change_down_defense = 0, pistol_speed = 50 },
        mid    = { melee_defense = 40, ranged_accuracy = 85, ranged_defense = 2 , block = 0, rifle_speed = 0, blind_defense = 40, stun_defense = 40, dizzy_defense = 0, knockdown_defense = 0, dodge = 40, posture_change_down_defense = 0, pistol_speed = 70 },
        master = { melee_defense = 45, ranged_accuracy = 95, ranged_defense = 7 , block = 0, rifle_speed = 0, blind_defense = 40, stun_defense = 40, dizzy_defense = 40, knockdown_defense = 50, dodge = 105, posture_change_down_defense = 20, pistol_speed = 100 },
    },
    marksman = {
        master = { attack_accuracy = 150 },
    },
    brawler = {
        master = { melee_accuracy = 60 },
    },
}

local function addInto(dst, src)
    if type(src) ~= "table" then return end
    for k, v in pairs(src) do
        if type(v) == "number" then
            dst[k] = (dst[k] or 0) + v
        end
    end
end

local function collectHighestTiers(primaryAttacks, secondaryAttacks)
    local tiers = {}

    local function mergeTierMap(attacks)
        if type(attacks) ~= "table" then return end
        local t = rawget(attacks, "__tiers")
        if type(t) ~= "table" then return end

        for prof, tier in pairs(t) do
            local cur = tiers[prof]
            if cur == nil or (TIER_RANK[tier] or 0) > (TIER_RANK[cur] or 0) then
                tiers[prof] = tier
            end
        end
    end

    mergeTierMap(primaryAttacks)
    mergeTierMap(secondaryAttacks)

    return tiers
end

local function buildStatisticsFromAttacks(primaryAttacks, secondaryAttacks)
    local out = {}
    local tiers = collectHighestTiers(primaryAttacks, secondaryAttacks)

    for prof, tier in pairs(tiers) do
        local byTier = PROF_TIER_STATS[prof]
        if byTier ~= nil then
            addInto(out, byTier[tier])
        end
    end

    return out
end

-- =========================================================
-- Wrap addCreatureTemplate ONCE, after it's defined
-- =========================================================
local function dumpStats(stats)
    if type(stats) ~= "table" then return "nil" end
    local parts = {}
    for k,v in pairs(stats) do
        table.insert(parts, k .. "=" .. tostring(v))
    end
    table.sort(parts)
    return table.concat(parts, ", ")
end

local function dumpTiers(attacks)
    if type(attacks) ~= "table" then return "nil" end
    local t = rawget(attacks, "__tiers")
    if type(t) ~= "table" then return "no __tiers" end
    local parts = {}
    for prof,tier in pairs(t) do
        table.insert(parts, prof .. ":" .. tier)
    end
    table.sort(parts)
    return table.concat(parts, ", ")
end

-- Prefer old-style 'attacks' because most NPCs use that in their templates.
local function getAttacksForStats(obj)
    if type(obj.attacks) == "table" and #obj.attacks > 0 then
        return obj.attacks, nil
    end
    return obj.primaryAttacks, obj.secondaryAttacks
end

local _oldAdd = CreatureTemplates.addCreatureTemplate
function CreatureTemplates:addCreatureTemplate(obj, file)
    if obj ~= nil then
        local primary, secondary = getAttacksForStats(obj)
        local computedStats = buildStatisticsFromAttacks(primary, secondary)

        if obj.statistics == nil then
            -- Case 1: NPC has no stats. Use our computed ones entirely.
            obj.statistics = computedStats
        else
            -- Case 2: NPC has manual stats (like Tusken King).
            -- We must MERGE our new speed stats into his existing block.
            for k, v in pairs(computedStats) do
                -- Only add the stat if the NPC doesn't already have it defined
                if obj.statistics[k] == nil then
                    obj.statistics[k] = v
                end
            end
        end
        
        -- Debug print to verify it worked for the specific file we are testing
        if string.find(file, "tusken_king") then
            print("[AUTO-STATS] Merged stats for Tusken King. Speed is now: " .. (obj.statistics["onehandmelee_speed"] or "NIL"))
        end
    end
    return _oldAdd(self, obj, file)
end

includeFile("creatureskills.lua")
includeFile("conversation.lua")
includeFile("serverobjects.lua")

local env = getfenv(1)
print("[TIER-TEST] riflemanmaster tiers = " .. dumpTiers(env.riflemanmaster))