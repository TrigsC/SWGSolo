/*
                Copyright <SWGEmu>
    See file COPYING for copying conditions.*/

#include "server/zone/objects/auction/MarketSeederBridge.h"

#include <cfloat>

#include "server/ServerCore.h"
#include "server/zone/Zone.h"
#include "server/zone/ZoneServer.h"
#include "server/zone/InRangeObjectsVector.h"
#include "server/zone/managers/auction/AuctionManager.h"
#include "server/zone/managers/auction/AuctionsMap.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/managers/object/ObjectManager.h"
#include "server/zone/objects/auction/AuctionItem.h"
#include "server/zone/objects/scene/SceneObjectType.h"
#include "server/zone/objects/tangible/TangibleObject.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "system/lang/String.h"
#include "system/lang/UnicodeString.h"
#include "system/util/SortedVector.h"
#include "system/thread/Locker.h"
#include "system/thread/Mutex.h"
#include "system/lang/ref/Reference.h"
#include "templates/manager/TemplateManager.h"
#include "templates/SharedObjectTemplate.h"
#include "engine/lua/Lua.h"

static Mutex systemSellerMutex;
static ManagedReference<CreatureObject*> systemSellerRef;

static Logger& marketSeederLogger() {
        static Logger logger("MarketSeeder");
        return logger;
}

bool MarketSeederBridge::templateExists(const String& templatePath) {
        TemplateManager* templateManager = TemplateManager::instance();

        if (templateManager == nullptr) {
                marketSeederLogger().error() << "templateExists: TemplateManager unavailable";
                return false;
        }

        if (templatePath.isEmpty()) {
                marketSeederLogger().error() << "templateExists: template path was empty";
                return false;
        }

        uint32 templateCRC = templatePath.hashCode();

        if (templateManager->getTemplate(templateCRC) != nullptr)
                return true;

        marketSeederLogger().error() << "templateExists: unknown template " << templatePath
                << " (crc=" << String::format("0x%08X", templateCRC) << ")";

        return false;
}

CreatureObject* MarketSeederBridge::ensureSystemSeller(Zone* zone) {
        ManagedReference<CreatureObject*> seller = systemSellerRef;

        if (seller != nullptr)
                return seller.get();

        Locker guard(&systemSellerMutex);

        seller = systemSellerRef;

        if (seller != nullptr)
                return seller.get();

        if (zone == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: Zone unavailable";
                return nullptr;
        }

        ManagedReference<CreatureManager*> creatureManager = zone->getCreatureManager();

        if (creatureManager == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: CreatureManager unavailable for zone " << zone->getZoneName();
                return nullptr;
        }

        const String templatePath("object/creature/player/human_male.iff");
        uint32 templateCRC = templatePath.hashCode();

        if (TemplateManager::instance()->getTemplate(templateCRC) == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: Template not found " << templatePath;
                return nullptr;
        }

        ManagedReference<CreatureObject*> created = creatureManager->createCreature(templateCRC, false);

        if (created == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: Failed to create system seller using template " << templatePath;
                return nullptr;
        }

        Locker sellerLocker(created);

        ManagedReference<PlayerObject*> ghost = created->getPlayerObject();
        
        created->setFirstName("MarketSeeder");
        created->setLastName("System");
        bool notifyCustomName = ghost != nullptr;

        if (!notifyCustomName) {
                marketSeederLogger().warning() << "ensureSystemSeller: created creature missing PlayerObject; skipping client custom name update";
        }

        created->setCustomObjectName("Market Seeder", notifyCustomName);


        created->addBankCredits(AuctionManager::SALESFEE * 50, false);
        created->addCashCredits(AuctionManager::SALESFEE * 50, false);

        systemSellerRef = created;

        marketSeederLogger().info(true) << "ensureSystemSeller: initialized system seller oid=" << created->getObjectID();

        return created;
}

CreatureObject* MarketSeederBridge::getSystemSeller(Zone* zone) {
        return ensureSystemSeller(zone);
}

SceneObject* MarketSeederBridge::findBazaarTerminal(Zone* zone, float x, float z, float y, float searchRadius) {
        if (zone == nullptr)
                return nullptr;

        SortedVector< ManagedReference<TreeEntry*> > nearby;
        zone->getInRangeObjects(x, z, y, searchRadius, &nearby, true, true);

        SceneObject* closest = nullptr;
        float closestDistanceSq = FLT_MAX;

        for (int i = 0; i < nearby.size(); ++i) {
                ManagedReference<TreeEntry*> entry = nearby.get(i);
                SceneObject* candidate = entry.castTo<SceneObject*>();

                if (candidate == nullptr || !candidate->isBazaarTerminal())
                        continue;

                float dx = candidate->getPositionX() - x;
                float dy = candidate->getPositionY() - y;
                float dz = candidate->getPositionZ() - z;
                float distSq = dx*dx + dy*dy + dz*dz;

                if (distSq < closestDistanceSq) {
                        closest = candidate;
                        closestDistanceSq = distSq;
                }
        }

        return closest;
}

bool MarketSeederBridge::listOnBazaar(SceneObject* item, CreatureObject* seller, const String& planet, float x, float z, float y, int price, int durationHours) {
        if (item == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: item was nullptr";
                return false;
        }

        ZoneServer* zoneServer = ServerCore::getZoneServer();

        if (zoneServer == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Zone server unavailable";
                return false;
        }

        ManagedReference<AuctionManager*> auctionManager = zoneServer->getAuctionManager();

        if (auctionManager == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: AuctionManager unavailable";
                return false;
        }

        Zone* zone = zoneServer->getZone(planet);

        if (zone == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Unable to resolve zone for planet " << planet;
                return false;
        }

        CreatureObject* resolvedSeller = seller != nullptr ? seller : getSystemSeller(zone);

        if (resolvedSeller == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Unable to resolve seller";
                return false;
        }

        if (resolvedSeller->getPlayerObject() == nullptr) {
                marketSeederLogger().warning() << "listOnBazaar: Seller " << resolvedSeller->getObjectID() << " missing PlayerObject; continuing";
        }

        SceneObject* vendor = findBazaarTerminal(zone, x, z, y, 32.0f);

        if (vendor == nullptr)
                vendor = findBazaarTerminal(zone, x, z, y, 128.0f);

        if (vendor == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: No bazaar terminal found near (" << x << ", " << z << ", " << y << ") on " << planet;
                return false;
        }

        ManagedReference<SceneObject*> inventory = resolvedSeller->getSlottedObject("inventory");

        if (inventory == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Seller missing inventory";
                return false;
        }

        if (!item->isASubChildOf(resolvedSeller)) {
                Locker sellerLocker(resolvedSeller);
                Locker inventoryLocker(inventory);
                Locker itemLocker(item);

                if (!inventory->transferObject(item, -1, true)) {
                        marketSeederLogger().error() << "listOnBazaar: Failed to transfer item " << item->getObjectID() << " into seller inventory";
                        return false;
                }
        }

        if (resolvedSeller->getBankCredits() < AuctionManager::SALESFEE * 10) {
                Locker sellerLocker(resolvedSeller);
                resolvedSeller->addBankCredits(AuctionManager::SALESFEE * 50, false);
        }

        if (durationHours <= 0)
                durationHours = 24;

        UnicodeString description("Market Seeder test listing");

        auctionManager->addSaleItem(resolvedSeller, item->getObjectID(), vendor, description, price, durationHours, false, false);

        ManagedReference<AuctionItem*> listing = auctionManager->getAuctionMap()->getItem(item->getObjectID());

        if (listing == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Listing for item " << item->getObjectID() << " not found after addSaleItem";
                return false;
        }

        marketSeederLogger().info(true) << "[MarketSeeder] Created item " << item->getObjectID() << " and listed on bazaar terminal " << vendor->getObjectID()
                << " at (" << x << ", " << y << ") price=" << price << " durationHours=" << durationHours;

        return true;
}

SceneObject* MarketSeederBridge::createItemForSeller(CreatureObject* seller, SceneObject* container, const String& templatePath, int slot, bool allowOverflow) {
        if (seller == nullptr) {
                marketSeederLogger().error() << "createItemForSeller: seller was nullptr";
                return nullptr;
        }

        if (container == nullptr) {
                marketSeederLogger().error() << "createItemForSeller: container was nullptr";
                return nullptr;
        }

        if (templatePath.isEmpty()) {
                marketSeederLogger().error() << "createItemForSeller: template path was empty";
                return nullptr;
        }

        ZoneServer* zoneServer = seller->getZoneServer();

        if (zoneServer == nullptr) {
                marketSeederLogger().error() << "createItemForSeller: Zone server unavailable";
                return nullptr;
        }

        TemplateManager* templateManager = TemplateManager::instance();

        if (templateManager == nullptr) {
                marketSeederLogger().error() << "createItemForSeller: TemplateManager unavailable";
                return nullptr;
        }

        uint32 templateCRC = templatePath.hashCode();
        SharedObjectTemplate* templateData = templateManager->getTemplate(templateCRC);

        if (templateData == nullptr) {
                marketSeederLogger().error() << "createItemForSeller: Template not found " << templatePath
                        << " (crc=" << String::format("0x%08X", templateCRC) << ")";
                return nullptr;
        }

        ManagedReference<SceneObject*> item = zoneServer->createObject(templateCRC, 1);

        if (item == nullptr) {
                marketSeederLogger().error() << "createItemForSeller: Failed to create object from template " << templatePath;
                return nullptr;
        }

        {
                Locker sellerLocker(seller);
                Locker containerLocker(container);
                Locker itemLocker(item);

                if (!container->transferObject(item, slot, true, allowOverflow)) {
                        marketSeederLogger().error() << "createItemForSeller: Failed to transfer created item into container";
                        item->destroyObjectFromDatabase(true);
                        return nullptr;
                }

                item->_setUpdated(true);

                ManagedReference<SceneObject*> parent = item->getParentRecursively(SceneObjectType::PLAYERCREATURE);

                if (parent != nullptr && parent->isPlayerCreature())
                        item->sendTo(parent, true);
        }
	
  		AuctionItem* aitem  = new AuctionItem(item->getObjectID());
		marketSeederLogger().info(true) << "createItemForSeller: pitem oid=" << aitem->getObjectID()
                << " seller oid " << seller->getObjectID();
	
		Locker locker(aitem);
	
        aitem->setOwnerID(seller->getObjectID());
	    aitem->setOwnerName(seller->getFirstName());

        ObjectManager::instance()->persistSceneObjectsRecursively(item, 1);

        marketSeederLogger().info(true) << "createItemForSeller: created item oid=" << item->getObjectID()
                << " from template " << templatePath;

        return item.get();
}

int MarketSeederBridge::luaGetSystemSeller(lua_State* L) {
        Zone* zone = nullptr;
        ZoneServer* zoneServer = ServerCore::getZoneServer();

        if (zoneServer == nullptr) {
                marketSeederLogger().error() << "luaGetSystemSeller: Zone server unavailable";
                lua_pushnil(L);
                return 1;
        }

        int argumentCount = lua_gettop(L);

        if (argumentCount >= 1 && lua_isstring(L, 1)) {
                String requestedPlanet = lua_tostring(L, 1);

                if (!requestedPlanet.isEmpty())
                        zone = zoneServer->getZone(requestedPlanet);
        }

        if (zone == nullptr)
                zone = zoneServer->getZone("tatooine");

        if (zone == nullptr) {
                marketSeederLogger().error() << "luaGetSystemSeller: Unable to resolve zone for system seller";
                lua_pushnil(L);
                return 1;
        }

        CreatureObject* seller = getSystemSeller(zone);

        if (seller != nullptr)
                lua_pushlightuserdata(L, seller);
        else
                lua_pushnil(L);

        return 1;
}

int MarketSeederBridge::luaListOnBazaar(lua_State* L) {
        int argumentCount = lua_gettop(L);

        if (argumentCount < 6) {
                marketSeederLogger().error() << "luaListOnBazaar: expected at least 6 arguments, received " << argumentCount;
                lua_pushboolean(L, false);
                return 1;
        }

        SceneObject* item = static_cast<SceneObject*>(lua_touserdata(L, 1));
        CreatureObject* seller = nullptr;

        if (!lua_isnil(L, 2))
                seller = static_cast<CreatureObject*>(lua_touserdata(L, 2));

        String planet = lua_tostring(L, 3);
        float x = (float) lua_tonumber(L, 4);
        float z = (float) lua_tonumber(L, 5);
        float y = (float) lua_tonumber(L, 6);
        int price = lua_tointeger(L, 7);
        int durationHours = argumentCount >= 8 ? lua_tointeger(L, 8) : 24;

        bool result = listOnBazaar(item, seller, planet, x, z, y, price, durationHours);

        lua_pushboolean(L, result);
        return 1;
}

int MarketSeederBridge::luaTemplateExists(lua_State* L) {
        int argumentCount = lua_gettop(L);

        if (argumentCount < 1 || !lua_isstring(L, 1)) {
                lua_pushboolean(L, false);
                return 1;
        }

        String templatePath = lua_tostring(L, 1);
        bool exists = templateExists(templatePath);

        lua_pushboolean(L, exists);
        return 1;
}

int MarketSeederBridge::luaCreateItemForSeller(lua_State* L) {
        int argumentCount = lua_gettop(L);

        if (argumentCount < 3) {
                marketSeederLogger().error() << "luaCreateItemForSeller: expected at least 3 arguments, received " << argumentCount;
                lua_pushnil(L);
                return 1;
        }

        CreatureObject* seller = static_cast<CreatureObject*>(lua_touserdata(L, 1));
        SceneObject* container = static_cast<SceneObject*>(lua_touserdata(L, 2));
        String templatePath = lua_tostring(L, 3);
        int slot = argumentCount >= 4 ? lua_tointeger(L, 4) : -1;
        bool allowOverflow = argumentCount >= 5 ? lua_toboolean(L, 5) : false;

        SceneObject* item = createItemForSeller(seller, container, templatePath, slot, allowOverflow);

        if (item != nullptr)
                lua_pushlightuserdata(L, item);
        else
                lua_pushnil(L);

        return 1;
}
