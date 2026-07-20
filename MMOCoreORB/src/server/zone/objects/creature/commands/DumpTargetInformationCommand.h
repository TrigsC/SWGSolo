/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef DUMPTARGETINFORMATIONCOMMAND_H_
#define DUMPTARGETINFORMATIONCOMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "templates/params/creature/ObjectFlag.h"
#include <fstream>
#include <cstdio>

class DumpTargetInformationCommand : public QueueCommand {
public:

	DumpTargetInformationCommand(const String& name, ZoneProcessServer* server)
		: QueueCommand(name, server) {

	}

	// --- diagnostic helpers (added: attackability forensics) ---
	static String yn(bool b) {
		return b ? String("yes") : String("no");
	}

	static String hex32(uint32 v) {
		char buf[16];
		snprintf(buf, sizeof(buf), "0x%08x", v);
		return String(buf);
	}

	// pvpStatusBitmask flag group
	static String decodePvpBits(uint32 b) {
		if (b == 0)
			return String("<none>");

		StringBuffer s;
		if (b & ObjectFlag::ATTACKABLE) s << "ATTACKABLE ";
		if (b & ObjectFlag::AGGRESSIVE) s << "AGGRESSIVE ";
		if (b & ObjectFlag::OVERT) s << "OVERT ";
		if (b & ObjectFlag::TEF) s << "TEF ";
		if (b & ObjectFlag::PLAYER) s << "PLAYER ";
		if (b & ObjectFlag::ENEMY) s << "ENEMY ";
		if (b & ObjectFlag::WILLBEDECLARED) s << "WILLBEDECLARED ";
		if (b & ObjectFlag::WASDECLARED) s << "WASDECLARED ";
		return s.toString();
	}

	// creatureBitmask flag group (shares numeric values with the pvp group but
	// is a different field, so decode it against its own names)
	static String decodeCreatureBits(uint32 b) {
		if (b == 0)
			return String("<none>");

		StringBuffer s;
		if (b & ObjectFlag::NPC) s << "NPC ";
		if (b & ObjectFlag::PACK) s << "PACK ";
		if (b & ObjectFlag::HERD) s << "HERD ";
		if (b & ObjectFlag::KILLER) s << "KILLER ";
		if (b & ObjectFlag::STALKER) s << "STALKER ";
		if (b & ObjectFlag::BABY) s << "BABY ";
		if (b & ObjectFlag::LAIR) s << "LAIR ";
		if (b & ObjectFlag::HEALER) s << "HEALER ";
		if (b & ObjectFlag::SCOUT) s << "SCOUT ";
		if (b & ObjectFlag::PET) s << "PET ";
		if (b & ObjectFlag::DROID_PET) s << "DROID_PET ";
		if (b & ObjectFlag::FACTION_PET) s << "FACTION_PET ";
		if (b & ObjectFlag::ESCORT) s << "ESCORT ";
		if (b & ObjectFlag::FOLLOW) s << "FOLLOW ";
		if (b & ObjectFlag::STATIC) s << "STATIC ";
		if (b & ObjectFlag::STATIONARY) s << "STATIONARY ";
		if (b & ObjectFlag::NOAIAGGRO) s << "NOAIAGGRO ";
		if (b & ObjectFlag::SCANNING_FOR_CONTRABAND) s << "SCANNING_FOR_CONTRABAND ";
		if (b & ObjectFlag::IGNORE_FACTION_STANDING) s << "IGNORE_FACTION_STANDING ";
		if (b & ObjectFlag::SQUAD) s << "SQUAD ";
		if (b & ObjectFlag::EVENTCONTROL) s << "EVENTCONTROL ";
		if (b & ObjectFlag::NOINTIMIDATE) s << "NOINTIMIDATE ";
		if (b & ObjectFlag::NODOT) s << "NODOT ";
		if (b & ObjectFlag::TEST) s << "TEST ";
		return s.toString();
	}

	static String movementStateName(uint32 st) {
		switch (st) {
			case AiAgent::OBLIVIOUS: return String("OBLIVIOUS(0)");
			case AiAgent::WATCHING: return String("WATCHING(1)");
			case AiAgent::STALKING: return String("STALKING(2)");
			case AiAgent::FOLLOWING: return String("FOLLOWING(3)");
			case AiAgent::PATROLLING: return String("PATROLLING(4)");
			case AiAgent::FLEEING: return String("FLEEING(5)");
			case AiAgent::LEASHING: return String("LEASHING(6)");
			case AiAgent::EVADING: return String("EVADING(7)");
			default: return String("state=") + String::valueOf((int) st);
		}
	}

	int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const {

		if (!checkStateMask(creature))
			return INVALIDSTATE;

		if (!checkInvalidLocomotions(creature))
			return INVALIDLOCOMOTION;

		if (!creature->isPlayerCreature())
			return GENERALERROR;

		CreatureObject* player = cast<CreatureObject*>(creature);

		//Apparently this command doesn't actually pass the targetid, so that probably means that it only accepts a player name
		//TODO: Reimplement this command as @getPlayerInfo
		uint64 targetID = player->getTargetID();

		ManagedReference<SceneObject*> obj = server->getZoneServer()->getObject(targetID);

		if (obj == nullptr)
			return INVALIDTARGET;

		ManagedReference<CellObject*> cell = obj->getParent().get().castTo<CellObject*>();

		int cellid = 0;
		uint32 buildingTemplate = 0;
		uint64 rootParentID = 0;

		if (cell != nullptr) {
			cellid = cell->getCellNumber();

			ManagedReference<SceneObject*> building = cell->getParent().get();
			buildingTemplate = building->getServerObjectCRC();
			rootParentID = building->getObjectID();
		}

		StringBuffer msg;

		float posX = obj->getPositionX(), posZ = obj->getPositionZ(), posY = obj->getPositionY();
		const Quaternion* direction = obj->getDirection();

		msg << "x = " << posX << ", z = " << posZ << ", y = " << posY << ", ow = " << direction->getW()
				<< ", ox = " << direction->getX() << ", oz = " << direction->getZ() << ", oy = " << direction->getY()
				<< ", cellid = " << cellid << endl;

		msg << "Root Parent:" << endl << "ID: " << rootParentID << endl;

		if (buildingTemplate != 0)
			msg << "Template: " << TemplateManager::instance()->getTemplateFile(buildingTemplate);

		if (obj->isAiAgent()) {
			AiAgent* objCreo = obj.castTo<AiAgent*>();

			PatrolPoint* home = objCreo->getHomeLocation();
			if (home != nullptr) {
				cell = home->getCell();

				if (cell != nullptr) {
					cellid = cell->getCellNumber();
					ManagedReference<SceneObject*> building = cell->getParent().get();
					buildingTemplate = building->getServerObjectCRC();
				}

				msg << endl << "homeX = " << home->getPositionX() << ", homeZ = " << home->getPositionZ() << ", homeY = " << home->getPositionY()
						<< ", homeCell = " << cellid;

				if (buildingTemplate != 0)
					msg << endl << TemplateManager::instance()->getTemplateFile(buildingTemplate);
			}

			if (objCreo->getPatrolPointSize() > 0) {
				PatrolPoint nextPosition = objCreo->getNextPosition();
				cell = nextPosition.getCell();

				if (cell != nullptr) {
					cellid = cell->getCellNumber();
					ManagedReference<SceneObject*> building = cell->getParent().get();
					buildingTemplate = building->getServerObjectCRC();
				}

				msg << endl << "nextX = " << nextPosition.getPositionX() << ", nextZ = " << nextPosition.getPositionZ() << ", nextY = " << nextPosition.getPositionY()
						<< ", nextCell = " << cellid;

				if (buildingTemplate != 0)
					msg << endl << TemplateManager::instance()->getTemplateFile(buildingTemplate);
			}

			msg << endl << "numberOfPlayersInRange = " << objCreo->getNumberOfPlayersInRange() << endl;

			WeaponObject* defaultWeapon = objCreo->getDefaultWeapon();
			if (defaultWeapon != nullptr) {
				msg << "Default Weapon ID = " << defaultWeapon->getObjectID() << endl;
			} else {
				msg << "Default Weapon is nullptr" << endl;
			}

			WeaponObject* primaryWeapon = objCreo->getPrimaryWeapon();
			if (primaryWeapon != nullptr) {
				msg << "Primary Weapon ID = " << primaryWeapon->getObjectID() << endl;
			} else {
				msg << "Primary Weapon is nullptr" << endl;
			}

			WeaponObject* secondaryWeapon = objCreo->getSecondaryWeapon();
			if (secondaryWeapon != nullptr) {
				msg << "Secondary Weapon ID = " << secondaryWeapon->getObjectID() << endl;
			} else {
				msg << "Secondary Weapon is nullptr" << endl;
			}

			WeaponObject* thrownWeapon = objCreo->getThrownWeapon();
			if (thrownWeapon != nullptr) {
				msg << "Thrown Weapon ID = " << thrownWeapon->getObjectID() << " with use count of = " << thrownWeapon->getUseCount() << endl;
			} else {
				msg << "Thrown Weapon is nullptr" << endl;
			}
		}

		// ===== ATTACKABILITY DIAGNOSTIC (added) =====
		// Explains why a target reads white / cannot be attacked. Dumps every
		// input AiAgentImplementation::isAttackableBy consults, then the
		// authoritative verdict for YOU (the player running the command).
		msg << endl << "===== ATTACKABILITY DIAGNOSTIC =====" << endl;
		msg << "OID: " << obj->getObjectID() << endl;
		msg << "Name: " << obj->getDisplayedName() << endl;
		msg << "ObjTemplate: " << TemplateManager::instance()->getTemplateFile(obj->getServerObjectCRC()) << endl;
		msg << "isCreatureObject=" << yn(obj->isCreatureObject())
				<< " isAiAgent=" << yn(obj->isAiAgent()) << endl;

		CreatureObject* creoTarget = obj->asCreatureObject();

		if (creoTarget != nullptr) {
			uint32 pvpBits = creoTarget->getPvpStatusBitmask();

			msg << "pvpStatusBitmask: " << hex32(pvpBits) << " [" << decodePvpBits(pvpBits) << "]" << endl;
			msg << "  ATTACKABLE flag: " << ((pvpBits & ObjectFlag::ATTACKABLE) ? String("SET") : String("MISSING -> NOT attackable")) << endl;
			msg << "optionsBitmask: " << hex32(creoTarget->getOptionsBitmask()) << endl;
			msg << "faction: " << hex32(creoTarget->getFaction())
					<< (creoTarget->getFaction() == 0 ? String(" (neutral/0)") : String("")) << endl;
			msg << "factionStatus: " << creoTarget->getFactionStatus() << " (0=ONLEAVE,1=COVERT,2=OVERT)" << endl;
			msg << "isPlayerCreature=" << yn(creoTarget->isPlayerCreature())
					<< " isInvisible=" << yn(creoTarget->isInvisible())
					<< " isInCombat=" << yn(creoTarget->isInCombat()) << endl;
			msg << "isDead=" << yn(creoTarget->isDead())
					<< " isIncapacitated=" << yn(creoTarget->isIncapacitated())
					<< " posture=" << creoTarget->getPosture() << endl;
			msg << "Health HAM: " << creoTarget->getHAM(0) << " / " << creoTarget->getMaxHAM(0) << endl;
			msg << "species=" << creoTarget->getSpecies()
					<< " mainDefender=" << yn(creoTarget->getMainDefender() != nullptr) << endl;

			AiAgent* ai = obj->asAiAgent();

			if (ai != nullptr) {
				uint32 cb = ai->getCreatureBitmask();

				msg << "-- AiAgent --" << endl;
				msg << "getSimPlayerBot=" << yn(ai->getSimPlayerBot()) << endl;
				msg << "movementState: " << movementStateName(ai->getMovementState()) << endl;
				msg << "creatureBitmask: " << hex32(cb) << " [" << decodeCreatureBits(cb) << "]" << endl;
				msg << "isBaby(bitmask)=" << yn((cb & ObjectFlag::BABY) != 0) << endl;
				msg << "aiNumberOfPlayersInRange=" << ai->getNumberOfPlayersInRange() << endl;
			}

			// Authoritative verdict for the player running the command.
			bool attackable = creoTarget->isAttackableBy(player);

			msg << "==> isAttackableBy(YOU): " << (attackable ? String("TRUE (attackable)") : String("FALSE (NOT attackable)")) << endl;

			if (!attackable) {
				String reason;

				if (ai != nullptr && ai->getMovementState() == AiAgent::LEASHING)
					reason = "LEASHING (returning home) - transient; re-check when it stops";
				else if (creoTarget->isDead())
					reason = "target is DEAD";
				else if (creoTarget->isIncapacitated())
					reason = "target is INCAPACITATED";
				else if (!(pvpBits & ObjectFlag::ATTACKABLE))
					reason = "pvpStatusBitmask missing ATTACKABLE";
				else if (ai != nullptr && ai->getSimPlayerBot() && creoTarget->getFaction() == 0)
					reason = "neutral sim player-bot (by design: players cannot attack neutral bots)";
				else
					reason = "faction / no-combat-area / event-area / AI-vs-AI rule (see faction + flags above)";

				msg << "==> LIKELY REASON: " << reason << endl;
			}
		} else {
			msg << "(target is not a CreatureObject - attackability verdict skipped)" << endl;
		}

		String body = msg.toString();

		player->sendSystemMessage(body);

		// Persist to a readable log file (server cwd = MMOCoreORB/bin, so this
		// lands next to core3.log at bin/log/dumptarget.log). Appends; each
		// dump is fronted by a timestamp/target header for easy grepping.
		std::ofstream dumpFile("log/dumptarget.log", std::ios::app);
		if (dumpFile.is_open()) {
			dumpFile << "======== dumpTargetInformation  t=" << System::getMiliTime() << "ms"
					<< "  by=" << player->getFirstName().toCharArray()
					<< "  targetOID=" << obj->getObjectID()
					<< "  targetName=" << obj->getDisplayedName().toCharArray()
					<< " ========\n";
			dumpFile << body.toCharArray() << "\n\n";
			dumpFile.close();
		}

		return SUCCESS;
	}

};

#endif //DUMPTARGETINFORMATIONCOMMAND_H_
