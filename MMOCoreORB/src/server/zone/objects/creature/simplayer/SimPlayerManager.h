/*
 * SimPlayerManager.h
 * Manager for handling SimPlayer population and lifecycle.
 */

#ifndef SIMPLAYERMANAGER_H_
#define SIMPLAYERMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/util/SynchronizedVectorMap.h"
#include "system/util/Vector.h"
#include "system/util/VectorMap.h"
#include "system/thread/Mutex.h"
#include "engine/util/u3d/Vector3.h"
#include "engine/lua/Lua.h"

#include "SimPlayerController.h"

using namespace server::zone;

class SimMinerSummaryTask;
class ResourceIntelligenceTask;
class MinerTargetRecommendationTask;
class MinerTargetSimulationTask;
class MinerDensityTargetSimulationTask;
class MinerPathValidationSimulationTask;
class MinerPathValidationTask;
class DemandProfileSimulationTask;
class DemandStateSimulationTask;
class MarketSupplyObservationTask;
class StockpileSnapshotSimulationTask;
class DemandWeightedMinerPlanSimulationTask;

class SimPlayerManager : public Singleton<SimPlayerManager>, public Object, public Logger {
private:
	friend class SimMinerSummaryTask;
	friend class ResourceIntelligenceTask;
	friend class MinerTargetRecommendationTask;
	friend class MinerTargetSimulationTask;
	friend class MinerDensityTargetSimulationTask;
	friend class MinerPathValidationSimulationTask;
	friend class MinerPathValidationTask;
	friend class DemandProfileSimulationTask;
	friend class DemandStateSimulationTask;
	friend class MarketSupplyObservationTask;
	friend class StockpileSnapshotSimulationTask;
	friend class DemandWeightedMinerPlanSimulationTask;

	// Map of Creature ObjectID -> Controller
	SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;
	VectorMap<String, uint64> conceptualMinerTotals;
	Mutex conceptualMinerTotalsMutex;

    Lua* lua;

public:
	struct ShuttleportLocation {
		String planet;
		String name;
		Vector3 spawn;
		Vector3 hangout;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	struct SpawnGroup {
		String type;
		int totalCount = 0;
		String behavior;
		String faction;
		Vector<String> templates;
		SimMinerConfig minerConfig;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	void cyclePvPBotWhenShuttleReady(uint64 oldOid,
                                const String& groupType,
                                const String& templateName,
                                bool imperial,
                                const String& fromPlanet,
                                const String& fromLocation,
                                int attempts = 0);

private:
	bool enabled = false;
	bool minerSummaryLoggingEnabled = false;
	bool minerSummaryTaskScheduled = false;
	int minerSummaryIntervalSeconds = 300;
	bool resourceIntelligenceEnabled = false;
	bool resourceIntelligenceLogTopResources = false;
	bool resourceIntelligenceTaskScheduled = false;
	int resourceIntelligenceIntervalSeconds = 600;
	int resourceIntelligenceTopN = 10;
	bool resourceScoringProfilesEnabled = false;
	Vector<String> resourceScoringProfileKeys;
	bool minerTargetRecommendationsEnabled = false;
	bool minerTargetRecommendationTaskScheduled = false;
	int minerTargetRecommendationIntervalSeconds = 300;
	int minerTargetRecommendationTopN = 1;
	bool minerTargetRecommendationIncludeAllActiveMiners = true;
	Vector<String> minerTargetRecommendationProfileKeys;
	bool minerTargetSimulationEnabled = false;
	bool minerTargetSimulationTaskScheduled = false;
	int minerTargetSimulationIntervalSeconds = 300;
	bool minerTargetSimulationPreferSamePlanet = true;
	int minerTargetSimulationSamePlanetBonus = 150;
	int minerTargetSimulationTravelPenalty = 100;
	String minerTargetSimulationAssignmentMode = "round_robin";
	VectorMap<String, float> minerTargetSimulationProfileWeights;
	bool minerDensityTargetSimulationEnabled = false;
	bool minerDensityTargetSimulationTaskScheduled = false;
	int minerDensityTargetSimulationIntervalSeconds = 300;
	Vector<int> minerDensityTargetSimulationSearchRadii;
	int minerDensityTargetSimulationSamplesPerRadius = 48;
	float minerDensityTargetSimulationMinAcceptableDensity = 0.65f;
	float minerDensityTargetSimulationPreferredDensity = 0.80f;
	bool minerDensityTargetSimulationRequireNavmesh = true;
	int minerDensityTargetSimulationMaxPathCheckAttempts = 8;
	float minerDensityTargetSimulationDistancePenaltyPerMeter = 0.02f;
	bool minerPathValidationSimulationEnabled = false;
	bool minerPathValidationSimulationTaskScheduled = false;
	int minerPathValidationSimulationIntervalSeconds = 300;
	bool minerPathValidationOnlyAcceptedDensityTargets = true;
	int minerPathValidationMaxPathDistance = 2500;
	int minerPathValidationMaxPathNodes = 256;
	bool demandProfileSimulationEnabled = false;
	bool demandProfileSimulationTaskScheduled = false;
	int demandProfileSimulationIntervalSeconds = 300;
	String demandProfileSimulationServerPhase = "mature_server";
	int demandProfileSimulationLogTopN = 3;
	VectorMap<String, int> demandProfileSimulationProfileEnabled;
	VectorMap<String, float> demandProfileSimulationProfileWeights;
	VectorMap<String, int> demandProfileSimulationProfilePriorities;
	bool demandStateSimulationEnabled = false;
	bool demandStateSimulationTaskScheduled = false;
	int demandStateSimulationIntervalSeconds = 300;
	int demandStateSimulationLogTopN = 3;
	String demandStateSimulationSupplyMode = "conceptual_totals";
	float demandStateSimulationActiveOpportunityWeight = 1.f;
	float demandStateSimulationShortageWeight = 1.f;
	float demandStateSimulationSurplusDampening = 0.5f;
	VectorMap<String, int> demandStateSimulationProfileEnabled;
	VectorMap<String, int> demandStateSimulationDesiredReserve;
	VectorMap<String, float> demandStateSimulationLowStockThreshold;
	VectorMap<String, float> demandStateSimulationCriticalStockThreshold;
	bool marketSupplyObservationEnabled = false;
	bool marketSupplyObservationTaskScheduled = false;
	int marketSupplyObservationIntervalSeconds = 300;
	int marketSupplyObservationMaxListingsScanned = 5000;
	bool marketSupplyObservationIncludeBazaar = true;
	bool marketSupplyObservationIncludePlayerVendors = true;
	bool marketSupplyObservationIncludeVendorStockrooms = false;
	bool marketSupplyObservationIncludePlayerInventory = false;
	bool marketSupplyObservationIncludePrivateContainers = false;
	int marketSupplyObservationMinQuantity = 1;
	int marketSupplyObservationLogTopN = 5;
	Mutex marketSupplyObservationMutex;
	int marketSupplyObservationListingsScanned = 0;
	int marketSupplyObservationResourceListings = 0;
	uint64 marketSupplyObservationTotalQuantity = 0;
	VectorMap<String, uint64> marketSupplyProfileQuantities;
	VectorMap<String, int> marketSupplyProfileListings;
	VectorMap<String, float> marketSupplyProfileCheapestPricePerUnit;
	VectorMap<String, float> marketSupplyProfileMedianPricePerUnit;
	VectorMap<String, String> marketSupplyProfileConfidence;
	VectorMap<String, String> marketSupplyProfileTopResource;
	VectorMap<String, String> marketSupplyProfileTopType;
	bool stockpileSnapshotSimulationEnabled = false;
	bool stockpileSnapshotSimulationTaskScheduled = false;
	int stockpileSnapshotSimulationIntervalSeconds = 300;
	int stockpileSnapshotSimulationLogTopN = 10;
	bool stockpileSnapshotSimulationIncludeConceptualMinerTotals = true;
	bool stockpileSnapshotSimulationIncludeMarketObservation = false;
	bool demandWeightedMinerPlanSimulationEnabled = false;
	bool demandWeightedMinerPlanSimulationTaskScheduled = false;
	int demandWeightedMinerPlanSimulationIntervalSeconds = 300;
	int demandWeightedMinerPlanSimulationLogTopN = 20;
	int demandWeightedMinerPlanSimulationSamePlanetBonus = 150;
	int demandWeightedMinerPlanSimulationTravelPenalty = 100;
	int demandWeightedMinerPlanSimulationMaxMinersPerProfile = 2;
	float demandWeightedMinerPlanSimulationMinimumPressureThreshold = 1.f;
	float demandWeightedMinerPlanSimulationStrongPressureRatio = 1.5f;
	String demandWeightedMinerPlanSimulationServerPhase = "mature_server";
	float demandWeightedMinerPlanSimulationActiveOpportunityWeight = 1.f;
	float demandWeightedMinerPlanSimulationShortageWeight = 1.f;
	float demandWeightedMinerPlanSimulationSurplusDampening = 0.5f;
	bool demandWeightedMinerPlanSimulationIncludeMarketSupply = false;
	VectorMap<String, int> demandWeightedMinerPlanSimulationProfileEnabled;
	VectorMap<String, int> demandWeightedMinerPlanSimulationDesiredReserve;
	VectorMap<String, float> demandWeightedMinerPlanSimulationLowStockThreshold;
	VectorMap<String, float> demandWeightedMinerPlanSimulationCriticalStockThreshold;
	Vector<ShuttleportLocation> allShuttleports;
	Vector<SpawnGroup> spawnGroups;

	// Lua config loading / spawning
	void loadLuaConfig();
	void spawnConfiguredGroups();
	void startControllerForAgent(AiAgent* agent, Reference<SimPlayerController*> ctrl);

	bool pickRandomShuttleport(ShuttleportLocation& out) const;
	bool isNearestShuttleBoardable(CreatureObject* c);
	void scheduleMinerSummaryTask();
	void runMinerSummaryTask();
	int countActiveMiners();
	void collectConceptualMinerTotals(Vector<String>& resourceNames, Vector<uint64>& amounts);
	void logConceptualMinerSummary();
	void scheduleResourceIntelligenceTask();
	void runResourceIntelligenceTask();
	void logResourceIntelligenceSummary();
	void scheduleMinerTargetRecommendationTask();
	void runMinerTargetRecommendationTask();
	void logMinerTargetRecommendations();
	void scheduleMinerTargetSimulationTask();
	void runMinerTargetSimulationTask();
	void logMinerTargetSimulations();
	void scheduleMinerDensityTargetSimulationTask();
	void runMinerDensityTargetSimulationTask();
	void logMinerDensityTargetSimulations();
	void scheduleMinerPathValidationSimulationTask();
	void runMinerPathValidationSimulationTask();
	void logMinerPathValidationSimulations();
	void scheduleDemandProfileSimulationTask();
	void runDemandProfileSimulationTask();
	void logDemandProfileSimulations();
	void refreshDemandProfileSimulationConfig();
	void scheduleDemandStateSimulationTask();
	void runDemandStateSimulationTask();
	void logDemandStateSimulations();
	void refreshDemandStateSimulationConfig();
	void applyDemandStateSimulationConfig(LuaObject& demandStateConfig);
	void scheduleMarketSupplyObservationTask();
	void runMarketSupplyObservationTask();
	void refreshMarketSupplyObservationConfig();
	void applyMarketSupplyObservationConfig(LuaObject& marketSupplyConfig);
	void observeMarketSupply();
	void clearMarketSupplyObservationSnapshot();
	void scheduleStockpileSnapshotSimulationTask();
	void runStockpileSnapshotSimulationTask();
	void refreshStockpileSnapshotSimulationConfig();
	void applyStockpileSnapshotSimulationConfig(LuaObject& stockpileSnapshotConfig);
	void logStockpileSnapshotSimulation();
	void scheduleDemandWeightedMinerPlanSimulationTask();
	void runDemandWeightedMinerPlanSimulationTask();
	void refreshDemandWeightedMinerPlanSimulationConfig();
	void applyDemandWeightedMinerPlanSimulationConfig(LuaObject& demandWeightedConfig);
	void applyDemandWeightedMinerPlanDependencyConfig(LuaObject& managerConfig);
	void logDemandWeightedMinerPlanSimulations();
	
	String pickRandomTemplate(const SpawnGroup& g) const;
	bool isImperialForSpawn(const SpawnGroup& g, const String& templateName) const;

	void spawnFromConfig(const SpawnGroup& g, const ShuttleportLocation& loc, const String& templateName);

public:
	SimPlayerManager();
	~SimPlayerManager();

	// Called by ZoneServer on startup
	void initialize();

	// The main logic to spawn a specific bot
	void spawnSimPlayer(const String& planet, float x, float y, const String& templateName);

	// Toggle logic
	void toggleBot(AiAgent* agent);

	uint64 recordConceptualMinerYield(const String& resourceName, int amount, uint64 sourceObjectID, bool logYield);

	// Cycle logic (called by SimPvPController)
	void cyclePvPBot(uint64 oldOid,
	                 const String& groupType,
	                 const String& templateName,
	                 bool imperial,
	                 const String& fromPlanet,
	                 const String& fromLocation);

	void spawnSimPlayerWithRoute(const String& planet,
                    			const Vector3& spawn,
                    			const Vector3& hangout,
                    			const String& templateName,
                    			const String& groupType,
                    			const String& locationName);
};

#endif /* SIMPLAYERMANAGER_H_ */
