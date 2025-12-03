local AiRegistry = {}

-- 1. MAP BY MOB NAME (Best Practice)
-- This uses the internal name found in scripts/mobile/...
AiRegistry.mobMap = {
    ["light_jedi_padawan"] = "padawan",
    ["specforce_marine", "specforce_infiltrator"] = "rebel_trooper", -- Example
    ["stormtrooper"] = "imperial_trooper", -- Example
    ["commoner"] = "citizen", -- Example
}

-- 2. MAP BY TEMPLATE FILE (Fallback)
-- Keep this for objects that don't have a mob name (like specific mission NPCs)
AiRegistry.templateMap = {
    -- ["object/mobile/some_weird_custom_guy.iff"] = "mysterious_stranger",
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
    ["citizen"] = {
        name = "Citizen",
        system_prompt = "You are a generic Star Wars citizen. You are busy and slightly annoyed.",
        skills = {}
    }
}

-- LOOKUP FUNCTION
function AiRegistry.getProfile(pCreature)
    if (pCreature == nil) then return nil end
    
    -- 1. SAFETY CHECK: Is this a Player?
    if (SceneObject(pCreature):isPlayerCreature()) then
        return nil
    end

    -- 2. SAFETY CHECK: Is this an AI?
    if (not SceneObject(pCreature):isAiAgent()) then
        return nil
    end
    
    -- 3. Try to find by Internal Mob Name
    local agent = LuaAiAgent(pCreature)
    local mobName = agent:getCreatureTemplateName()
    
    -- CHECK IF NAME IS VALID (New Check)
    if (mobName ~= nil and AiRegistry.mobMap[mobName]) then
        return AiRegistry.profiles[AiRegistry.mobMap[mobName]]
    end

    -- 4. Fallback: Try to find by IFF File Path
    local templatePath = SceneObject(pCreature):getTemplateObjectPath()
    if (templatePath ~= nil and AiRegistry.templateMap[templatePath]) then
        return AiRegistry.profiles[AiRegistry.templateMap[templatePath]]
    end

    return nil
end

return AiRegistry