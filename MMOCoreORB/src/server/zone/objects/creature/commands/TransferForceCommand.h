#ifndef TRANSFERFORCECOMMAND_H_
#define TRANSFERFORCECOMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"
#include "server/zone/managers/frs/FrsManager.h"
#include "server/zone/objects/creature/ai/AiAgent.h"

class TransferForceCommand : public CombatQueueCommand {
public:

    TransferForceCommand(const String& name, ZoneProcessServer* server) : CombatQueueCommand(name, server) {
    }

    int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const {
        if (!checkStateMask(creature))
            return INVALIDSTATE;

        if (!checkInvalidLocomotions(creature))
            return INVALIDLOCOMOTION;

        if (isWearingArmor(creature)) {
            return NOJEDIARMOR;
        }

        ManagedReference<SceneObject*> object = server->getZoneServer()->getObject(target);

        if (object == nullptr || !object->isCreatureObject())
            return INVALIDTARGET;

        CreatureObject* targetCreature = cast<CreatureObject*>(object.get());

        if (targetCreature == nullptr || targetCreature->isDead() || targetCreature->isIncapacitated())
            return INVALIDTARGET;

        // Allow Players or AI
        if (!targetCreature->isPlayerCreature() && !targetCreature->isAiAgent())
            return INVALIDTARGET;

        if (!CollisionManager::checkLineOfSight(creature, targetCreature)) {
            creature->sendSystemMessage("@combat_effects:cansee_fail");
            return GENERALERROR;
        }

        if (!checkDistance(creature, targetCreature, range))
            return TOOFAR;

        if (targetCreature->isPlayerCreature() && !playerEntryCheck(creature, targetCreature)) {
            return GENERALERROR;
        }

        Locker clocker(targetCreature, creature);

        ManagedReference<PlayerObject*> targetGhost = targetCreature->getPlayerObject();
        ManagedReference<PlayerObject*> playerGhost = creature->getPlayerObject();

        if (creature->isPlayerCreature() && playerGhost == nullptr) return GENERALERROR;
        if (targetCreature->isPlayerCreature() && targetGhost == nullptr) return GENERALERROR;
        
        // Prevent transfer to self
        if (creature == targetCreature) return GENERALERROR;

        int transfer = System::random(75) + minDamage; 

        if (checkForArenaDuel(targetCreature)) {
            creature->sendSystemMessage("@jedi_spam:no_help_target"); 
            return GENERALERROR;
        }

        if (!targetCreature->isHealableBy(creature)) {
            creature->sendSystemMessage("@healing:pvp_no_help"); 
            return GENERALERROR;
        }

        // 1. CHECK ATTACKER FORCE
        int attackerForce = 0;
        if (creature->isPlayerCreature()) {
            attackerForce = playerGhost->getForcePower();
        } else if (creature->isAiAgent()) {
            // AI LOGIC UPDATE
            attackerForce = cast<AiAgent*>(creature)->getCurrentForce();
        }

        if (attackerForce < forceCost) {
            creature->sendSystemMessage("@jedi_spam:no_force_power"); 
            return GENERALERROR;
        }

        // 2. CHECK TARGET CAPACITY
        int targetCurrentForce = 0;
        int targetMaxForce = 0;

        if (targetCreature->isPlayerCreature()) {
            targetCurrentForce = targetGhost->getForcePower();
            targetMaxForce = targetGhost->getForcePowerMax();
        } else if (targetCreature->isAiAgent()) {
            // AI LOGIC UPDATE
            AiAgent* agent = cast<AiAgent*>(targetCreature);
            targetCurrentForce = agent->getCurrentForce();
            // TODO: Verify your IDL has getMaxForce(). If not, replace this call.
            targetMaxForce = agent->getMaxForce();
        }

        int forceSpace = targetMaxForce - targetCurrentForce;
        int forceTransfer = 0;

        if (forceSpace > 0) { 
            forceTransfer = forceSpace >= transfer ? transfer : forceSpace;
        } else {
            return GENERALERROR; // Target full
        }

        // 3. APPLY CHANGES
        
        // Remove Cost from Caster
        if (creature->isPlayerCreature()) {
            playerGhost->setForcePower(attackerForce - forceCost);
        } else if (creature->isAiAgent()) {
            // AI LOGIC UPDATE
            cast<AiAgent*>(creature)->setCurrentForce(attackerForce - forceCost);
        }

        // Add Transfer to Target
        if (targetCreature->isPlayerCreature()) {
            targetGhost->setForcePower(targetCurrentForce + forceTransfer);
        } else if (targetCreature->isAiAgent()) {
            // AI LOGIC UPDATE
            cast<AiAgent*>(targetCreature)->setCurrentForce(targetCurrentForce + forceTransfer);
        }

        uint32 animCRC = getAnimationString().hashCode();
        creature->doCombatAnimation(targetCreature, animCRC, 0x1, 0xFF);
        CombatManager::instance()->broadcastCombatSpam(creature, targetCreature, nullptr, forceTransfer, "cbt_spam", combatSpam, 0);

        if (creature->isPlayerCreature()) {
            VisibilityManager::instance()->increaseVisibility(creature, visMod);
            if (ConfigManager::instance()->useCovertOvertSystem())
                checkForTef(creature, targetCreature);
        }

        return SUCCESS;
    }

    float getCommandDuration(CreatureObject* object, const UnicodeString& arguments) const {
        return defaultTime;
    }

};

#endif //TRANSFERFORCECOMMAND_H_