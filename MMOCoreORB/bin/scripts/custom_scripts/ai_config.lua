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

return AiConfig
