/*
 * JediQueueCommand.h
 *
 *  Created on: Apr 9, 2015
 *      Author: valkyra
 */

#ifndef JEDIQUEUECOMMAND_H_
#define JEDIQUEUECOMMAND_H_


#include "server/zone/objects/creature/buffs/Buff.h"
#include "QueueCommand.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/managers/visibility/VisibilityManager.h"
#include "server/zone/objects/creature/buffs/SingleUseBuff.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "server/zone/managers/frs/FrsManager.h"
#include "server/zone/objects/creature/ai/AiAgent.h"

class JediQueueCommand : public QueueCommand {

protected:
	int forceCost;
	int duration;
	uint32 animationCRC;
	String clientEffect;
	float speedMod;
	int visMod;
	int buffClass;
	float frsLightForceCostModifier;
	float frsDarkForceCostModifier;
	float frsDarkExtraForceCostModifier;
	float frsLightExtraForceCostModifier;
	float frsLightBuffModifier;
	float frsDarkBuffModifier;
	float frsLightForcePowerModifier;
	float frsDarkForcePowerModifier;

	uint32 buffCRC;
	Vector<uint32> overrideableCRCs;
	Vector<uint32> blockingCRCs;
	Vector<unsigned int> singleUseEventTypes;


public:
	enum { BASE_BUFF, SINGLE_USE_BUFF };

	// For AI to understand Force Cost
	int getForceCost() const {
        return forceCost;
    }

	JediQueueCommand(const String& name, ZoneProcessServer* server) : QueueCommand(name, server) {
		forceCost = 0;
		duration = 0;
		animationCRC = 0;
		clientEffect = "";
		buffClass = BASE_BUFF;
		speedMod = 0;
		visMod = 10;
		buffCRC = 0;
		frsLightForceCostModifier = 0;
		frsDarkExtraForceCostModifier = 0;
		frsLightExtraForceCostModifier = 0;
		frsDarkForceCostModifier = 0;
		frsLightBuffModifier = 0;
		frsDarkBuffModifier = 0;
		frsLightForcePowerModifier = 0;
		frsDarkForcePowerModifier = 0;
	}

	int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const {
		return SUCCESS;
	}

	bool isJediQueueCommand() const {
		return true;
	}

	int doJediSelfBuffCommand(CreatureObject* creature) const {
		// first and foremost, we need to toggle this buff off if we already have it
		if (creature->hasBuff(buffCRC)) {
			creature->removeBuff(buffCRC);
			return SUCCESS;
		}

		// Do checks first.
		int res = doCommonJediSelfChecks(creature);

		if (res != SUCCESS)
			return res;

        return doBuff(creature);
	}

	int doBuff(CreatureObject* creature) const {
		ManagedReference<Buff*> buff = createJediSelfBuff(creature);

		// Return if buff is NOT valid.
		if (buff == nullptr)
			return GENERALERROR;

		Locker locker(buff);

		// Add buff.
		creature->addBuff(buff);

		// Force Cost.
		doForceCost(creature);

		// Client Effect.
		if (!clientEffect.isEmpty()) {
			creature->playEffect(clientEffect, "");
		}

		// Return.
		return SUCCESS;
	}

	int doJediForceCostCheck(CreatureObject* creature) const {
        // 1. PLAYER LOGIC
        if (creature->isPlayerCreature()) {
            ManagedReference<PlayerObject*> playerObject = creature->getPlayerObject();

            if (playerObject && playerObject->getForcePower() < getFrsModifiedForceCost(creature)) {
                creature->sendSystemMessage("@jedi_spam:no_force_power"); 
                return GENERALERROR;
            }
        }
        // 2. AI LOGIC
        else if (creature->isAiAgent()) {
            AiAgent* ai = creature->asAiAgent();
            if (ai != nullptr) {
                // Safety: Ensure a minimum cost so they can't spam free spells
                int cost = forceCost;
                if (cost <= 0) cost = 50; 

                if (ai->getCurrentForce() < cost) {
                    return GENERALERROR; // Out of Force
                }
            }
        }

        return SUCCESS;
    }

	int doCommonJediSelfChecks(CreatureObject* creature) const {
		int res = doCommonMedicalCommandChecks(creature);

		if (res != SUCCESS)
			return res;

		if (isWearingArmor(creature))
			return NOJEDIARMOR;

		for (int i=0; i < blockingCRCs.size(); ++i) {
			if (creature->hasBuff(blockingCRCs.get(i))) {
				return NOSTACKJEDIBUFF;
			}
		}

		res = doJediForceCostCheck(creature);
		return res;
	}

	ManagedReference<Buff*> createJediSelfBuff(CreatureObject* creature) const {
		for (int i=0; i < overrideableCRCs.size(); ++i) {
			int buff = overrideableCRCs.get(i);
			if (creature->hasBuff(buff)) {
				creature->removeBuff(buff);
			}
		}

		// Create buff object.
		ManagedReference<Buff*> buff = nullptr;

		if(buffClass == BASE_BUFF || singleUseEventTypes.size() == 0) {
			buff = new Buff(creature, buffCRC, duration, BuffType::JEDI);
		} else if(buffClass == SINGLE_USE_BUFF) {;
			SingleUseBuff* suBuff = new SingleUseBuff(creature, buffCRC, duration, BuffType::JEDI, getNameCRC());

			buff = suBuff;

			// Otherwise the compiler complains we're modifying member variables in a "const" function
			Vector<uint32> singleUseTypesCopy = singleUseEventTypes;

			Locker blocker(suBuff);
			suBuff->init(&singleUseTypesCopy);

		} else {
			error("Unknown buff type");
			return nullptr;
		}

		Locker locker(buff);

		if (speedMod > 0) {
			buff->setSpeedMultiplierMod(speedMod);
			buff->setAccelerationMultiplierMod(speedMod);
		}

		StringIdChatParameter start("jedi_spam", "apply_" + name);
		StringIdChatParameter end("jedi_spam", "remove_" + name);

		buff->setStartMessage(start);
		buff->setEndMessage(end);

		for (int i=0; i < skillMods.size(); ++i) {
			int modValue = skillMods.elementAt(i).getValue();
			int frsModifiedValue = getFrsModifiedBuffValue(creature, modValue);
			buff->setSkillModifier(skillMods.elementAt(i).getKey(), frsModifiedValue);
		}

		return buff;
	}

	int getFrsModifiedBuffValue(CreatureObject* player, int amount) const {
		PlayerObject* ghost = player->getPlayerObject();

		if (ghost == nullptr)
			return amount;

		Locker locker(player);

		FrsData* playerData = ghost->getFrsData();
		short councilType = playerData->getCouncilType();

		locker.release();

		float buffModifier = 0;
		int controlModifier = 0;

		if (councilType == FrsManager::COUNCIL_LIGHT) {
			controlModifier = player->getSkillMod("force_control_light");
			buffModifier = frsLightBuffModifier;
		} else if (councilType == FrsManager::COUNCIL_DARK) {
			controlModifier = player->getSkillMod("force_control_dark");
			buffModifier = frsDarkBuffModifier;
		}

		if (controlModifier == 0 || buffModifier == 0)
			return amount;

		return amount + (int)((controlModifier * buffModifier) + 0.5f);
	}


	int getFrsModifiedForceCost(CreatureObject* creature) const {
		// AI has no FRS stats, just return the base cost.
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

	float getFrsModifiedExtraForceCost(CreatureObject* creature, float val) const {
		ManagedReference<PlayerObject*> ghost = creature->getPlayerObject();

		if (ghost == nullptr)
			return val;

		Locker locker(creature);

		FrsData* playerData = ghost->getFrsData();
		int councilType = playerData->getCouncilType();

		locker.release();

		int manipulationMod = 0;
		float frsModifier = 0;

		if (councilType == FrsManager::COUNCIL_LIGHT) {
			manipulationMod = creature->getSkillMod("force_manipulation_light");
			frsModifier = frsLightExtraForceCostModifier;
		} else if (councilType == FrsManager::COUNCIL_DARK) {
			manipulationMod = creature->getSkillMod("force_manipulation_dark");
			frsModifier = frsDarkExtraForceCostModifier;
		}

		if (manipulationMod == 0 || frsModifier == 0)
			return val;

		return val + ((float)manipulationMod * frsModifier);
	}

	void doForceCost(CreatureObject* creature) const {
        // 1. PLAYER LOGIC
        if (creature->isPlayerCreature()) {
            ManagedReference<PlayerObject*> playerObject = creature->getPlayerObject();
            if (playerObject != nullptr) {
                playerObject->setForcePower(playerObject->getForcePower() - getFrsModifiedForceCost(creature));
                VisibilityManager::instance()->increaseVisibility(creature, visMod);
            }
        }
        // 2. AI LOGIC 
        else if (creature->isAiAgent()) {
            AiAgent* ai = creature->asAiAgent();
            if (ai != nullptr) {
                 int cost = forceCost;
                 if (cost <= 0) cost = 50; // Default fallback

                 int newForce = ai->getCurrentForce() - cost;
                 ai->setCurrentForce(newForce < 0 ? 0 : newForce);
                 
                 // Debug Logging (Optional)
                 StringBuffer msg;
                 msg << "AI Force Spell used. Cost: " << cost << " Remaining: " << newForce;
                 ai->info(msg.toString(), true);
            }
        }
    }

	void setForceCost(int fc) {
		forceCost = fc;
	}

	void setDuration(int dur) {
		duration = dur;
	}

	void setAnimationCRC(uint32 crc) {
		animationCRC = crc;
	}

	void setClientEffect(String ce) {
		clientEffect = ce;
	}

	void setSpeedMod(float sm) {
		speedMod = sm;
	}

	void setBuffClass(int bt) {
		buffClass = bt;
	}

	void setVisMod(int vm) {
		visMod = vm;
	}

	int getVisMod() const {
		return visMod;
	}

	void setFrsLightForceCostModifier(float val) {
		frsLightForceCostModifier = val;
	}
	void setFrsDarkForceCostModifier(float val) {
		frsDarkForceCostModifier = val;
	}
	void setFrsDarkExtraForceCostModifier(float val) {
		frsDarkExtraForceCostModifier = val;
	}
	void setFrsLightExtraForceCostModifier(float val) {
		frsLightExtraForceCostModifier = val;
	}
	void setFrsLightBuffModifier(float val) {
		frsLightBuffModifier = val;
	}
	void setFrsDarkBuffModifier(float val) {
		frsDarkBuffModifier = val;
	}
	void setFrsLightForcePowerModifier(float val) {
		frsLightForcePowerModifier = val;
	}
	void setFrsDarkForcePowerModifier(float val) {
		frsDarkForcePowerModifier = val;
	}
};


#endif /* SRC_SERVER_ZONE_OBJECTS_CREATURE_COMMANDS_JEDIQUEUECOMMAND_H_ */
