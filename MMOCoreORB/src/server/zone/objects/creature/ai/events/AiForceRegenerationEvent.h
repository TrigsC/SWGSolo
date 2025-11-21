/*
 * AiBehaviorEvent.h
 *
 *  Created on: 11/21/2025
 *      Author: trigues
 */

#ifndef AIFORCEREGENERATIONEVENT_H_
#define AIFORCEREGENERATIONEVENT_H_

#include "server/zone/objects/creature/ai/AiAgent.h"

namespace server {
namespace zone {
namespace objects {
namespace creature {
namespace ai {
namespace events {

class AiForceRegenerationEvent : public Task {
    ManagedWeakReference<AiAgent*> weakAgent;

public:
    AiForceRegenerationEvent(AiAgent* agent) : Task() {
        weakAgent = agent;
    }

    void run() {
        ManagedReference<AiAgent*> agent = weakAgent.get();

        if (agent == nullptr) {
            return;
        }

        Locker lock(agent);

        if (agent->isDead() || agent->getZone() == nullptr) {
            return; // Don't regen if dead or removed from world
        }

        // Call the regen function on the agent
        agent->doForceRegen();
    }
};

} // namespace events
} // namespace ai
} // namespace creature
} // namespace objects
} // namespace zone
} // namespace server

using namespace server::zone::objects::creature::ai::events;

#endif /*AIFORCEREGENERATIONEVENT_H_*/