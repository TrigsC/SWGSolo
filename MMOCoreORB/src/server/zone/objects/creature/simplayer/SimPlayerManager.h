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
#include "engine/util/JSONSerializationType.h"
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
class AiEconomyConceptualTotalsPersistenceTask;
class MinerIntelligentTargetingTask;

struct MinerPathValidationSnapshot {
	String zoneName;
	String profileKey;
	String resourceName;
	String resourceType;
	bool acceptedDensityTarget = false;
	bool pathFound = false;
	String targetSource;
	String rejectReason;
	String pathTrustStatus;
	int pathNodes = 0;
	float pathDistance = 0.f;
	float density = 0.f;
	float directDistance = 0.f;
	float targetX = 0.f;
	float targetY = 0.f;
	float targetZ = 0.f;
	uint64 recordedAtMs = 0;
};

struct MinerIntelligentTargetAssignment {
	uint64 minerID = 0;
	uint64 createdAtMs = 0;
	uint64 updatedAtMs = 0;
	uint64 validatedAtMs = 0;
	uint64 queuedAtMs = 0;
	uint64 activatedAtMs = 0;
	uint64 sampleStartedAtMs = 0;
	uint64 sampleFinishedAtMs = 0;
	uint64 expiresAtMs = 0;
	String targetSource;
	String selectedProfileKey;
	String assignmentReason;
	String demandState;
	float pressureScore = 0.f;
	String targetResourceName;
	String targetResourceType;
	String targetZoneName;
	float targetX = 0.f;
	float targetY = 0.f;
	float targetZ = 0.f;
	float targetDensity = 0.f;
	String densityTargetStatus;
	String pathValidationStatus;
	String pathValidationTrustStatus;
	bool pathValidationMatched = false;
	String status;
	String clearReason;
	String lastActivationResult;
	String lastFailureReason;

	bool isValid() const {
		return minerID != 0 && targetSource == "demand_weighted_plan" &&
			!selectedProfileKey.isEmpty() && !targetResourceName.isEmpty() &&
			!targetResourceType.isEmpty() && !targetZoneName.isEmpty() &&
			densityTargetStatus == "accepted" && expiresAtMs > 0;
	}
};

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
	friend class AiEconomyConceptualTotalsPersistenceTask;
	friend class MinerIntelligentTargetingTask;

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
	Mutex minerPathValidationSnapshotMutex;
	VectorMap<uint64, MinerPathValidationSnapshot> minerPathValidationSnapshots;
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
	bool aiEconomyPersistConceptualMinerTotals = false;
	bool aiEconomyPersistenceTaskScheduled = false;
	bool aiEconomyPersistenceLogSummary = true;
	bool aiEconomyPersistenceFailureLogged = false;
	int aiEconomyPersistenceIntervalSeconds = 300;
	bool persistentStockpileDemandEnabled = false;
	bool persistentStockpileDemandIncludeConceptualMinerLots = true;
	bool persistentStockpileDemandLogSummary = true;
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
	bool minerIntelligentTargetingEnabled = false;
	bool minerIntelligentTargetingTaskScheduled = false;
	String minerIntelligentTargetingMode = "off";
	int minerIntelligentTargetingIntervalSeconds = 300;
	int minerIntelligentTargetingMaxActiveMiners = 1;
	bool minerIntelligentTargetingRequireDemandWeightedPlan = true;
	bool minerIntelligentTargetingRequireAcceptedDensityTarget = true;
	bool minerIntelligentTargetingRequireValidPath = true;
	bool minerIntelligentTargetingFallbackToConceptualLoop = true;
	int minerIntelligentTargetingRollbackOnFailureCount = 3;
	bool minerIntelligentTargetingLogDecisionSummary = true;
	bool minerIntelligentTargetingLogVerboseSwitchDecisions = false;
	bool minerIntelligentTargetingAssignmentEnabled = true;
	int minerIntelligentTargetingAssignmentTtlSeconds = 30;
	bool minerIntelligentTargetingAssignmentReplaceOnlyWhenExpiredOrInvalid = true;
	bool minerIntelligentTargetingAssignmentClearOnSampleComplete = true;
	bool minerIntelligentTargetingAssignmentClearOnCombat = true;
	bool minerIntelligentTargetingAssignmentClearOnIncapOrDeath = true;
	bool minerIntelligentTargetingAssignmentClearOnZoneChange = true;
	bool minerIntelligentTargetingAssignmentLogLifecycle = true;
	bool minerIntelligentTargetingAssignmentLogRetained = false;
	bool minerIntelligentTargetingLimitedActivationEnabled = false;
	int minerIntelligentTargetingLimitedMaxActivationsPerInterval = 1;
	int minerIntelligentTargetingLimitedMaxActiveIntelligentMiners = 1;
	int minerIntelligentTargetingLimitedCooldownSecondsPerMiner = 0;
	bool minerIntelligentTargetingLimitedRequireSamePlanet = true;
	bool minerIntelligentTargetingLimitedDisableOnFirstFailure = true;
	bool minerIntelligentTargetingLimitedDisableOnActivationFailure = false;
	bool minerIntelligentTargetingLimitedLogActivationLifecycle = true;
	bool minerIntelligentTargetingLimitedLogHealthSummary = true;
	bool minerIntelligentTargetingLimitedEmergencyDisabled = false;
	Vector<String> minerIntelligentTargetingLimitedAllowedZones;
	Mutex minerIntelligentTargetingFailureMutex;
	VectorMap<uint64, int> minerIntelligentTargetingFailureCounts;
	Mutex minerIntelligentTargetingCooldownMutex;
	VectorMap<uint64, uint64> minerIntelligentTargetingLastActivationMs;
	Mutex minerIntelligentTargetingHealthMutex;
	int minerIntelligentActivationHealthAttempts = 0;
	int minerIntelligentActivationHealthStarted = 0;
	int minerIntelligentActivationHealthArrivals = 0;
	int minerIntelligentActivationHealthSamplesCompleted = 0;
	int minerIntelligentActivationHealthPathFailures = 0;
	int minerIntelligentActivationHealthExpired = 0;
	int minerIntelligentActivationHealthCooldownSkips = 0;
	int minerIntelligentActivationHealthActiveCapSkips = 0;
	int minerIntelligentActivationHealthZoneSkips = 0;
	Mutex minerIntelligentTargetingAssignmentMutex;
	VectorMap<uint64, MinerIntelligentTargetAssignment> minerIntelligentTargetAssignments;
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
	void recordMinerPathValidationSnapshot(uint64 minerID, const MinerPathValidationSnapshot& snapshot);
	bool getMinerPathValidationSnapshot(uint64 minerID, MinerPathValidationSnapshot& snapshot);
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
	void scheduleAiEconomyPersistenceTask();
	void runAiEconomyPersistenceTask();
	void refreshAiEconomyPersistenceConfig();
	void applyAiEconomyPersistenceConfig(LuaObject& persistenceConfig);
	void applyPersistentStockpileDemandConfig(LuaObject& stockpileDemandConfig);
	void scheduleDemandWeightedMinerPlanSimulationTask();
	void runDemandWeightedMinerPlanSimulationTask();
	void refreshDemandWeightedMinerPlanSimulationConfig();
	void applyDemandWeightedMinerPlanSimulationConfig(LuaObject& demandWeightedConfig);
	void applyDemandWeightedMinerPlanDependencyConfig(LuaObject& managerConfig);
	void logDemandWeightedMinerPlanSimulations();
	void scheduleMinerIntelligentTargetingTask();
	void runMinerIntelligentTargetingTask();
	void refreshMinerIntelligentTargetingConfig();
	void applyMinerIntelligentTargetingConfig(LuaObject& targetingConfig);
	void logMinerIntelligentTargetingDecisions();
	bool getMinerIntelligentTargetAssignment(uint64 minerID, MinerIntelligentTargetAssignment& assignment);
	void putMinerIntelligentTargetAssignment(const MinerIntelligentTargetAssignment& assignment);
	void clearMinerIntelligentTargetAssignment(uint64 minerID, const String& reason, const String& mode);
	void recordMinerIntelligentTargetAssignmentLifecycle(uint64 minerID, const String& eventName, const String& detail);
	bool isMinerIntelligentTargetZoneAllowed(const String& zoneName);
	bool isMinerIntelligentAssignmentActive(const MinerIntelligentTargetAssignment& assignment);
	int countActiveMinerIntelligentAssignments();
	bool isMinerIntelligentActivationOnCooldown(uint64 minerID, uint64 nowMs);
	void rememberMinerIntelligentActivation(uint64 minerID, uint64 nowMs);
	void recordMinerIntelligentActivationHealthEvent(const String& eventName);
	void snapshotAndResetMinerIntelligentActivationHealth(int& attempts, int& started, int& arrivals, int& samplesCompleted, int& pathFailures, int& expired, int& cooldownSkips, int& activeCapSkips, int& zoneSkips);

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
	void clearMinerIntelligentTargetAssignmentFromController(uint64 minerID, const String& reason);
	void clearMinerIntelligentTargetAssignmentOnSampleComplete(uint64 minerID);
	void recordMinerIntelligentTargetAssignmentLifecycleFromController(uint64 minerID, const String& eventName, const String& detail = "");
	JSONSerializationType getAiEconomyDashboardSnapshot();

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
