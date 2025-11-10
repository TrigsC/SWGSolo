MarketSeeder = {
    planet = "naboo",
    bazaar = { x = -5145.46, z = 6.55, y = 4143.07 },
    testPrice = 12345,
    testDurationHours = 24,
    template = "object/tangible/loot/simple_kit/empty_datapad.iff",
    runOnceOnBoot = true,
    _hasSeeded = false
}

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

    if MarketSeederBridge.templateExists ~= nil and not MarketSeederBridge.templateExists(self.template) then
        self:log("template not found: " .. self.template)

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Template not found; check template path.")
        end

        return false
    end

    local pItem = nil

    if MarketSeederBridge.createItem ~= nil then
        pItem = MarketSeederBridge.createItem(pSeller, pInventory, self.template, -1, false)
    else
        pItem = giveItem(pInventory, self.template, -1)
    end

    if pItem == nil then
        self:log("failed to create test item for template " .. self.template)

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Failed to create test item; check template path.")
        end

        return false
    end

    local itemObject = SceneObject(pItem)
    local itemId = 0

    if itemObject ~= nil then
        itemId = itemObject:getObjectID()
    end

    self:log(string.format("created test item template=%s oid=%s (reason=%s)", self.template, tostring(itemId), context))

    local success = MarketSeederBridge.listOnBazaar(pItem, pSeller, self.planet, self.bazaar.x, self.bazaar.z, self.bazaar.y, self.testPrice, self.testDurationHours)

    if success then
        self._hasSeeded = true
        self.lastItemId = itemId
        self:log(string.format("listed item oid=%s at %s (%.1f, %.1f) price=%d durationHours=%d", tostring(itemId), self.planet, self.bazaar.x, self.bazaar.y, self.testPrice, self.testDurationHours))

        if pInvoker ~= nil then
            CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Test listing created on Mos Eisley bazaar.")
        end

        return true
    end

    self:log("listing failed; cleaning up item")

    if itemObject ~= nil then
        itemObject:destroyObjectFromWorld(true)
        itemObject:destroyObjectFromDatabase()
    end

    if pInvoker ~= nil then
        CreatureObject(pInvoker):sendSystemMessage("[MarketSeeder] Failed to list bazaar item; check server logs.")
    end

    return false
end

