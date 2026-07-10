/*
 * CreatureTemplate.cpp
 *
 *  Created on: 22/01/2012
 *      Author: victor
 */

#include "CreatureTemplate.h"
#include "server/zone/managers/creature/CreatureTemplateManager.h"

CreatureTemplate::CreatureTemplate() {
	conversationTemplate = 0;
	kinetic = 0;
	energy = 0;
	electricity = 0;
	stun = 0;
	blast = 0;
	heat = 0;
	cold = 0;
	acid = 0;
	lightSaber = 0;

	scale = 1.f;

	objectName = "";
	randomNameType = 0;
	mobType = 0;
	randomNameTag = false;
	customName = "";
	planetMapCategory = 0;
	mapCategoryName = "";
	planetMapSubCategory = 0;
	mapSubCategoryName = "";
	socialGroup = "";
	faction = "";
	level = 0;
	chanceHit = 0.f;
	damageMin = 0;
	damageMax = 0;
	attackSpeed = 0.0f;
	specialDamageMult = 1.f;
	range = 0;
	baseXp = 0;
	baseHAM = 0;
	baseHAMmax = 0;
	armor = 0;
	meatType = "";
	meatAmount = 0;
	hideType = "";
	hideAmount = 0;
	boneType = "";
	boneAmount = 0;
	milkType = "";
	milk = 0.f;
	tamingChance = 0.f;
	ferocity = 0;
	aggroRadius = 0;
	pvpBitmask = 0;
	creatureBitmask = 0;
	diet = 0;
	optionsBitmask = 0;
	customAiMap = 0;
	tauntable = true;
	healerType = "";
	jediArchetype = "";
	frsRankMin = -1;
	frsRankMax = -1;
	frsCouncil = 0;
	lightsaberColor = 0;

	primaryWeapon = "";
	secondaryWeapon = "";
	thrownWeapon = "";

	templates.removeAll();

	primaryAttacks = new CreatureAttackMap();
	secondaryAttacks = new CreatureAttackMap();

	aiTemplate = "example";
	defaultWeapon = "";
	defaultAttack = "defaultattack";
	controlDeviceTemplate = "object/intangible/pet/pet_control.iff";
	containerComponentTemplate = "";
	reactionStf = "";
	personalityStf = "";
}

CreatureTemplate::~CreatureTemplate() {
	templates.removeAll();

	delete primaryAttacks;
	primaryAttacks = nullptr;

	delete secondaryAttacks;
	secondaryAttacks = nullptr;
}

void CreatureTemplate::readObject(LuaObject* templateData) {
	conversationTemplate = String(templateData->getStringField("conversationTemplate").trim()).hashCode();
	objectName = templateData->getStringField("objectName").trim();
	randomNameType = templateData->getIntField("randomNameType");
	randomNameTag = templateData->getBooleanField("randomNameTag");

	mapCategoryName = String(templateData->getStringField("planetMapCategory").trim());
	planetMapCategory = mapCategoryName.hashCode();

	mapSubCategoryName = String(templateData->getStringField("planetMapSubCategory").trim());
	planetMapSubCategory = mapSubCategoryName.hashCode();

	mobType = templateData->getIntField("mobType");

	customName = templateData->getStringField("customName").trim();
	socialGroup = templateData->getStringField("socialGroup").trim();
	faction = templateData->getStringField("faction").trim().toLowerCase();
	level = templateData->getIntField("level");
	chanceHit = templateData->getFloatField("chanceHit");
	damageMin = templateData->getIntField("damageMin");
	damageMax = templateData->getIntField("damageMax");
	specialDamageMult = templateData->getFloatField("specialDamageMult");
	attackSpeed = templateData->getFloatField("attackSpeed");
	if (specialDamageMult < 0.001f) specialDamageMult = 1.f; // could use numeric_limit here, but this will prevent people from putting tiny modifiers in as well.
	baseXp = templateData->getIntField("baseXp");
	baseHAM = templateData->getIntField("baseHAM");
	baseHAMmax = templateData->getIntField("baseHAMmax");
	armor = templateData->getIntField("armor");
	meatType = templateData->getStringField("meatType").trim();
	meatAmount = templateData->getIntField("meatAmount");
	hideType = templateData->getStringField("hideType").trim();
	hideAmount = templateData->getIntField("hideAmount");
	boneType = templateData->getStringField("boneType").trim();
	boneAmount = templateData->getIntField("boneAmount");
	milk = templateData->getIntField("milk");
	tamingChance = templateData->getFloatField("tamingChance");
	ferocity = templateData->getIntField("ferocity");
	aggroRadius = templateData->getIntField("aggroRadius");
	pvpBitmask = templateData->getIntField("pvpBitmask");
	creatureBitmask = templateData->getIntField("creatureBitmask");
	diet = templateData->getIntField("diet");
	optionsBitmask = templateData->getIntField("optionsBitmask");
	patrolPathTemplate = templateData->getStringField("patrolPathTemplate");
	defaultWeapon = templateData->getStringField("defaultWeapon");
	tauntable = templateData->getBooleanField("tauntable", true);
	healerType = templateData->getStringField("healerType").trim();
	jediArchetype = templateData->getStringField("jediArchetype").trim().toLowerCase();
	frsRankMin = templateData->getSignedIntField("frsRank", -1);
	frsRankMax = frsRankMin;

	int configuredFrsRankMin = templateData->getSignedIntField("frsRankMin", -1);
	int configuredFrsRankMax = templateData->getSignedIntField("frsRankMax", -1);

	if (configuredFrsRankMin >= 0 || configuredFrsRankMax >= 0) {
		frsRankMin = configuredFrsRankMin >= 0 ? configuredFrsRankMin :
			configuredFrsRankMax;
		frsRankMax = configuredFrsRankMax >= 0 ? configuredFrsRankMax :
			configuredFrsRankMin;
	}

	if (frsRankMin > frsRankMax) {
		int swapRank = frsRankMin;
		frsRankMin = frsRankMax;
		frsRankMax = swapRank;
	}

	if (frsRankMin < -1)
		frsRankMin = -1;
	if (frsRankMax < -1)
		frsRankMax = -1;
	if (frsRankMin > 11)
		frsRankMin = 11;
	if (frsRankMax > 11)
		frsRankMax = 11;

	frsCouncil = templateData->getSignedIntField("frsCouncil", 0);
	String frsCouncilName = templateData->getStringField("frsCouncil").trim().toLowerCase();

	if (frsCouncilName == "light" || frsCouncilName == "rebel")
		frsCouncil = 1;
	else if (frsCouncilName == "dark" || frsCouncilName == "imperial")
		frsCouncil = 2;

	if (frsCouncil == 0 && frsRankMin >= 0) {
		String lowerTemplateName = templateName.toLowerCase();
		String lowerObjectName = objectName.toLowerCase();

		if (lowerTemplateName.contains("light_jedi") ||
				lowerObjectName.contains("light_jedi") ||
				lowerObjectName.contains("light jedi"))
			frsCouncil = 1;
		else if (lowerTemplateName.contains("dark_jedi") ||
				lowerObjectName.contains("dark_jedi") ||
				lowerObjectName.contains("dark jedi"))
			frsCouncil = 2;
	}
	lightsaberColor = templateData->getIntField("lightsaberColor");

	if(!templateData->getStringField("defaultAttack").isEmpty())
		defaultAttack = templateData->getStringField("defaultAttack");

	if(!templateData->getStringField("customAiMap").isEmpty())
		customAiMap = templateData->getStringField("customAiMap").hashCode();

	scale = templateData->getFloatField("scale");

	if (!templateData->getStringField("milkType").isEmpty()) {
		milkType = templateData->getStringField("milkType").trim();
	}

	LuaObject statsTable = templateData->getObjectField("statistics");
    if (statsTable.isValidTable()) {
        lua_State* L = statsTable.getLuaState();

        // Iterate over the Lua table
        for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
            String key = "";
            int value = 0;
            bool valid = false;

            // 1. Check Key (Must be String)
            if (lua_type(L, -2) == LUA_TSTRING) {
                key = lua_tostring(L, -2);

                // 2. Check Value (Accept Number OR String)
                if (lua_type(L, -1) == LUA_TNUMBER) {
                    value = (int)lua_tonumber(L, -1);
                    valid = true;
                } else if (lua_type(L, -1) == LUA_TSTRING) {
                    String valStr = lua_tostring(L, -1);
                    value = Integer::valueOf(valStr);
                    valid = true;
                }
            }

            if (valid) {
                statistics.put(key, value);

                // --- DEBUG: PROVE THAT C++ SEES THE STATS ---
                // We use Logger::console because 'attacker' does not exist here.
                // if (key.contains("speed")) {
                //     StringBuffer msg;
                //     msg << "[CreatureTemplate] STAT-LOAD: Injected " << key << " = " << value;
                //     Logger::console.info(msg.toString(), true);
                // }
            } else {
                 // Warn if we are skipping data
                 if (lua_type(L, -2) == LUA_TSTRING) {
                     String skey = lua_tostring(L, -2); // Renamed to avoid shadow warning
                     StringBuffer msg;
                     msg << "[CreatureTemplate] STAT-SKIP: Skipped " << skey << " (Invalid Type)";
                     Logger::console.info(msg.toString(), true);
                 }
            }
        }
    }
    statsTable.pop();

	LuaObject res = templateData->getObjectField("resists");
	if (res.getTableSize() == 9) {
		kinetic = res.getFloatAt(1);
		energy = res.getFloatAt(2);
		blast = res.getFloatAt(3);
		heat = res.getFloatAt(4);
		cold = res.getFloatAt(5);
		electricity = res.getFloatAt(6);
		acid = res.getFloatAt(7);
		stun = res.getFloatAt(8);
		lightSaber = res.getFloatAt(9);
	}

	res.pop();

	LuaObject temps = templateData->getObjectField("templates");
	if (temps.isValidTable()) {
		for (int i = 1; i <= temps.getTableSize(); ++i) {
			String tempName = temps.getStringAt(i).trim();

			if (tempName.endsWith(".iff")) {
				templates.add(tempName);
				continue;
			}

			const Vector<String>& dressGroup = CreatureTemplateManager::instance()->getDressGroup(tempName);
			templates.addAll(dressGroup);
		}
	}

	temps.pop();

	LuaObject lootCollections = templateData->getObjectField("lootGroups");
	lootgroups.readObject(&lootCollections, level);
	lootCollections.pop();

	primaryWeapon = templateData->getStringField("primaryWeapon");
	secondaryWeapon = templateData->getStringField("secondaryWeapon");
	thrownWeapon = templateData->getStringField("thrownWeapon");

	LuaObject attackList = templateData->getObjectField("primaryAttacks");
	if (attackList.isValidTable()) {
		int size = attackList.getTableSize();
		lua_State* L = attackList.getLuaState();
		for (int i = 1; i <= size; ++i) {
			lua_rawgeti(L, -1, i);
			LuaObject atk(L);

			if (atk.isValidTable()) {
				int atkSize = atk.getTableSize();
				if (atkSize == 2) {
					String com = atk.getStringAt(1).trim();
					String arg = atk.getStringAt(2).trim();

					primaryAttacks->addAttack(com, arg);
				}
			}

			atk.pop();
		}
	}

	attackList.pop();

	attackList = templateData->getObjectField("secondaryAttacks");
	if (attackList.isValidTable()) {
		int size = attackList.getTableSize();
		lua_State* L = attackList.getLuaState();
		for (int i = 1; i <= size; ++i) {
			lua_rawgeti(L, -1, i);
			LuaObject atk(L);

			if (atk.isValidTable()) {
				int atkSize = atk.getTableSize();
				if (atkSize == 2) {
					String com = atk.getStringAt(1).trim();
					String arg = atk.getStringAt(2).trim();

					secondaryAttacks->addAttack(com, arg);
				}
			}

			atk.pop();
		}
	}

	attackList.pop();

	LuaObject hueTable = templateData->getObjectField("hues");
	if (hueTable.isValidTable()) {
		for (int i = 1; i <= hueTable.getTableSize(); ++i) {
			hues.add(hueTable.getIntAt(i));
		}
	}

	hueTable.pop();

	outfit = templateData->getStringField("outfit");

	aiTemplate = templateData->getStringField("aiTemplate");

	if(!templateData->getStringField("controlDeviceTemplate").isEmpty())
		controlDeviceTemplate = templateData->getStringField("controlDeviceTemplate");

	containerComponentTemplate = templateData->getStringField("containerComponentTemplate");

	reactionStf = templateData->getStringField("reactionStf");
	personalityStf = templateData->getStringField("personalityStf");
}
