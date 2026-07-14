/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef FORCESHIELD2COMMAND_H_
#define FORCESHIELD2COMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"

class ForceShield2Command : public JediQueueCommand {
public:

	ForceShield2Command(const String& name, ZoneProcessServer* server) : JediQueueCommand(name, server) {
		buffCRC = BuffCRC::JEDI_FORCE_SHIELD_2;
		overrideableCRCs.add(BuffCRC::JEDI_FORCE_SHIELD_1);
		singleUseEventTypes.add(ObserverEventType::FORCESHIELD);
		skillMods.put("force_shield", 45);
	}

	int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const override {
		return doJediSelfBuffCommand(creature);
	}

	void handleBuff(SceneObject* creature, ManagedObject* object, int64 param) const override {
		handleMitigationForceCost(creature->asCreatureObject(),
			BuffCRC::JEDI_FORCE_SHIELD_2, param, 0.3f,
			"clienteffect/pl_force_shield_hit.cef");
	}

};

#endif //FORCESHIELD2COMMAND_H_
