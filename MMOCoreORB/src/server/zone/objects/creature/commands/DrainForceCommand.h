#ifndef DRAINFORCECOMMAND_H_
#define DRAINFORCECOMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"
#include "CombatQueueCommand.h"
#include "server/zone/objects/creature/ai/AiAgent.h"

class DrainForceCommand : public CombatQueueCommand {
public:

    DrainForceCommand(const String& name, ZoneProcessServer* server) : CombatQueueCommand(name, server) {
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

        // Allow targeting Players OR AI
        if (object == nullptr || !object->isCreatureObject())
            return INVALIDTARGET;

        CreatureObject* targetCreature = cast<CreatureObject*>(object.get());

        if (targetCreature == nullptr || targetCreature->isDead() || (targetCreature->isIncapacitated() && !targetCreature->isFeigningDeath()) || !targetCreature->isAttackableBy(creature))
            return INVALIDTARGET;

        // Valid targets: Players OR AI
        if (!targetCreature->isPlayerCreature() && !targetCreature->isAiAgent())
             return INVALIDTARGET;

        if (!checkDistance(creature, targetCreature, range))
            return TOOFAR;

        if (!CollisionManager::checkLineOfSight(creature, targetCreature)) {
            creature->sendSystemMessage("@combat_effects:cansee_fail");
            return GENERALERROR;
        }

        if (targetCreature->isPlayerCreature() && !playerEntryCheck(creature, targetCreature)) {
            return GENERALERROR;
        }

        Locker clocker(targetCreature, creature);

        ManagedReference<PlayerObject*> targetGhost = targetCreature->getPlayerObject();
        ManagedReference<PlayerObject*> playerGhost = creature->getPlayerObject();

        // Safety: Only fail if we EXPECT a ghost (Player) but don't find one
        if (creature->isPlayerCreature() && playerGhost == nullptr) return GENERALERROR;
        if (targetCreature->isPlayerCreature() && targetGhost == nullptr) return GENERALERROR;

        CombatManager* manager = CombatManager::instance();
        if (manager == nullptr)
            return GENERALERROR;

        if (manager->startCombat(creature, targetCreature, false)) { 
            
            // 1. GET ATTACKER FORCE DATA (CAPACITY CHECK)
            int attackerCurrentForce = 0;
            int attackerMaxForce = 0;

            if (creature->isPlayerCreature()) {
                attackerCurrentForce = playerGhost->getForcePower();
                attackerMaxForce = playerGhost->getForcePowerMax();
            } else if (creature->isAiAgent()) {
                AiAgent* agent = cast<AiAgent*>(creature);
                // AI LOGIC UPDATE: Use your custom methods
                attackerCurrentForce = agent->getCurrentForce();
                
                // TODO: Verify your IDL has getMaxForce(). If not, replace this call.
                attackerMaxForce = agent->getMaxForce(); 
            }

            int forceSpace = attackerMaxForce - attackerCurrentForce;

            if (forceSpace <= 0) // Cannot Drain if attacker is full
                return GENERALERROR;

            if (attackerCurrentForce < forceCost) {
                creature->sendSystemMessage("@jedi_spam:no_force_power"); 
                return GENERALERROR;
            }

            int drain = System::random(maxDamage);

            // 2. GET TARGET FORCE DATA
            int targetForce = 0;

            if (targetCreature->isPlayerCreature()) {
                targetForce = targetGhost->getForcePower();
            } else if (targetCreature->isAiAgent()) {
                // AI LOGIC UPDATE: Use your custom methods
                AiAgent* targetAgent = cast<AiAgent*>(targetCreature);
                targetForce = targetAgent->getCurrentForce();
            }

            if (targetForce <= 0) {
                creature->sendSystemMessage("@jedi_spam:target_no_force"); 
                return GENERALERROR;
            }

            // 3. CALCULATE DRAIN AMOUNT
            int forceDrain = targetForce >= drain ? drain : targetForce; 

            if (forceDrain > forceSpace) {
                forceDrain = forceSpace; 
            }

            // 4. APPLY CHANGES (ATTACKER GAINS, TARGET LOSES)
            
            // Attacker Gains (Net = Gain - Cost)
            int netChange = forceDrain - forceCost;
            
            if (creature->isPlayerCreature()) {
                playerGhost->setForcePower(attackerCurrentForce + netChange);
            } else if (creature->isAiAgent()) {
                // AI LOGIC UPDATE
                cast<AiAgent*>(creature)->setCurrentForce(attackerCurrentForce + netChange);
            }

            // Target Loses
            if (targetCreature->isPlayerCreature()) {
                targetGhost->setForcePower(targetForce - forceDrain);
            } else if (targetCreature->isAiAgent()) {
                // AI LOGIC UPDATE
                cast<AiAgent*>(targetCreature)->setCurrentForce(targetForce - forceDrain);
            }

            // 5. ANIMATION & SPAM
            uint32 animCRC = getAnimationString().hashCode();
            creature->doCombatAnimation(targetCreature, animCRC, 0x1, 0xFF);
            manager->broadcastCombatSpam(creature, targetCreature, nullptr, forceDrain, "cbt_spam", combatSpam, 1);

            // Force Absorb Calculation
            if (targetCreature->getSkillMod("force_absorb") > 0) {
                float drainAbsorb = forceDrain * 0.4f;
                targetCreature->notifyObservers(ObserverEventType::FORCEABSORB, targetCreature, drainAbsorb);
                manager->sendMitigationCombatSpam(targetCreature, nullptr, drainAbsorb, 0x04); 
            }

            // TEF / Visibility (Only for Player Attackers)
            if (creature->isPlayerCreature()) {
                VisibilityManager::instance()->increaseVisibility(creature, visMod);
                bool shouldGcwCrackdownTef = false, shouldGcwTef = false, shouldBhTef = false;
                manager->checkForTefs(creature, targetCreature, &shouldGcwCrackdownTef, &shouldGcwTef, &shouldBhTef);
                if (shouldGcwCrackdownTef || shouldGcwTef || shouldBhTef) {
                    playerGhost->updateLastCombatActionTimestamp(shouldGcwCrackdownTef, shouldGcwTef, shouldBhTef);
                }
            }

            return SUCCESS;
        }

        return GENERALERROR;
    }

    float getCommandDuration(CreatureObject* object, const UnicodeString& arguments) const {
        float combatHaste = object->getSkillMod("combat_haste");

        if (combatHaste > 0) {
            return defaultTime * (1.f - (combatHaste / 100.f));
        } else {
            return defaultTime;
        }
    }
};

#endif //DRAINFORCECOMMAND_H_