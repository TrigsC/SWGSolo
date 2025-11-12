MarketSeeder = {
    planet = "naboo",
    bazaar = { x = -5145.46, z = 6.55, y = 4143.07 },
    testPrice = 12345,
    testDurationHours = 24,
    template = "object/tangible/loot/simple_kit/empty_datapad.iff",
    templates = nil,
    templateQuantities = {},
    templateSource = {
        type = "character_builder",
        prefixFilters = {
            "object/tangible/",
            "object/weapon/"
        },
        excludePrefixes = {
            "object/tangible/terminal/"
        },
        maxTemplates = 250,
        quantity = 1
    },
    defaultQuantity = 1,
    maxListingsPerSeed = 250,
    summaryLogLimit = 10,
    runOnceOnBoot = true,
    _hasSeeded = false
}

includeFile("custom/market_seeder_templates.lua")

registerScreenPlay("MarketSeeder", true)

local function ensureBridge()
    MarketSeederBridge = MarketSeederBridge or {}

    if marketSeederListOnBazaar ~= nil then
        MarketSeederBridge = MarketSeederBridge or {}
        MarketSeederBridge.listOnBazaar = marketSeederListOnBazaar
    end

    if marketSeederGetSystemSeller ~= nil then
        MarketSeederBridge.getSystemSeller = marketSeederGetSystemSeller
    end

    if marketSeederTemplateExists ~= nil then
        MarketSeederBridge.templateExists = marketSeederTemplateExists
    end

    if marketSeederCreateItem ~= nil then
        MarketSeederBridge.createItem = marketSeederCreateItem
    end
end

function MarketSeeder:log(message)
    logLua("[MarketSeeder] " .. message)
end

function MarketSeeder:start()
    ensureBridge()
    self:log("screenplay loaded (runOnceOnBoot=" .. tostring(self.runOnceOnBoot) .. ")")

    if self.runOnceOnBoot then
        self:seed_once(nil, "boot")
    end
end

function MarketSeeder:getSeller()
    if self._seller ~= nil then
        return self._seller
    end

    if MarketSeederBridge == nil or MarketSeederBridge.getSystemSeller == nil then
        self:log("bridge unavailable while acquiring seller")
        return nil
    end

    local pSeller = MarketSeederBridge.getSystemSeller(self.planet)

    if pSeller ~= nil then
        self._seller = pSeller

        local sellerObj = SceneObject(pSeller)
        if sellerObj ~= nil then
            self:log(string.format("using system seller oid=%s", tostring(sellerObj:getObjectID())))
        else
            self:log("system seller acquired")
        end
    else
        self:log("failed to obtain system seller")
    end

    return self._seller
end

function MarketSeeder:seed_once(pInvoker, reason)
    ensureBridge()

    local context = reason or "manual"

    if MarketSeederBridge == nil or MarketSeederBridge.listOnBazaar == nil then
        self:log("bridge unavailable; aborting seed request")

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Bridge unavailable; check server logs.")
        end

        return false
    end

    local pSeller = self:getSeller()

    if pSeller == nil then
        self:log("no seller available for seeding")

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] No seller available; check server logs.")
        end

        return false
    end

    local seller = CreatureObject(pSeller)

    if seller == nil then
        self:log("seller reference invalid")

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Seller reference invalid; check server logs.")
        end

        return false
    end

    local pInventory = seller:getSlottedObject("inventory")

    if pInventory == nil then
        self:log("seller inventory not found")

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Seller inventory missing; check server logs.")
        end

        return false
    end

    local queue = self:buildSeedQueue()

    if #queue == 0 then
        self:log("no templates available for seeding")

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] No templates configured for seeding.")
        end

        return false
    end

    local limit = #queue
    if self.maxListingsPerSeed ~= nil then
        local maxListings = tonumber(self.maxListingsPerSeed)
        if maxListings ~= nil and maxListings >= 0 then
            limit = math.min(limit, math.floor(maxListings + 0.5))
        end
    end

    local summary = {
        attempted = limit,
        listed = 0,
        missingTemplate = 0,
        creationFailed = 0,
        listingFailed = 0,
        samples = {
            missingTemplate = {},
            creationFailed = {},
            listingFailed = {}
        }
    }

    local lastItemId = 0
    local sampleLimit = self.summaryLogLimit or 0

    for index = 1, limit, 1 do
        local templatePath = queue[index]
        local success, reason, itemId = self:seedTemplate(pSeller, pInventory, templatePath)

        if success then
            summary.listed = summary.listed + 1
            lastItemId = itemId or lastItemId
        else
            if reason == "missingTemplate" then
                summary.missingTemplate = summary.missingTemplate + 1
            elseif reason == "creationFailed" then
                summary.creationFailed = summary.creationFailed + 1
            elseif reason == "listingFailed" then
                summary.listingFailed = summary.listingFailed + 1
            end

            if sampleLimit > 0 then
                local samples = summary.samples[reason]
                if samples ~= nil and #samples < sampleLimit then
                    samples[#samples + 1] = templatePath
                end
            end
        end
    end

    if summary.listed > 0 then
        self._hasSeeded = true
        self.lastItemId = lastItemId
    end

    local details = string.format(
        "seed summary (reason=%s): listed=%d attempted=%d missing=%d createFailed=%d listFailed=%d",
        context,
        summary.listed,
        summary.attempted,
        summary.missingTemplate,
        summary.creationFailed,
        summary.listingFailed
    )

    self:log(details)

    for reason, samples in pairs(summary.samples) do
        if #samples > 0 then
            self:log(string.format("sample %s templates: %s", reason, table.concat(samples, ", ")))
        end
    end

    if pInvoker ~= nil then
        CreatureObject(pInvoker):sendSystemMessage(string.format(
            "[MarketSeeder] Bazaar listings created: %d/%d (missing=%d, createFailed=%d, listFailed=%d)",
            summary.listed,
            summary.attempted,
            summary.missingTemplate,
            summary.creationFailed,
            summary.listingFailed
        ))
    end

    return summary.listed > 0
end

function MarketSeeder:getTemplateRequests()
    local requests = {}

    local defaultQuantity = tonumber(self.defaultQuantity) or 1
    if defaultQuantity < 1 then
        defaultQuantity = 1
    end

    local templateQuantities = self.templateQuantities or {}

    local function resolveQuantity(templatePath, quantity)
        local override = templateQuantities[templatePath]

        if override ~= nil then
            local overrideNumber = tonumber(override)
            if overrideNumber ~= nil and overrideNumber >= 1 then
                return math.floor(overrideNumber + 0.5)
            end
        end

        local value = tonumber(quantity)
        if value ~= nil and value >= 1 then
            return math.floor(value + 0.5)
        end

        return defaultQuantity
    end

    local function addTemplate(templatePath, quantity)
        if type(templatePath) ~= "string" or templatePath == "" then
            return
        end

        local resolvedQuantity = resolveQuantity(templatePath, quantity)

        requests[#requests + 1] = {
            template = templatePath,
            count = resolvedQuantity
        }
    end

    if self.template ~= nil then
        addTemplate(self.template, defaultQuantity)
    end

    if self.templates ~= nil then
        for _, entry in ipairs(self.templates) do
            if type(entry) == "string" then
                addTemplate(entry, defaultQuantity)
            elseif type(entry) == "table" then
                local templatePath = entry.template or entry[1]
                local quantity = entry.count or entry.quantity or entry.amount or entry.copies or entry[2]
                addTemplate(templatePath, quantity)
            end
        end
    end

    if self.templateSource ~= nil then
        local sourceQuantity = self.templateSource.quantity or self.templateSource.count or self.templateSource.amount

        if MarketSeederTemplates ~= nil and MarketSeederTemplates.getTemplates ~= nil then
            local sourceTemplates = MarketSeederTemplates.getTemplates(self.templateSource)
            for _, templatePath in ipairs(sourceTemplates) do
                addTemplate(templatePath, sourceQuantity)
            end
        else
            self:log("template source unavailable; unable to load templates from source")
        end
    end

    return requests
end

function MarketSeeder:buildSeedQueue()
    local queue = {}
    local requests = self:getTemplateRequests()

    for _, request in ipairs(requests) do
        local templatePath = request.template
        local count = request.count or 1

        if type(templatePath) == "string" and templatePath ~= "" then
            if type(count) ~= "number" then
                count = tonumber(count) or 1
            end

            count = math.max(1, math.floor(count + 0.5))

            for _ = 1, count, 1 do
                queue[#queue + 1] = templatePath
            end
        end
    end

    return queue
end

function MarketSeeder:seedTemplate(pSeller, pInventory, templatePath)
    if MarketSeederBridge.templateExists ~= nil and not MarketSeederBridge.templateExists(templatePath) then
        return false, "missingTemplate"
    end

    local pItem = nil

    if MarketSeederBridge.createItem ~= nil then
        pItem = MarketSeederBridge.createItem(pSeller, pInventory, templatePath, -1, false)
    else
        pItem = giveItem(pInventory, templatePath, -1)
    end

    if pItem == nil then
        return false, "creationFailed"
    end

    local itemObject = SceneObject(pItem)
    local itemId = 0

    if itemObject ~= nil then
        itemId = itemObject:getObjectID()
    end

    local success = MarketSeederBridge.listOnBazaar(pItem, pSeller, self.planet, self.bazaar.x, self.bazaar.z, self.bazaar.y, self.testPrice, self.testDurationHours)

    if success then
        return true, nil, itemId
    end

    if itemObject ~= nil then
        itemObject:destroyObjectFromWorld(true)
        itemObject:destroyObjectFromDatabase()
    end

    return false, "listingFailed"
end

