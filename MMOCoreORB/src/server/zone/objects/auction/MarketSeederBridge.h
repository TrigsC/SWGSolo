/*
                Copyright <SWGEmu>
    See file COPYING for copying conditions.*/

#ifndef MARKETSEEDERBRIDGE_H_
#define MARKETSEEDERBRIDGE_H_

#include "server/zone/objects/scene/SceneObject.h"
#include "server/zone/objects/creature/CreatureObject.h"

namespace server { namespace zone { class Zone; } }

struct lua_State;

class MarketSeederBridge {
public:
        static bool listOnBazaar(SceneObject* item, CreatureObject* seller, const String& planet, float x, float y, int price, int durationHours);

        static CreatureObject* getSystemSeller(server::zone::Zone* zone);

        static int luaListOnBazaar(lua_State* L);
        static int luaGetSystemSeller(lua_State* L);

private:
    static CreatureObject* ensureSystemSeller(server::zone::Zone* zone);
    static SceneObject*    findBazaarTerminal(server::zone::Zone* zone, float x, float y, float searchRadius);
};


#endif // MARKETSEEDERBRIDGE_H_
