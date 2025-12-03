local AiRegistry = {}

-- Map specific Creature Templates to Profile Keys
AiRegistry.templateMap = {
    -- The filename of the NPC : The profile to load
    ["object/mobile/light_jedi_padawan.iff"] = "padawan",
    ["object/mobile/stormtrooper.iff"] = "trooper", -- Example for later
}

-- The detailed profiles
AiRegistry.profiles = {
    ["padawan"] = {
        name = "Padawan Learner",
        system_prompt = "You are a loyal Star Wars Padawan. Call the player Master. Keep it brief.",
        skills = {
            ["heal"] = { 
                animation = "force_healing_1",
                cpp_function = "healCreatureTarget",
                response = "Yes Master, healing you now."
            }
        }
    },
    ["trooper"] = {
        name = "Stormtrooper",
        system_prompt = "You are a loyal Imperial Stormtrooper. You demand identification.",
        skills = {}
    }
}

function AiRegistry.getProfileByTemplate(templatePath)
    local profileKey = AiRegistry.templateMap[templatePath]
    if profileKey then
        return AiRegistry.profiles[profileKey]
    end
    return nil
end

return AiRegistry