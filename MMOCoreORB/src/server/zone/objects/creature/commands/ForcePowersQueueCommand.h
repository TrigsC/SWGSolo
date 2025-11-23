/*
 * ForcePowersQueueCommand.h
 *
 *  Created on: 24/05/2010
 *      Author: Valkyra
 */

#ifndef FORCEPOWERSQUEUECOMMAND_H_
#define FORCEPOWERSQUEUECOMMAND_H_

#include"server/zone/ZoneServer.h"
#include "server/zone/objects/scene/SceneObject.h"
#include "server/zone/managers/combat/CombatManager.h"
#include "server/zone/managers/combat/CreatureAttackData.h"
#include "server/zone/managers/collision/CollisionManager.h"
#include "CombatQueueCommand.h"
#include "server/zone/managers/visibility/VisibilityManager.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/creature/commands/ForcePowersQueueCommand.h"
#include "server/zone/objects/creature/commands/JediQueueCommand.h"

class ForcePowersQueueCommand : public CombatQueueCommand {
public:
	
	// For AI to understand Force Cost
	int getForceCost() const {
        return forceCost;
    }

	ForcePowersQueueCommand(const String& name, ZoneProcessServer* server) : CombatQueueCommand(name, server) {
		visMod = 25;
	}

	int doCombatAction(CreatureObject* creature, const uint64& target, const UnicodeString& arguments = "") const {
        ManagedReference<SceneObject*> targetObject = server->getZoneServer()->getObject(target);

        if (targetObject == nullptr || !targetObject->isTangibleObject() || targetObject == creature)
            return INVALIDTARGET;

        float checkRange = range;

        if (creature->isProne())
            return NOPRONE;

        if(!checkDistance(creature, targetObject, checkRange))
            return TOOFAR;

        if (!CollisionManager::checkLineOfSight(creature, targetObject)) {
            creature->sendSystemMessage("@cbt_spam:los_fail");// "You lost sight of your target."
            return GENERALERROR;
        }

        if (!playerEntryCheck(creature, targetObject->asTangibleObject())) {
            return GENERALERROR;
        }

        ManagedReference<PlayerObject*> ghost = creature->getPlayerObject();

        // -------------------------------------------------------
        // 1. PRE-FLIGHT CHECK: Do we have enough Force?
        // -------------------------------------------------------
        if (ghost != nullptr && ghost->getForcePower() < getFrsModifiedForceCost(creature)) {
            creature->sendSystemMessage("@jedi_spam:no_force_power"); 
            return GENERALERROR;
        } 
        else if (creature->isAiAgent()) {
            AiAgent* ai = creature->asAiAgent();
            // Safety: If template has 0 cost, default to 50 so they can't spam forever
            int cost = forceCost;
            if (cost <= 0) cost = 50;

            if (ai != nullptr && ai->getCurrentForce() < cost) {
                return GENERALERROR; // AI is out of force
            }
        }
        // -------------------------------------------------------

        CombatManager* combatManager = CombatManager::instance();

        try {
            int res = combatManager->doCombatAction(creature, creature->getWeapon(), cast<TangibleObject*>(targetObject.get()), CreatureAttackData(arguments, this, target));

            switch (res) {
            case -1:
                return INVALIDTARGET;
            case -3:
                return GENERALERROR;
            }

            // -------------------------------------------------------
            // 2. DEDUCTION: Pay the bill
            // -------------------------------------------------------
            if (ghost != nullptr) {
                ghost->setForcePower(ghost->getForcePower() - getFrsModifiedForceCost(creature));
            } 
            else if (creature->isAiAgent()) {
                AiAgent* ai = creature->asAiAgent();
                if (ai != nullptr) {
                    int cost = forceCost;
                    if (cost <= 0) cost = 50;

                    int newForce = ai->getCurrentForce() - cost;
                    ai->setCurrentForce(newForce < 0 ? 0 : newForce);

                    // --- DEBUG LOGGING ---
                    // Uncomment this to verify it works in the console!
                    StringBuffer msg;
                    msg << "AI Force Power Used (" << name << "). Cost: " << cost << " Rem: " << newForce;
                    ai->info(msg.toString(), true);
                }
            }
            // -------------------------------------------------------

        } catch (Exception& e) {
            error("unreported exception caught in ForcePowersQueueCommand::doCombatAction");
            error(e.getMessage());
            e.printStackTrace();
        }

        // Increase Visibility for Force Power (Players only)
        if (ghost != nullptr)
            VisibilityManager::instance()->increaseVisibility(creature, visMod);

        return SUCCESS;
    }

	int getFrsModifiedForceCost(CreatureObject* creature) const {
		// AI Check
		if (creature->isAiAgent()) {
             return forceCost;
        }

		ManagedReference<PlayerObject*> ghost = creature->getPlayerObject();

		if (ghost == nullptr)
			return forceCost;

		Locker locker(creature);

		FrsData* playerData = ghost->getFrsData();
		int councilType = playerData->getCouncilType();

		locker.release();

		int manipulationMod = 0;
		float frsModifier = 0;

		if (councilType == FrsManager::COUNCIL_LIGHT) {
			manipulationMod = creature->getSkillMod("force_manipulation_light");
			frsModifier = frsLightForceCostModifier;
		} else if (councilType == FrsManager::COUNCIL_DARK) {
			manipulationMod = creature->getSkillMod("force_manipulation_dark");
			frsModifier = frsDarkForceCostModifier;
		}

		if (manipulationMod == 0 || frsModifier == 0)
			return forceCost;

		return forceCost + (int)((manipulationMod * frsModifier) + .5);
	}

	float getCommandDuration(CreatureObject *object, const UnicodeString& arguments) const {
		float combatHaste = object->getSkillMod("combat_haste");

		if (combatHaste > 0)
			return speed * (1.f - (combatHaste / 100.f));
		else
			return speed;
	}

	virtual bool isJediCombatQueueCommand() {
		return true;
	}

};

#endif /* FORCEPOWERSQUEUECOMMAND_H_ */
