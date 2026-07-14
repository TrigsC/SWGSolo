/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef FORCEARMOR1COMMAND_H_
#define FORCEARMOR1COMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"

class ForceArmor1Command : public JediQueueCommand {
public:

	ForceArmor1Command(const String& name, ZoneProcessServer* server) : JediQueueCommand(name, server) {
		buffCRC = BuffCRC::JEDI_FORCE_ARMOR_1;
		blockingCRCs.add(BuffCRC::JEDI_FORCE_ARMOR_2);
		singleUseEventTypes.add(ObserverEventType::FORCEARMOR);
		skillMods.put("force_armor", 25);
	}

	int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const override {
		return doJediSelfBuffCommand(creature);
	}

	void handleBuff(SceneObject* sceneObject, ManagedObject* object, int64 param) const override {
		handleMitigationForceCost(sceneObject->asCreatureObject(),
			BuffCRC::JEDI_FORCE_ARMOR_1, param, 0.5f,
			"clienteffect/pl_force_armor_hit.cef");
	}

};

#endif //FORCEARMOR1COMMAND_H_
