-- Central logging helper for custom AI Lua systems.

AiLogger = AiLogger or {}

AiLogger.LEVEL_ERROR = 1
AiLogger.LEVEL_WARN = 2
AiLogger.LEVEL_INFO = 3
AiLogger.LEVEL_DEBUG = 4
AiLogger.LEVEL_TRACE = 5

local LEVEL_NAMES = {
    [AiLogger.LEVEL_ERROR] = "ERROR",
    [AiLogger.LEVEL_WARN] = "WARN",
    [AiLogger.LEVEL_INFO] = "INFO",
    [AiLogger.LEVEL_DEBUG] = "DEBUG",
    [AiLogger.LEVEL_TRACE] = "TRACE"
}

local LEVEL_BY_NAME = {
    error = AiLogger.LEVEL_ERROR,
    warn = AiLogger.LEVEL_WARN,
    warning = AiLogger.LEVEL_WARN,
    info = AiLogger.LEVEL_INFO,
    debug = AiLogger.LEVEL_DEBUG,
    trace = AiLogger.LEVEL_TRACE
}

local CORE_LEVEL_BY_AI_LEVEL = {
    [AiLogger.LEVEL_ERROR] = LT_ERROR or 1,
    [AiLogger.LEVEL_WARN] = LT_WARNING or 2,
    [AiLogger.LEVEL_INFO] = LT_INFO or 4,
    [AiLogger.LEVEL_DEBUG] = LT_DEBUG or 5,
    [AiLogger.LEVEL_TRACE] = LT_TRACE or 6
}

local Config = nil
do
    local ok, cfg = pcall(require, "custom_scripts.ai_config")
    if ok and cfg ~= nil then
        Config = cfg
    elseif AiConfig ~= nil then
        Config = AiConfig
    else
        Config = {}
    end
end

local LoggerModule = nil
do
    local ok, logger = pcall(require, "utils.logger")
    if ok and logger ~= nil then
        LoggerModule = logger
    elseif Logger ~= nil then
        LoggerModule = Logger
    end
end

local function configuredLevel()
    local logging = Config.logging or {}
    local configured = logging.level or "warn"

    if type(configured) == "number" then
        return configured
    end

    configured = string.lower(tostring(configured))
    return LEVEL_BY_NAME[configured] or AiLogger.LEVEL_WARN
end

local function categoryEnabled(category)
    local logging = Config.logging or {}
    if logging.enabled == false then
        return false
    end

    local categories = logging.categories or {}
    if categories[category] == false then
        return false
    end

    return true
end

local function shouldLog(level, category)
    if level > configuredLevel() then
        return false
    end

    return categoryEnabled(category)
end

local function emit(level, category, message)
    category = tostring(category or "general")
    if not shouldLog(level, category) then
        return
    end

    local levelName = LEVEL_NAMES[level] or "INFO"
    local formatted = "[AI][" .. category .. "][" .. levelName .. "] " .. tostring(message or "")
    local coreLevel = CORE_LEVEL_BY_AI_LEVEL[level] or LT_INFO or 4

    if type(logLua) == "function" then
        local ok = pcall(logLua, coreLevel, formatted)
        if ok then
            return
        end
    end

    if LoggerModule ~= nil and LoggerModule.log ~= nil then
        local ok = pcall(LoggerModule.log, LoggerModule, formatted, coreLevel)
        if ok then
            return
        end
    end

    if type(print) == "function" then
        pcall(print, formatted)
    end
end

function AiLogger.error(category, message)
    emit(AiLogger.LEVEL_ERROR, category, message)
end

function AiLogger.warn(category, message)
    emit(AiLogger.LEVEL_WARN, category, message)
end

function AiLogger.info(category, message)
    emit(AiLogger.LEVEL_INFO, category, message)
end

function AiLogger.debug(category, message)
    emit(AiLogger.LEVEL_DEBUG, category, message)
end

function AiLogger.trace(category, message)
    emit(AiLogger.LEVEL_TRACE, category, message)
end

return AiLogger
