-- Utility helpers for MarketSeeder to build template lists from existing data sources.

MarketSeederTemplates = MarketSeederTemplates or {}

local function ensureCharacterBuilderLoaded()
    if object_tangible_terminal_terminal_character_builder ~= nil then
        return true
    end

    pcall(includeFile, "object/tangible/terminal/terminal_character_builder.lua")

    return object_tangible_terminal_terminal_character_builder ~= nil
end

local function matchesPrefix(value, prefixes)
    if prefixes == nil then
        return true
    end

    for _, prefix in ipairs(prefixes) do
        if type(prefix) == "string" and prefix ~= "" then
            if string.sub(value, 1, string.len(prefix)) == prefix then
                return true
            end
        end
    end

    return false
end

local function excludedByPrefix(value, prefixes)
    if prefixes == nil then
        return false
    end

    for _, prefix in ipairs(prefixes) do
        if type(prefix) == "string" and prefix ~= "" then
            if string.sub(value, 1, string.len(prefix)) == prefix then
                return true
            end
        end
    end

    return false
end

local function extractTemplatesFromCharacterBuilder(config)
    if not ensureCharacterBuilderLoaded() then
        return {}
    end

    local terminal = object_tangible_terminal_terminal_character_builder

    if terminal == nil or terminal.itemList == nil then
        return {}
    end

    local allowDuplicates = config ~= nil and (config.allowDuplicates == true)
    local prefixFilters = nil
    local excludePrefixes = nil
    local limit = nil

    if config ~= nil then
        prefixFilters = config.prefixFilters or config.prefixes
        excludePrefixes = config.excludePrefixes or config.excludes

        if config.maxTemplates ~= nil then
            limit = tonumber(config.maxTemplates)
            if limit ~= nil then
                limit = math.max(0, math.floor(limit + 0.5))
            end
        end
    end

    local templates = {}
    local seen = {}

    local function addTemplate(path)
        if type(path) ~= "string" or path == "" then
            return
        end

        if prefixFilters ~= nil and not matchesPrefix(path, prefixFilters) then
            return
        end

        if excludePrefixes ~= nil and excludedByPrefix(path, excludePrefixes) then
            return
        end

        if limit ~= nil and #templates >= limit then
            return
        end

        if allowDuplicates or not seen[path] then
            templates[#templates + 1] = path
            seen[path] = true
        end
    end

    local function extract(node)
        if type(node) ~= "table" then
            return
        end

        local index = 1
        local nodeCount = #node

        while index <= nodeCount do
            local label = node[index]
            local value = node[index + 1]

            if type(label) == "string" then
                if type(value) == "table" then
                    extract(value)
                    index = index + 2
                elseif type(value) == "string" then
                    addTemplate(value)
                    index = index + 2
                else
                    index = index + 1
                end
            elseif type(label) == "table" then
                extract(label)
                index = index + 1
            else
                index = index + 1
            end

            if limit ~= nil and #templates >= limit then
                break
            end
        end
    end

    extract(terminal.itemList)

    return templates
end

function MarketSeederTemplates.getTemplates(config)
    if config == nil then
        return {}
    end

    local mode = config.type or config.mode or config.source or ""

    if mode == "character_builder" or mode == "character_builder_terminal" or mode == "characterBuilder" then
        return extractTemplatesFromCharacterBuilder(config)
    end

    if mode == "list" or mode == "templates" then
        local templates = {}

        if config.templates ~= nil then
            for _, entry in ipairs(config.templates) do
                if type(entry) == "string" then
                    templates[#templates + 1] = entry
                elseif type(entry) == "table" then
                    local path = entry.template or entry[1]
                    if type(path) == "string" and path ~= "" then
                        templates[#templates + 1] = path
                    end
                end
            end
        end

        return templates
    end

    return {}
end

return MarketSeederTemplates
