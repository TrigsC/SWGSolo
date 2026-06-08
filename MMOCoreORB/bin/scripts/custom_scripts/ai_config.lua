-- Central configuration for custom AI service integration.

AiConfig = AiConfig or {}

AiConfig.llm = AiConfig.llm or {}
if AiConfig.llm.enabled == nil then
    AiConfig.llm.enabled = true
end
AiConfig.llm.url = AiConfig.llm.url or "http://ollama_brain:11434/api/generate"
AiConfig.llm.model = AiConfig.llm.model or "llama3.2"
AiConfig.llm.timeoutSeconds = AiConfig.llm.timeoutSeconds or 3

AiConfig.smartDoctor = AiConfig.smartDoctor or {}
if AiConfig.smartDoctor.llmFlavorEnabled == nil then
    AiConfig.smartDoctor.llmFlavorEnabled = false
end

AiConfig.logging = AiConfig.logging or {}
if AiConfig.logging.enabled == nil then
    AiConfig.logging.enabled = true
end
AiConfig.logging.level = AiConfig.logging.level or "warn"
AiConfig.logging.categories = AiConfig.logging.categories or {}
if AiConfig.logging.categories.doctor == nil then
    AiConfig.logging.categories.doctor = true
end
if AiConfig.logging.categories.entertainer == nil then
    AiConfig.logging.categories.entertainer = true
end
if AiConfig.logging.categories.chat == nil then
    AiConfig.logging.categories.chat = true
end
if AiConfig.logging.categories.llm == nil then
    AiConfig.logging.categories.llm = true
end
if AiConfig.logging.categories.simplayer == nil then
    AiConfig.logging.categories.simplayer = true
end
if AiConfig.logging.categories.bridge == nil then
    AiConfig.logging.categories.bridge = true
end

return AiConfig
