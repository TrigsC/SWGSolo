includeFile("default.lua")
includeFile("cityPatrol.lua")
includeFile("crackdown.lua")
includeFile("deathWatch.lua")
includeFile("enclaveSentinel.lua")
includeFile("escort.lua")
includeFile("eventControl.lua")
includeFile("pet.lua")
includeFile("static.lua")
includeFile("villageRaider.lua")
includeFile("simMiner.lua")

customMap = {
	{ "crackdown", {
		{NONE, "rootCrackdown"},
		{AWARE, "awareCrackdown"},
		{IDLE, "idleCrackdown"},
		{LOOKAT, "lookCrackdown"}
	}},

	{ "enclaveSentinel", {
		{AWARE, "awareEnclavesentinel"}
	}},

	{ "deathWatch", {
		{AWARE, "awareDeathwatch"}
	}},

	{ "villageRaider", {
		{AWARE, "awareVillageraider"}
	}},

	{ "cityPatrol", {
		{ROOT, "rootCitypatrol"},
		{IDLE, "idleCitypatrol"}
	}},

	-- No-op tree for SimPlayer miner bots (controller drives movement). NONE is
	-- the root slot that runBehaviorTree() fetches; ROOT in other entries is an
	-- undefined global that also evaluates to NONE (0).
	{ "simMiner", {
		{NONE, "rootSimMiner"},
		{IDLE, "idleSimMiner"}
	}},
}

bitmaskLookup = {
	{NONE, {
		{NONE, "rootDefault"},
		{AWARE, "awareDefault"},
		{IDLE, "idleDefault"},
		{ATTACK, "attackDefault"},
		{EQUIP, "equipDefault"},
		{TARGET, "targetDefault"},
		{MOVE, "moveDefault"},
		{LOOKAT, "lookDefault"},
		{AGGRO, "aggroDefault"},
		{SCARE, "scareDefault"},
		{KILL, "killDefault"},
		{STALK, 'stalkDefault'},
		{CRACKDOWNSCAN, "crackdownScanDefault"},
		{HEAL, "healDefault"},
		{CHATREACTION, "chatReactionDefault"},
		{NOTIFYHELP, "notifyHelpDefault"},
		{HARVEST, "harvestPet"}
	}},

	{NPC, {
		{LOOKAT, "failTest"},
		{SCARE, "failTest"}
	}},

	{PET, {
		{NONE, "rootPet"},
		{ATTACK, "attackPet"},
		{AWARE, "awarePet"},
		{EQUIP, "equipPet"},
		{IDLE, "idlePet"},
		{TARGET, "targetPet"},
		{MOVE, "movePet"},
		{HARVEST, "harvestPet"}
	}},

	{FACTION_PET, {
		{NONE, "rootPet"},
		{ATTACK, "attackPet"},
		{AWARE, "awarePet"},
		{EQUIP, "equipPet"},
		{IDLE, "idlePet"},
		{TARGET, "targetPet"},
		{MOVE, "movePet"}
	}},

	{DROID_PET, {
		{NONE, "rootPet"},
		{AWARE, "awarePet"},
		{EQUIP, "equipPet"},
		{IDLE, "idlePet"},
		{TARGET, "targetPet"},
		{MOVE, "movePet"},
		{HARVEST, "harvestPet"}
	}},

	{ESCORT, {
		{AWARE, "awareEscort"},
		{IDLE, "runEscort"},
		{MOVE, "moveEscort"},
		{TARGET, "targetDefault"}
	}},

	{FOLLOW, {
		{IDLE, "runEscort"},
		{TARGET, "targetDefault"}
	}},

	{STATIC, {
		{IDLE, "wanderStatic"}
	}},

	{STATIONARY, {
		{IDLE, "idleStatic"}
	}},

	{NOAIAGGRO, {
		{NONE, "rootStatic"}
	}},

	{SQUAD, {
		{NONE, "rootCrackdown"},
		{AWARE, "awareCrackdown"},
		{IDLE, "idleCrackdown"},
		{LOOKAT, "lookCrackdown"}
	}},

	{EVENTCONTROL, {
		{NONE, "rootCrackdown"},
		{AWARE, "awareCrackdown"},
		{IDLE, "idleEventcontrol"},
		{LOOKAT, "lookCrackdown"}
	}},

	{TEST, {
		{AWARE, "failTest"},
		{IDLE, "succeedTest"},
		{ATTACK, "failTest"},
		{EQUIP, "failTest"},
		{TARGET, "failTest"},
		{MOVE, "failTest"},
		{LOOKAT, "failTest"},
		{AGGRO, "failTest"},
		{SCARE, "failTest"}
	}}
}
