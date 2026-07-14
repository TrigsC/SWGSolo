/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef FORCEARMOR2COMMAND_H_
#define FORCEARMOR2COMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"

class ForceArmor2Command : public JediQueueCommand {
public:

	ForceArmor2Command(const String& name, ZoneProcessServer* server) : JediQueueCommand(name, server) {
		buffCRC = BuffCRC::JEDI_FORCE_ARMOR_2;
		overrideableCRCs.add(BuffCRC::JEDI_FORCE_ARMOR_1);
		singleUseEventTypes.add(ObserverEventType::FORCEARMOR);
		skillMods.put("force_armor", 45);
	}

	int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const override {
		return doJediSelfBuffCommand(creature);
	}

	void handleBuff(SceneObject* sceneObject, ManagedObject* object, int64 param) const override {
		handleMitigationForceCost(sceneObject->asCreatureObject(),
			BuffCRC::JEDI_FORCE_ARMOR_2, param, 0.3f,
			"clienteffect/pl_force_armor_hit.cef");
	}

};

#endif //FORCEARMOR2COMMAND_H_
