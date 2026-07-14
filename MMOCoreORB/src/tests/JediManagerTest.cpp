/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#include "server/zone/managers/jedi/JediManager.h"
#include "server/zone/objects/creature/ai/JediFrsNpcData.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "engine/lua/MockLua.h"
#include "engine/lua/MockLuaFunction.h"
#include "server/zone/managers/director/MockDirectorManager.h"

using ::testing::_;
using ::testing::Return;
using ::testing::AnyNumber;
using ::testing::TypedEq;
using ::testing::An;

namespace server {
namespace zone {
namespace managers {
namespace jedi {
namespace tests {

class JediManagerTest : public ::testing::Test, public Logger {
public:
	JediManager* jediManager;

	JediManagerTest() : Logger("JediManagerTest"), jediManager(nullptr) {
		// Perform creation setup here.
	}

	~JediManagerTest() {
		// Clean up.
	}

	void SetUp() {
		// Perform setup of common constructs here.
		jediManager = new JediManager();
		jediManager->setJediManagerName("JediManager");
	}

	void TearDown() {
		// Perform clean up of common constructs here.
		delete jediManager;
	}

	void constructorDefaults(MockLua& mockLua) {
		EXPECT_CALL(mockLua, runFile(_)).Times(AnyNumber());
		EXPECT_CALL(mockLua, getGlobalInt(_)).Times(AnyNumber());
		EXPECT_CALL(mockLua, getGlobalString(_)).Times(AnyNumber());
		EXPECT_CALL(mockLua, setGlobalInt(_, _)).Times(AnyNumber());
		ON_CALL(mockLua, getGlobalString(_)).WillByDefault(Return(String("")));
	}
};

TEST_F(JediManagerTest, ShouldRunFileJediManagerLuaAtLoadConfiguration) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	EXPECT_CALL(mockLua, runFile(String("scripts/managers/jedi/jedi_manager.lua"))).Times(1);

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, ShouldReadTheJediProgressionTypeVariableAtLoadConfigurations) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	EXPECT_CALL(mockLua, getGlobalInt(String("jediProgressionType"))).WillOnce(Return((int)JediManager::NOJEDIPROGRESSION /* gcc-4.4.5 cast-hack */));

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, ShouldRunTheHolocronJediManagerLuaFileIfHolocronJediProgressionIsConfigured) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	ON_CALL(mockLua, getGlobalInt(String("jediProgressionType"))).WillByDefault(Return((int)JediManager::HOLOGRINDJEDIPROGRESSION /* gcc-4.4.5 cast-hack */));
	EXPECT_CALL(mockLua, runFile(String("scripts/managers/jedi/hologrind_jedi_manager.lua"))).Times(1);

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, ShouldRunTheVillageJediManagerLuaFileIfVillageJediProgressionIsConfigured) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	ON_CALL(mockLua, getGlobalInt(String("jediProgressionType"))).WillByDefault(Return((int)JediManager::VILLAGEJEDIPROGRESSION /* gcc-4.4.5 cast-hack */));
	EXPECT_CALL(mockLua, runFile(String("scripts/managers/jedi/village_jedi_manager.lua"))).Times(1);

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, ShouldReadTheCustomJediProgressionFileStringIfCustomJediProgressionIsConfigured) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	ON_CALL(mockLua, getGlobalInt(String("jediProgressionType"))).WillByDefault(Return((int)JediManager::CUSTOMJEDIPROGRESSION /* gcc-4.4.5 cast-hack */));
	EXPECT_CALL(mockLua, getGlobalString(String("customJediProgressionFile"))).WillOnce(Return(String("scripts/managers/jedi/custom_jedi_manager.lua")));

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, ShouldLoadTheCustomJediProgressionFileIfCustomJediProgressionIsConfigured) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	ON_CALL(mockLua, getGlobalInt(String("jediProgressionType"))).WillByDefault(Return((int)JediManager::CUSTOMJEDIPROGRESSION /* gcc-4.4.5 cast-hack */));
	EXPECT_CALL(mockLua, getGlobalString(String("customJediProgressionFile"))).WillOnce(Return(String("scripts/managers/jedi/custom_jedi_manager.lua")));
	EXPECT_CALL(mockLua, runFile(String("scripts/managers/jedi/custom_jedi_manager.lua"))).Times(1);

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, ShouldReadTheJediProgressionSystemNameAtLoadConfiguration) {
	MockLua mockLua;

	constructorDefaults(mockLua);

	ON_CALL(mockLua, getGlobalInt(String("jediProgressionType"))).WillByDefault(Return((int)JediManager::HOLOGRINDJEDIPROGRESSION /* gcc-4.4.5 cast-hack */));
	EXPECT_CALL(mockLua, getGlobalString(String("jediManagerName"))).WillOnce(Return(String("HologrindJediManager")));

	jediManager->loadConfiguration(&mockLua);
}

TEST_F(JediManagerTest, OnPlayerCreatedShouldCallTheOnPlayerCreatedMethodInTheLuaJediManager) {
	MockLua mockLua;
	Reference<MockLuaFunction*> mockLuaFunction = new MockLuaFunction();
	Reference<MockDirectorManager*> mockDirectorManager = new MockDirectorManager();
	Reference<MockCreatureObject*> mockCreatureObject = new MockCreatureObject();

	EXPECT_CALL(*mockDirectorManager, getLuaInstance()).WillOnce(Return(&mockLua));
	EXPECT_CALL(mockLua, createFunction(String("JediManager"), String("onPlayerCreated"), 0)).WillOnce(Return(mockLuaFunction));
	EXPECT_CALL(*mockLuaFunction, addArgument(TypedEq<void*>(mockCreatureObject))).Times(1);
	EXPECT_CALL(*mockLuaFunction, callFunction()).Times(1);

	Reference<DirectorManager*> realDirectorManager = DirectorManager::instance();

	DirectorManager::setSingletonInstance(mockDirectorManager);

	jediManager->onPlayerCreated(mockCreatureObject);

	DirectorManager::setSingletonInstance(realDirectorManager);
}

TEST_F(JediManagerTest, OnPlayerLoggedInShouldCallTheOnPlayerLoggedInMethodInTheLuaJediManager) {
	MockLua mockLua;
	Reference<MockLuaFunction*> mockLuaFunction = new MockLuaFunction();
	Reference<MockDirectorManager*> mockDirectorManager = new MockDirectorManager();
	Reference<MockCreatureObject*> mockCreatureObject = new MockCreatureObject();

	EXPECT_CALL(*mockDirectorManager, getLuaInstance()).WillOnce(Return(&mockLua));
	EXPECT_CALL(mockLua, createFunction(String("JediManager"), String("onPlayerLoggedIn"), 0)).WillOnce(Return(mockLuaFunction));
	EXPECT_CALL(*mockLuaFunction, addArgument(TypedEq<void*>(mockCreatureObject))).Times(1);
	EXPECT_CALL(*mockLuaFunction, callFunction()).Times(1);

	Reference<DirectorManager*> realDirectorManager = DirectorManager::instance();

	DirectorManager::setSingletonInstance(mockDirectorManager);

	jediManager->onPlayerLoggedIn(mockCreatureObject);

	DirectorManager::setSingletonInstance(realDirectorManager);
}

TEST_F(JediManagerTest, OnPlayerLoggedOutShouldCallTheOnPlayerLoggedOutMethodInTheLuaJediManager) {
	MockLua mockLua;
	Reference<MockLuaFunction*> mockLuaFunction = new MockLuaFunction();
	Reference<MockDirectorManager*> mockDirectorManager = new MockDirectorManager();
	Reference<MockCreatureObject*> mockCreatureObject = new MockCreatureObject();

	EXPECT_CALL(*mockDirectorManager, getLuaInstance()).WillOnce(Return(&mockLua));
	EXPECT_CALL(mockLua, createFunction(String("JediManager"), String("onPlayerLoggedOut"), 0)).WillOnce(Return(mockLuaFunction));
	EXPECT_CALL(*mockLuaFunction, addArgument(TypedEq<void*>(mockCreatureObject))).Times(1);
	EXPECT_CALL(*mockLuaFunction, callFunction()).Times(1);

	Reference<DirectorManager*> realDirectorManager = DirectorManager::instance();

	DirectorManager::setSingletonInstance(mockDirectorManager);

	jediManager->onPlayerLoggedOut(mockCreatureObject);

	DirectorManager::setSingletonInstance(realDirectorManager);
}

TEST_F(JediManagerTest, CheckForceStatusCommandShouldCallTheCheckForceStatusCommandMethodInTheLuaJediManager) {
	MockLua mockLua;
	Reference<MockLuaFunction*> mockLuaFunction = new MockLuaFunction();
	Reference<MockDirectorManager*> mockDirectorManager = new MockDirectorManager();
	Reference<MockCreatureObject*> mockCreatureObject = new MockCreatureObject();

	EXPECT_CALL(*mockDirectorManager, getLuaInstance()).WillOnce(Return(&mockLua));
	EXPECT_CALL(mockLua, createFunction(String("JediManager"), String("checkForceStatusCommand"), 0)).WillOnce(Return(mockLuaFunction));
	EXPECT_CALL(*mockLuaFunction, addArgument(TypedEq<void*>(mockCreatureObject))).Times(1);
	EXPECT_CALL(*mockLuaFunction, callFunction()).Times(1);

	Reference<DirectorManager*> realDirectorManager = DirectorManager::instance();

	DirectorManager::setSingletonInstance(mockDirectorManager);

	jediManager->checkForceStatusCommand(mockCreatureObject);

	DirectorManager::setSingletonInstance(realDirectorManager);
}

TEST_F(JediManagerTest, UseItemShouldCallTheUseItemMethodInTheLuaJediManager) {
	MockLua mockLua;
	Reference<MockLuaFunction*> mockLuaFunction = new MockLuaFunction();
	Reference<MockDirectorManager*> mockDirectorManager = new MockDirectorManager();
	Reference<MockCreatureObject*> mockCreatureObject = new MockCreatureObject();
	Reference<MockSceneObject*> mockSceneObject = new MockSceneObject();

	EXPECT_CALL(*mockDirectorManager, getLuaInstance()).WillOnce(Return(&mockLua));
	EXPECT_CALL(mockLua, createFunction(String("JediManager"), String("useItem"), 0)).WillOnce(Return(mockLuaFunction));
	EXPECT_CALL(*mockLuaFunction, addArgument(An<void*>())).Times(2);
	EXPECT_CALL(*mockLuaFunction, addArgument(An<int>())).Times(1);
	EXPECT_CALL(*mockLuaFunction, callFunction()).Times(1);

	Reference<DirectorManager*> realDirectorManager = DirectorManager::instance();

	DirectorManager::setSingletonInstance(mockDirectorManager);

	jediManager->useItem(mockSceneObject, JediManager::ITEMHOLOCRON, mockCreatureObject);

	DirectorManager::setSingletonInstance(realDirectorManager);
}

TEST(JediNpcForceAccounting, RankSkillModsMatchAuthoritativeTables) {
	const int expectedLightControl[JediFrsNpcData::RANK_COUNT] =
		{ 5, 10, 15, 20, 25, 35, 45, 55, 70, 85, 100, 120 };
	const int expectedDarkControl[JediFrsNpcData::RANK_COUNT] =
		{ 4, 6, 8, 10, 12, 15, 20, 25, 35, 45, 60, 75 };
	const int expectedLightPower[JediFrsNpcData::RANK_COUNT] =
		{ 4, 6, 8, 10, 12, 15, 20, 25, 35, 45, 60, 75 };
	const int expectedDarkPower[JediFrsNpcData::RANK_COUNT] =
		{ 5, 10, 15, 20, 25, 35, 45, 55, 70, 85, 100, 120 };
	const int expectedManipulation[JediFrsNpcData::RANK_COUNT] =
		{ 5, 8, 12, 16, 20, 25, 30, 35, 45, 55, 65, 80 };
	const int expectedMaxForce[JediFrsNpcData::RANK_COUNT] =
		{ 90, 160, 230, 300, 370, 500, 650, 800, 1050, 1300, 1600, 1950 };
	const int expectedRegen[JediFrsNpcData::RANK_COUNT] =
		{ 1, 2, 3, 4, 5, 6, 8, 9, 12, 14, 17, 20 };

	for (int rank = 0; rank < JediFrsNpcData::RANK_COUNT; ++rank) {
		EXPECT_EQ(JediFrsNpcData::getControl(true, rank), expectedLightControl[rank]);
		EXPECT_EQ(JediFrsNpcData::getControl(false, rank), expectedDarkControl[rank]);
		EXPECT_EQ(JediFrsNpcData::getPower(true, rank), expectedLightPower[rank]);
		EXPECT_EQ(JediFrsNpcData::getPower(false, rank), expectedDarkPower[rank]);
		EXPECT_EQ(JediFrsNpcData::getManipulation(rank), expectedManipulation[rank]);
		EXPECT_EQ(JediFrsNpcData::getMaxForceBonus(true, rank), expectedMaxForce[rank]);
		EXPECT_EQ(JediFrsNpcData::getMaxForceBonus(false, rank), expectedMaxForce[rank]);
		EXPECT_EQ(JediFrsNpcData::getRegenBonus(rank), expectedRegen[rank]);
	}
}

TEST(JediNpcForceAccounting, DefensiveValuesMatchBothCouncilTables) {
	const int expectedLightArmor2[JediFrsNpcData::RANK_COUNT] =
		{ 47, 49, 50, 52, 54, 57, 61, 64, 70, 75, 80, 87 };
	const int expectedDarkArmor2[JediFrsNpcData::RANK_COUNT] =
		{ 46, 47, 48, 49, 49, 50, 52, 54, 57, 61, 66, 71 };
	const int expectedLightFeedback2[JediFrsNpcData::RANK_COUNT] =
		{ 97, 100, 102, 104, 106, 111, 115, 120, 127, 133, 140, 149 };
	const int expectedDarkFeedback2[JediFrsNpcData::RANK_COUNT] =
		{ 97, 98, 99, 100, 100, 102, 104, 106, 111, 115, 122, 129 };
	const int expectedLightResist[JediFrsNpcData::RANK_COUNT] =
		{ 27, 29, 30, 32, 34, 37, 41, 44, 50, 55, 60, 67 };
	const int expectedDarkResist[JediFrsNpcData::RANK_COUNT] =
		{ 26, 27, 28, 29, 29, 30, 32, 34, 37, 41, 46, 51 };
	const int expectedArmor2CostTenths[JediFrsNpcData::RANK_COUNT] =
		{ 285, 276, 264, 252, 240, 225, 210, 195, 165, 135, 105, 60 };

	for (int rank = 0; rank < JediFrsNpcData::RANK_COUNT; ++rank) {
		int lightControl = JediFrsNpcData::getControl(true, rank);
		int darkControl = JediFrsNpcData::getControl(false, rank);
		int manipulation = JediFrsNpcData::getManipulation(rank);

		EXPECT_EQ(45 + JediFrsNpcData::getRoundedControlBonus(lightControl, 35),
			expectedLightArmor2[rank]);
		EXPECT_EQ(45 + JediFrsNpcData::getRoundedControlBonus(darkControl, 35),
			expectedDarkArmor2[rank]);
		EXPECT_EQ(95 + JediFrsNpcData::getRoundedControlBonus(lightControl, 45),
			expectedLightFeedback2[rank]);
		EXPECT_EQ(95 + JediFrsNpcData::getRoundedControlBonus(darkControl, 45),
			expectedDarkFeedback2[rank]);
		EXPECT_EQ(25 + JediFrsNpcData::getRoundedControlBonus(lightControl, 35),
			expectedLightResist[rank]);
		EXPECT_EQ(25 + JediFrsNpcData::getRoundedControlBonus(darkControl, 35),
			expectedDarkResist[rank]);
		EXPECT_EQ(300 - (manipulation * 3), expectedArmor2CostTenths[rank]);
	}
}

TEST(JediNpcForceAccounting, OffensivePowerValuesMatchBothCouncilTables) {
	const int expectedLightLightning2Min[JediFrsNpcData::RANK_COUNT] =
		{ 620, 630, 640, 650, 660, 675, 700, 725, 775, 825, 900, 975 };
	const int expectedDarkLightning2Min[JediFrsNpcData::RANK_COUNT] =
		{ 625, 650, 675, 700, 725, 775, 825, 875, 950, 1025, 1100, 1200 };

	for (int rank = 0; rank < JediFrsNpcData::RANK_COUNT; ++rank) {
		EXPECT_EQ(600 + (JediFrsNpcData::getPower(true, rank) * 5),
			expectedLightLightning2Min[rank]);
		EXPECT_EQ(600 + (JediFrsNpcData::getPower(false, rank) * 5),
			expectedDarkLightning2Min[rank]);
	}
}

TEST(JediNpcForceAccounting, EnduranceTuningUsesExactAttackAffordability) {
	EXPECT_FLOAT_EQ(JediFrsNpcData::getNpcSaberForceCost(), 10.f);
	EXPECT_EQ(JediFrsNpcData::FORCE_THREAT_MEMORY_MS, 20000);
	EXPECT_EQ(JediFrsNpcData::FORCE_FEEDBACK_RECAST_MS, 120000);
	EXPECT_EQ(JediFrsNpcData::FORCE_ABSORB_RECAST_MS, 180000);

	EXPECT_TRUE(JediFrsNpcData::canAffordAttackCost(0, 0.f, true));
	EXPECT_FALSE(JediFrsNpcData::canAffordAttackCost(19, 20.f, false));
	EXPECT_TRUE(JediFrsNpcData::canAffordAttackCost(20, 20.f, false));

	// CombatManager applies the player saber rule: current Force must be
	// strictly greater than the floating-point saber cost, while a Force
	// command may equal its cost.
	EXPECT_FALSE(JediFrsNpcData::canAffordAttackCost(20, 20.f, true));
	EXPECT_TRUE(JediFrsNpcData::canAffordAttackCost(21, 20.f, true));
	EXPECT_TRUE(JediFrsNpcData::canAffordAttackCost(18, 17.5f, true));
	EXPECT_TRUE(JediFrsNpcData::canAffordAttackCost(1236, 30.f, true));
	EXPECT_FALSE(JediFrsNpcData::canAffordAttackCost(15, 18.f, true));
}


}
}
}
}
}
