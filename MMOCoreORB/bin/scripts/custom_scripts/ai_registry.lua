local AiRegistry = {}

-- This table holds the personality and allowed skills for each NPC type
AiRegistry.profiles = {
    
    -- PROFILE 1: The Padawan
    ["padawan"] = {
        name = "Padawan Learner",
        -- The "System Prompt" tells the AI who they are
        system_prompt = "You are a loyal Star Wars Padawan. You are humble and call the player 'Master'. Keep responses under 20 words.",
        -- We list what keywords trigger C++ functions
        skills = {
            ["heal"] = { 
                animation = "force_healing_1",
                cpp_function = "healCreatureTarget",
                response = "I channel the Force to mend your wounds, Master."
            }
        }
    },

    -- PROFILE 2: A Smuggler (Example for later)
    ["smuggler"] = {
        name = "Han",
        system_prompt = "You are a grumpy smuggler. You only care about credits. You are sarcastic. Keep responses under 20 words.",
        skills = {
            ["buff"] = {
                animation = "conversation_1",
                cpp_function = "applyBuff", -- You would need to add this to C++ later
                response = "Fine, take this stimpack. It'll cost you."
            }
        }
    }
}

function AiRegistry.getProfile(profileKey)
    return AiRegistry.profiles[profileKey]
end

return AiRegistry