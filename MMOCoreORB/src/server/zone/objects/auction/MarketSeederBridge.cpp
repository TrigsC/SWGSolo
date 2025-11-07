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
#include "engine/lua/Lua.h"

static Mutex systemSellerMutex;
static ManagedReference<CreatureObject*> systemSellerRef;

static Logger& marketSeederLogger() {
        static Logger logger("MarketSeeder");
        return logger;
}

CreatureObject* MarketSeederBridge::ensureSystemSeller() {
        ManagedReference<CreatureObject*> seller = systemSellerRef;

        if (seller != nullptr)
                return seller.get();

        Locker guard(&systemSellerMutex);

        seller = systemSellerRef;

        if (seller != nullptr)
                return seller.get();

        ZoneServer* zoneServer = ServerCore::getZoneServer();

        if (zoneServer == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: Zone server unavailable";
                return nullptr;
        }

        ManagedReference<CreatureManager*> creatureManager = zoneServer->getCreatureManager();

        if (creatureManager == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: CreatureManager unavailable";
                return nullptr;
        }

        const String templatePath("/object/mobile/shared_dressed_commoner_tatooine_male_01.iff");
        uint32 templateCRC = templatePath.hashCode();

        ManagedReference<CreatureObject*> created = creatureManager->createCreature(templateCRC, false);

        if (created == nullptr) {
                marketSeederLogger().error() << "ensureSystemSeller: Failed to create system seller using template " << templatePath;
                return nullptr;
        }

        Locker sellerLocker(created);

        created->setFirstName("MarketSeeder");
        created->setLastName("System");
        created->setCustomObjectName("Market Seeder", true);
        created->setBankCredits(500000);
        created->setCashCredits(500000);

        systemSellerRef = created;

        marketSeederLogger().info(true) << "ensureSystemSeller: initialized system seller oid=" << created->getObjectID();

        return created;
}

CreatureObject* MarketSeederBridge::getSystemSeller() {
        return ensureSystemSeller();
}

SceneObject* MarketSeederBridge::findBazaarTerminal(Zone* zone, float x, float y, float searchRadius) {
        if (zone == nullptr)
                return nullptr;

        SortedVector< ManagedReference<TreeEntry*> > nearby;
        zone->getInRangeObjects(x, 0, y, searchRadius, &nearby, true, true);

        SceneObject* closest = nullptr;
        float closestDistanceSq = FLT_MAX;

        for (int i = 0; i < nearby.size(); ++i) {
                ManagedReference<TreeEntry*> entry = nearby.get(i);
                SceneObject* candidate = entry.castTo<SceneObject*>();

                if (candidate == nullptr || !candidate->isBazaarTerminal())
                        continue;

                float dx = candidate->getPositionX() - x;
                float dy = candidate->getPositionY() - y;
                float distSq = dx * dx + dy * dy;

                if (distSq < closestDistanceSq) {
                        closest = candidate;
                        closestDistanceSq = distSq;
                }
        }

        return closest;
}

bool MarketSeederBridge::listOnBazaar(SceneObject* item, CreatureObject* seller, const String& planet, float x, float y, int price, int durationHours) {
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

        CreatureObject* resolvedSeller = seller != nullptr ? seller : getSystemSeller();

        if (resolvedSeller == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Unable to resolve seller";
                return false;
        }

        Zone* zone = zoneServer->getZone(planet);

        if (zone == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: Unable to resolve zone for planet " << planet;
                return false;
        }

        SceneObject* vendor = findBazaarTerminal(zone, x, y, 32.0f);

        if (vendor == nullptr)
                vendor = findBazaarTerminal(zone, x, y, 128.0f);

        if (vendor == nullptr) {
                marketSeederLogger().error() << "listOnBazaar: No bazaar terminal found near (" << x << ", " << y << ") on " << planet;
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
                resolvedSeller->setBankCredits(AuctionManager::SALESFEE * 50);
        }

        if (durationHours <= 0)
                durationHours = 24;

        UnicodeString description(L"Market Seeder test listing");

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

int MarketSeederBridge::luaGetSystemSeller(lua_State* L) {
        CreatureObject* seller = getSystemSeller();

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
        float y = (float) lua_tonumber(L, 5);
        int price = lua_tointeger(L, 6);
        int durationHours = argumentCount >= 7 ? lua_tointeger(L, 7) : 24;

        bool result = listOnBazaar(item, seller, planet, x, y, price, durationHours);

        lua_pushboolean(L, result);
        return 1;
}
