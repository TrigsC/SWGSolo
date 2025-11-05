/*
                Copyright <SWGEmu>
    See file COPYING for copying conditions.*/

#ifndef SEEDMARKETCOMMAND_H_
#define SEEDMARKETCOMMAND_H_

#include "server/zone/objects/creature/commands/QueueCommand.h"
#include "server/zone/managers/director/DirectorManager.h"
#include "server/zone/objects/player/PlayerObject.h"

class SeedMarketCommand : public QueueCommand {
public:
        SeedMarketCommand(const String& name, ZoneProcessServer* server)
                : QueueCommand(name, server) {
        }

        int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const {
                if (!checkStateMask(creature))
                        return INVALIDSTATE;

                if (!checkInvalidLocomotions(creature))
                        return INVALIDLOCOMOTION;

                ManagedReference<PlayerObject*> ghost = creature->getPlayerObject();

                if (ghost == nullptr || !ghost->isPrivileged()) {
                        creature->sendSystemMessage("@player_structure:no_permissions");
                        return INSUFFICIENTPERMISSION;
                }

                Lua* lua = DirectorManager::instance()->getLuaInstance();

                if (lua == nullptr)
                        return GENERALERROR;

                Reference<LuaFunction*> seedFunction = lua->createFunction("MarketSeeder", "seed_once", 0);

                if (seedFunction == nullptr)
                        return GENERALERROR;

                *seedFunction << creature;
                *seedFunction << arguments.toString();

                seedFunction->callFunction();

                return SUCCESS;
        }
};

#endif // SEEDMARKETCOMMAND_H_
