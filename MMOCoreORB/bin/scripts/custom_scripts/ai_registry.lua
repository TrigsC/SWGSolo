-- MMOCoreORB/bin/scripts/custom_scripts/ai_registry.lua
--
-- Your existing AiRegistry file, extended with the Smart Doctor Buffer profile + mob map entry.
-- IMPORTANT:
--  - Keep recruiter role semantics unchanged (role="recruiter" triggers your ChatHandler logic).
--  - Smart doctor uses role="smart_doctor" so aiGlobalChatHandler can route deterministically to SmartDoctorBuffer.
--  - LLM (AiBrain) can still be used for flavor lines, but core pricing/queue/payment/buff steps are deterministic in smartDoctorBuffer.lua.

local AiRegistry = {}

-- 1. MAP BY MOB NAME (Best Practice)
-- This uses the internal name found in scripts/mobile/...
AiRegistry.mobMap = {
    ["light_jedi_padawan"] = "padawan",
    ["rebel_recruiter"] = "rebel_recruiter",
    ["imperial_recruiter"] = "imperial_recruiter",
    ["specforce_infiltrator"] = "rebel_trooper",
    ["specforce_marine"] = "rebel_trooper",
    ["stormtrooper"] = "imperial_trooper",
    ["commoner"] = "citizen",

    -- Smart Doctor Buffer NPC template (scripts/mobile/smart_doctor_buffer.lua)
    ["smart_doctor_buffer"] = "smart_doctor",
}

-- 2. MAP BY TEMPLATE FILE (Fallback)
-- Keep this for objects that don't have a mob name (like specific mission NPCs)
AiRegistry.templateMap = {
    -- ["object/mobile/some_weird_custom_guy.iff"] = "mysterious_stranger",

    -- Optional fallback (only needed if your doctor is spawned via template path rather than mob template name)
    -- ["object/mobile/dressed_doctor_human_male_01.iff"] = "smart_doctor",
}

-- THE PROFILES
AiRegistry.profiles = {
    ["padawan"] = {
        name = "Padawan Learner",
        call_signs = {"padawan", "apprentice", "learner"},
        system_prompt = "You are a loyal Star Wars Padawan. Call the player Master.",
        skills = {
            ["heal"] = {
                animation = "force_healing_1",
                cpp_function = "healCreatureTarget",
                response = "Yes Master, healing you now."
            }
        }
    },

    ["rebel_trooper"] = {
        name = "Rebel Special Forces",
        call_signs = {"soldier", "marine", "trooper"},
        system_prompt = "You are a hardened Rebel SpecForce fighting the Empire.",
        skills = {}
    },

    ["imperial_trooper"] = {
        name = "Imperial Trooper",
        call_signs = {"trooper", "stormtrooper", "soldier"},
        system_prompt = "You are an Imperial trooper. You are disciplined, suspicious, and follow orders.",
        skills = {}
    },

    ["citizen"] = {
        name = "Citizen",
        system_prompt = "You are a generic Star Wars citizen. You are busy and slightly annoyed.",
        skills = {}
    },

    -- Add the Rebel Recruiter
    ["rebel_recruiter"] = {
        role = "recruiter", -- CRITICAL: This triggers the logic in ChatHandler
        system_prompt = "You are a weary but dedicated Rebel Alliance Recruiter. You speak formally but warmly to fellow Rebels.",
        call_signs = {"recruiter", "officer"},
        skills = {}
    },

    -- Add the Imperial Recruiter
    ["imperial_recruiter"] = {
        role = "recruiter",
        system_prompt = "You are an Imperial Recruiter. You are arrogant, efficient, and demand respect. You view civilians as beneath you.",
        call_signs = {"recruiter", "officer"},
        skills = {}
    },

    -- Smart Doctor Buffer (Mode 2 deterministic flow in smartDoctorBuffer.lua)
    ["smart_doctor"] = {
        role = "smart_doctor", -- CRITICAL: aiGlobalChatHandler routes to SmartDoctorBuffer when role == "smart_doctor"
        name = "Doctor Buffer",
        call_signs = {"doctor", "doc", "medic", "buffer"},
        system_prompt = "You are a veteran Star Wars Galaxies Doctor buffer in a cantina/med center. You are friendly, efficient, and feel like a real player doc. You must never quote prices or queue positions unless provided explicitly.",
        skills = {}
    }
}

-- LOOKUP FUNCTION
function AiRegistry.getProfile(pCreature)
    if (pCreature == nil) then return nil end

    -- 1. SAFETY CHECK: Is this a Player?
    local okIsPlayer, isPlayer = pcall(function()
        return SceneObject(pCreature):isPlayerCreature()
    end)
    if (not okIsPlayer or isPlayer) then
        return nil
    end

    -- 2. SAFETY CHECK: Is this an AI?
    local okIsAi, isAi = pcall(function()
        return SceneObject(pCreature):isAiAgent()
    end)
    if (not okIsAi or not isAi) then
        return nil
    end

    -- 3. Try to find by Internal Mob Name
    local okAgent, agent = pcall(LuaAiAgent, pCreature)
    if (not okAgent or agent == nil) then
        return nil
    end

    local okMobName, mobName = pcall(function()
        return agent:getCreatureTemplateName()
    end)

    -- CHECK IF NAME IS VALID
    if (okMobName and mobName ~= nil and AiRegistry.mobMap[mobName]) then
        return AiRegistry.profiles[AiRegistry.mobMap[mobName]]
    end

    -- 4. Fallback: Try to find by IFF File Path
    local okTemplatePath, templatePath = pcall(function()
        return SceneObject(pCreature):getTemplateObjectPath()
    end)
    if (okTemplatePath and templatePath ~= nil and AiRegistry.templateMap[templatePath]) then
        return AiRegistry.profiles[AiRegistry.templateMap[templatePath]]
    end

    return nil
end

function AiRegistry.getProfileByTemplate(profileKey)
    if (profileKey == nil) then return nil end

    return AiRegistry.profiles[tostring(profileKey)]
end

return AiRegistry
