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
class MinerRecoveryTask;
class SimPlayerConfiguredSpawnTask;
class HiveCrafterConsumerTask;

// P.5.3: defined in the .cpp (file scope); forward-declared here so the crafter
// consumer can share the demand-state compute helper by reference.
struct DemandStateSimulationResult;

struct MinerPathValidationSnapshot {
	uint64 validationSnapshotId = 0;
	uint64 assignmentGenerationId = 0;
	String targetHash;
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
	float minerX = 0.f;
	float minerY = 0.f;
	float minerZ = 0.f;
	bool directFallback = false;
	bool minerInNavmeshKnown = false;
	bool minerInNavmesh = false;
	bool targetNavmeshChecked = false;
	bool targetInNavmesh = false;
	bool targetTerrainHeightKnown = false;
	float targetTerrainHeight = 0.f;
	float targetZDelta = 0.f;
	int maxPathDistance = 0;
	int maxPathNodes = 0;
	uint64 recordedAtMs = 0;
	// P.4.1 overland reachability diagnostics (additive only; does not affect
	// pathTrustStatus, rejectReason, or the activation gate).
	bool overlandEvaluated = false;
	bool overlandReachable = false;
	String overlandRejectReason = "none";
	bool overlandWaterAtTarget = false;
};

struct MinerIntelligentTargetAssignment {
	uint64 minerID = 0;
	uint64 assignmentGenerationId = 0;
	uint64 createdAtMs = 0;
	uint64 updatedAtMs = 0;
	uint64 validatedAtMs = 0;
	uint64 queuedAtMs = 0;
	uint64 activatedAtMs = 0;
	uint64 sampleStartedAtMs = 0;
	uint64 sampleFinishedAtMs = 0;
	uint64 stationedAtMs = 0;
	uint64 lastStationSampleAtMs = 0;
	uint64 expiresAtMs = 0;
	bool normalTtlSkippedForActiveMovement = false;
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
	float targetDirectDistance = 0.f;
	// P.5.1: exact resource-spawn identity captured at selection so hive
	// deposits can record crafting-grade lots (spawn id + stats), not just a
	// coarse conceptual label. 0 spawn id => degrade to conceptual-only.
	uint64 targetResourceSpawnObjectId = 0;
	String targetResourceClassChain;
	bool targetResourceActive = true;
	int targetResourceOq = -1;
	int targetResourceCd = -1;
	int targetResourceDr = -1;
	int targetResourceHr = -1;
	int targetResourceFl = -1;
	int targetResourceMa = -1;
	int targetResourcePe = -1;
	int targetResourceSr = -1;
	int targetResourceUt = -1;
	int targetResourceCr = -1;
	String densityTargetStatus;
	String pathValidationStatus;
	String pathValidationTrustStatus;
	String currentPathValidationStatus;
	String currentPathTrustStatus;
	uint64 latestValidationSnapshotId = 0;
	String latestValidationTargetHash;
	String latestValidationMismatchReason;
	bool pathValidationMatched = false;
	String targetHash;
	uint64 validatedSnapshotId = 0;
	String validatedTargetHash;
	String validatedPathValidationStatus;
	String validatedPathTrustStatus;
	float latestPathDistance = 0.f;
	float validatedPathDistance = 0.f;
	uint64 activationSnapshotId = 0;
	String activationTargetHash;
	String activationPathValidationStatus;
	String activationPathTrustStatus;
	float activationPathDistance = 0.f;
	bool lifecycleDowngradePrevented = false;
	String status;
	String clearReason;
	String lastActivationResult;
	String lastFailureReason;
	String rebalanceReason;
	int stationSampleCount = 0;
	uint64 stationYieldQuantity = 0;
	uint64 stationDurationSeconds = 0;
	int stationCoverageRetainedCount = 0;
	bool reachabilityValidatedRecorded = false;
	bool reachabilityRejectedRecorded = false;
	bool reachabilityActivatedRecorded = false;
	bool reachabilitySampleCompletedRecorded = false;
	bool reachabilityStationedCoverageRecorded = false;

	bool isValid() const {
		return minerID != 0 && targetSource == "demand_weighted_plan" &&
			!selectedProfileKey.isEmpty() && !targetResourceName.isEmpty() &&
			!targetResourceType.isEmpty() && !targetZoneName.isEmpty() &&
			densityTargetStatus == "accepted" && expiresAtMs > 0;
	}

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct SimIntelligentYieldSnapshot {
	uint64 minerID = 0;
	uint64 recordedAtMs = 0;
	uint64 assignmentGenerationId = 0;
	String targetHash;
	uint64 activationSnapshotId = 0;
	String activationPathValidationStatus;
	String activationPathTrustStatus;
	uint64 assignmentCreatedAtMs = 0;
	uint64 assignmentAgeSeconds = 0;
	int amount = 0;
	String conceptualLabel;
	String sourceResourceName;
	String sourceResourceType;
	String sourceZone;
	float sourceX = 0.f;
	float sourceY = 0.f;
	float sourceZ = 0.f;
	float sourceDensity = 0.f;
	String selectedDemandProfile;
	String demandState;
	float pressureScore = 0.f;
	String yieldMode;
	String identityConfidence;
	bool realResourceCreated = false;
	bool resourceContainerCreated = false;
	bool inventoryMutated = false;
	bool economyMutated = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct SimulatedAcquisitionEvent {
	uint64 timestampMs = 0;
	uint64 minerID = 0;
	uint64 assignmentGenerationId = 0;
	uint64 activationSnapshotId = 0;
	uint64 stationedAtMs = 0;
	uint64 stationDurationSeconds = 0;
	String resourceName;
	String resourceType;
	String resourceClass;
	String planet;
	String spawnIdentity;
	String demandProfile;
	String activationPathTrustStatus;
	String conceptualLabel;
	uint32 quantity = 0;
	float density = 0.f;
	float concentration = 0.f;
	bool wouldCreateResourceContainer = true;
	bool realResourceCreated = false;
	bool resourceContainerCreated = false;
	bool inventoryMutated = false;
	bool economyMutated = false;
	bool persistenceMutated = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

// P.5.1: per-resource-spawn running yield accumulated this session, flushed to
// the persistent galaxy hive stockpile as an exact-identity lot (spawn id +
// stats). Keyed hive-wide by resource-spawn object id, not by miner.
struct MinerSpawnYieldAccumulator {
	uint64 resourceSpawnObjectId = 0;
	uint64 sessionQuantity = 0;
	// P.5.2: how much of sessionQuantity has already been written to the hive
	// lot, so each flush adds only the new delta (deposits and consumer draws
	// then compose instead of overwriting each other).
	uint64 lastFlushedQuantity = 0;
	String resourceSpawnName;
	String resourceType;
	String resourceClassChain;
	String sourcePlanet;
	String sourceZone;
	String matchedDemandProfiles;
	bool active = true;
	int oq = -1;
	int cd = -1;
	int dr = -1;
	int hr = -1;
	int fl = -1;
	int ma = -1;
	int pe = -1;
	int sr = -1;
	int ut = -1;
	int cr = -1;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MinerRecoveryDiagnosticRow {
	uint64 minerID = 0;
	uint64 assignmentGenerationId = 0;
	uint64 activationSnapshotId = 0;
	String targetHash;
	String resourceName;
	String resourceType;
	String demandProfile;
	String lifecycleStatus;
	String recoveryStatus;
	String stuckReason;
	String recoveryRecommendation;
	String lastRecoveryAction;
	String currentZone;
	String targetZone;
	float currentX = 0.f;
	float currentY = 0.f;
	float currentZ = 0.f;
	float targetX = 0.f;
	float targetY = 0.f;
	float targetZ = 0.f;
	float distanceToTarget = 0.f;
	uint64 assignmentAgeSeconds = 0;
	uint64 movementAgeSeconds = 0;
	uint64 sampleAgeSeconds = 0;
	uint64 acquisitionAgeSeconds = 0;
	uint64 expectedNextSampleAtMs = 0;
	uint64 expectedNextSampleAgeSeconds = 0;
	uint64 stationDurationSeconds = 0;
	int stationSampleCount = 0;
	uint64 stationYieldQuantity = 0;
	bool controllerFound = false;
	bool minerFound = false;
	bool positionKnown = false;
	bool dead = false;
	bool incapacitated = false;
	bool inCombat = false;
	bool healthy = false;
	bool needsAttention = false;
	bool adminActionsEnabled = false;
	bool dryRun = true;
	String copyableCurrentCoordinates;
	String copyableTargetCoordinates;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct SimulatedAcquisitionRuntimeState;

struct SimResourceAwareStockpileRow {
	String aggregationKey;
	uint64 quantity = 0;
	int eventCount = 0;
	uint64 firstObservedMs = 0;
	uint64 lastObservedMs = 0;
	String conceptualLabel;
	String sourceResourceName;
	String sourceResourceType;
	String sourceZone;
	float sourceX = 0.f;
	float sourceY = 0.f;
	float sourceZ = 0.f;
	float sourceDensity = 0.f;
	String selectedDemandProfile;
	String demandState;
	float pressureScore = 0.f;
	String acquisitionSource;
	String resourceLifecycleState;
	String identityConfidence;
	String yieldMode;
	bool realResourceCreated = false;
	bool resourceContainerCreated = false;
	bool inventoryMutated = false;
	bool economyMutated = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MinerAssignmentHistorySnapshot {
	uint64 minerID = 0;
	uint64 assignmentGenerationId = 0;
	uint64 recordedAtMs = 0;
	uint64 createdAtMs = 0;
	uint64 validatedAtMs = 0;
	uint64 queuedAtMs = 0;
	uint64 activatedAtMs = 0;
	uint64 sampleStartedAtMs = 0;
	uint64 sampleFinishedAtMs = 0;
	uint64 stationedAtMs = 0;
	uint64 lastStationSampleAtMs = 0;
	uint64 expiresAtMs = 0;
	uint64 movementAgeSeconds = 0;
	uint64 movementTimeoutSeconds = 0;
	uint64 sampleAgeSeconds = 0;
	uint64 sampleTimeoutSeconds = 0;
	bool normalTtlSkippedForActiveMovement = false;
	uint64 latestValidationSnapshotId = 0;
	uint64 validatedSnapshotId = 0;
	uint64 activationSnapshotId = 0;
	String targetHash;
	String latestValidationTargetHash;
	String validatedTargetHash;
	String activationTargetHash;
	String selectedProfileKey;
	String targetResourceName;
	String targetResourceType;
	String targetZoneName;
	String status;
	String clearReason;
	String latestValidationStatus;
	String latestPathTrustStatus;
	String activationValidationStatus;
	String activationPathTrustStatus;
	float latestPathDistance = 0.f;
	float activationPathDistance = 0.f;
	String validationMismatchReason;
	String rebalanceReason;
	int stationSampleCount = 0;
	uint64 stationYieldQuantity = 0;
	uint64 stationDurationSeconds = 0;
	bool lifecycleDowngradePrevented = false;
	bool yielded = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MinerReachabilityCalibrationBucket {
	int candidatesGenerated = 0;
	int candidatesValidated = 0;
	int candidatesRejected = 0;
	int densityTargetsChosen = 0;
	int densityTargetsValidated = 0;
	int densityTargetsActivated = 0;
	int densityTargetsSampleCompleted = 0;
	int coverageRetainedCount = 0;
	int stationedSampleCount = 0;
	uint64 stationedDurationTotalSeconds = 0;
	int stationedDurationSamples = 0;
	float distanceTotal = 0.f;
	int distanceSamples = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MinerReachabilityValidationOutcome {
	int count = 0;
	float distanceTotal = 0.f;
	int distanceSamples = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
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
	friend class SimPlayerConfiguredSpawnTask;

	// Map of Creature ObjectID -> Controller
	SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;
	VectorMap<String, uint64> conceptualMinerTotals;
	Mutex conceptualMinerTotalsMutex;
	// P.5.1: exact-identity per-spawn hive deposits (galaxy-scoped).
	VectorMap<uint64, MinerSpawnYieldAccumulator> spawnYieldAccumulators;
	Mutex spawnYieldAccumulatorMutex;
	Vector<SimIntelligentYieldSnapshot> recentIntelligentYields;
	Mutex recentIntelligentYieldsMutex;
	Vector<SimResourceAwareStockpileRow> resourceAwareStockpileRows;
	Mutex resourceAwareStockpileMutex;
	Vector<MinerAssignmentHistorySnapshot> recentMinerAssignmentHistory;
	Mutex recentMinerAssignmentHistoryMutex;
	Mutex minerReachabilityCalibrationMutex;
	MinerReachabilityCalibrationBucket minerReachabilityTotals;
	VectorMap<String, MinerReachabilityCalibrationBucket> minerReachabilityByPlanet;
	VectorMap<String, MinerReachabilityCalibrationBucket> minerReachabilityByResourceClass;
	VectorMap<String, MinerReachabilityCalibrationBucket> minerReachabilityByDensitySource;
	VectorMap<String, MinerReachabilityCalibrationBucket> minerReachabilityByDistanceBand;
	VectorMap<String, MinerReachabilityValidationOutcome> minerReachabilityValidationOutcomes;
	VectorMap<String, int> minerReachabilityFailureReasons;

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
	bool configuredSpawnTaskScheduled = false;
	int configuredSpawnStartupDelaySeconds = 0;
	int configuredSpawnBatchSize = 5;
	int configuredSpawnBatchDelayMs = 1000;
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
	bool navAreaDensitySelectionEnabled = false;
	bool navAreaDensitySelectionShadowMode = true;
	int navAreaSampleCacheTtlSeconds = 900;
	int navAreaMaxSamplesPerArea = 8;
	int navAreaMaxSampleAttemptsPerCycle = 16;
	int navAreaMaxPathValidationsPerCycle = 0;
	bool navAreaAvoidGenericInteriors = true;
	bool navAreaPreferCityAndPoiRegions = true;
	bool reachabilityMemoryEnabled = true;
	bool reachabilityCandidatePreferenceEnabled = false;
	int reachabilityMemoryTtlSeconds = 1800;
	int reachabilityBucketSizeMeters = 128;
	int reachabilityMinAttemptsBeforePenalty = 3;
	float reachabilityVerifiedPathScoreBonus = 0.15f;
	float reachabilitySampleCompleteScoreBonus = 0.25f;
	float reachabilityRepeatedFailurePenalty = 0.25f;
	float reachabilityLongDistancePenalty512Plus = 0.15f;
	bool reachabilityPlanetPenaltyEnabled = true;
	bool reachabilityResourcePenaltyEnabled = true;
	int reachabilityMaxMemoryRows = 5000;
	bool minerPathValidationSimulationEnabled = false;
	bool minerPathValidationSimulationTaskScheduled = false;
	int minerPathValidationSimulationIntervalSeconds = 300;
	bool minerPathValidationOnlyAcceptedDensityTargets = true;
	int minerPathValidationMaxPathDistance = 2500;
	int minerPathValidationMaxPathNodes = 256;
	Mutex minerPathValidationSnapshotMutex;
	VectorMap<uint64, MinerPathValidationSnapshot> minerPathValidationSnapshots;
	// P.4.1 overland travel reachability guards. Read by MinerPathValidationTask
	// via friend access. P.4.2 adds travelEnableOverlandActivation: when true, an
	// overland-reachable off-navmesh target validates under the directOverland
	// trust tier and is allowed to activate (still no vehicles/economy mutation).
	bool travelOverlandDiagnosticsEnabled = true;
	float travelWaterMarginMeters = 1.0f;
	bool travelRejectWaterTargets = true;
	bool travelEnableOverlandActivation = false;
	// P.4.5a station/shuttle travel: at activation, if a travel point is much
	// closer to the target than the miner, teleport (switchZone) the miner to the
	// station's outdoor arrival point so it only walks the short last leg. Same-
	// planet only, gated, safe reposition (no object containment).
	bool travelEnableStationTravel = false;
	int travelStationMinSavingMeters = 400;
	float travelStationMaxRangeMeters = 16000.f;
	int stationTravelCount = 0;
	int stationTravelMetersSaved = 0;
	// P.4.5b cross-planet dispatch (proportional rebalance, player-mimetic: run to
	// the origin starport's ticket collector, board = switchZone to the
	// destination starport's outdoor arrival, ride, then gather). Default off +
	// dryRun on so the build ships inert. NPC repositioning only — no economy/
	// inventory/persistence mutation (same class as station travel + recovery).
	struct PlanetDispatchPlanetRow {
		String planet;
		int current = 0;
		int desired = 0;
		int demandWeight = 0;
		bool home = false;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};
	bool travelEnablePlanetDispatch = false;
	bool travelPlanetDispatchDryRun = true;
	int travelPlanetDispatchIntervalSeconds = 60;
	int travelPlanetDispatchMinDemandScore = 750;
	int travelPlanetDispatchMinMinersPerHomePlanet = 2;
	int travelPlanetDispatchMaxMinersPerRemotePlanet = 3;
	int travelPlanetDispatchPerMinerCooldownSeconds = 900;
	int travelPlanetDispatchPerPlanetCooldownSeconds = 300;
	float travelPlanetDispatchBoardRadiusMeters = 20.f;
	bool minerPlanetDispatchTaskScheduled = false;
	int planetDispatchCount = 0;
	int travelBoardedCount = 0;
	VectorMap<uint64, uint64> planetDispatchMinerCooldownMs;
	VectorMap<String, uint64> planetDispatchPlanetCooldownMs;
	Mutex planetDispatchMutex;
	Vector<PlanetDispatchPlanetRow> planetDispatchPlanRows;
	int planetDispatchTotalMiners = 0;
	String planetDispatchLastTargetZone;
	uint64 planetDispatchLastDonorId = 0;
	String planetDispatchLastDonorFromZone;
	String planetDispatchLastSkipReason;
	String planetDispatchLastBoardedFromZone;
	String planetDispatchLastBoardedToZone;
	String planetDispatchLastBoardedReason;
	// P.4.4a real vehicle mechanics (spawn/mount/dismount/store). Heavily gated
	// (master flag default off). Simulation-only object lifecycle — no economy/
	// inventory/persistence mutation. Devices stored as SceneObject* to keep the
	// header light; cast to VehicleControlDevice* in the .cpp.
	bool vehicleMechanicsEnabled = false;
	String vehicleObjectTemplate = "object/mobile/vehicle/speederbike_swoop.iff";
	String vehicleControlDeviceTemplate = "object/intangible/vehicle/speederbike_swoop_pcd.iff";
	bool vehicleSelfTestEnabled = false;
	int vehicleSelfTestIntervalSeconds = 180;
	int vehicleSelfTestHoldSeconds = 10;
	// P.4.4b mounted travel: miners deploy+mount a swoop for long overland legs
	// (SimMinerController::maybeMountForTravel) and dismount at every leg exit.
	bool mountedTravelEnabled = false;
	int mountedTravelMinLegMeters = 150;
	int mountedTravelLegCount = 0;
	bool vehicleSelfTestTaskScheduled = false;
	uint64 vehicleSelfTestActiveMinerID = 0;
	Mutex vehicleMechanicsMutex;
	VectorMap<uint64, ManagedReference<SceneObject*> > activeMinerVehicleDevices;
	int vehicleDeployCount = 0;
	int vehicleMountCount = 0;
	int vehicleDismountCount = 0;
	int vehicleStoreCount = 0;
	int vehicleMechanicsFailureCount = 0;
	// Client presentation only: when enabled, spawned sim NPCs get
	// ObjectFlag::PLAYER (0x10) in their pvpStatusBitmask so clients render
	// them with player radar dots / player con-color rules. No server gameplay
	// logic reads this bit on AiAgents (all reads are isPlayerCreature-gated).
	bool simNpcPlayerDotEnabled = false;
	int simNpcPlayerDotFlaggedCount = 0;
	uint64 nextMinerPathValidationSnapshotId = 1;
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
	bool marketSupplyObservationResolveResourceContainers = false;
	int marketSupplyObservationStartupDelaySeconds = 900;
	int marketSupplyObservationMinQuantity = 1;
	int marketSupplyObservationLogTopN = 5;
	uint64 marketSupplyObservationStartedAtMs = 0;
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
	// P.5.1: persist exact resource-spawn-identity hive lots (crafting-grade).
	bool aiEconomyPersistSpawnIdentifiedLots = false;
	bool aiEconomyPersistenceTaskScheduled = false;
	bool aiEconomyPersistenceLogSummary = true;
	bool aiEconomyPersistenceFailureLogged = false;
	int aiEconomyPersistenceIntervalSeconds = 300;
	// P.5.2: non-destructive hive reservation self-test (reserve then release),
	// so the reservation ledger can be verified before a real crafter consumer.
	bool hiveReservationSelfTestEnabled = false;
	bool hiveReservationSelfTestTaskScheduled = false;
	int hiveReservationSelfTestIntervalSeconds = 120;
	int hiveReservationSelfTestReserveQuantity = 5;
	// P.5.3: first crafter consumer — demand-driven reserve+CONSUME that draws
	// hive stock down (the first real consumer). Simulation-only: consume
	// decrements the private hive ledger, no game state. C++ default off.
	bool hiveCrafterConsumerEnabled = false;
	bool hiveCrafterConsumerTaskScheduled = false;
	int hiveCrafterConsumerIntervalSeconds = 90;
	int hiveCrafterConsumerBatchQuantity = 25;
	int hiveCrafterConsumerMinOq = 0;
	bool hiveCrafterConsumerPreferShortage = true;
	bool hiveCrafterConsumerAllowAnyLotFallback = true;
	// Guarded runtime counters (task writes, dashboard reads).
	Mutex hiveCrafterConsumerMutex;
	uint64 hiveCrafterBatchesCompleted = 0;
	uint64 hiveCrafterUnitsConsumed = 0;
	String hiveCrafterLastProfile;
	String hiveCrafterLastReserveReason;
	bool hiveCrafterLastFallbackUsed = false;
	VectorMap<String, uint64> hiveCrafterProducedByProfile;
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
	bool aiTravelSimulationEnabled = true;
	int aiTravelSimulationMaxPlans = 20;
	bool aiTravelSimulationIncludeResourceRushPlans = true;
	bool aiTravelSimulationIncludeHubReturnPlans = true;
	bool aiTravelSimulationHomeHubEnabled = true;
	String aiTravelSimulationHomeHubKey = "coronet_resource_hub";
	String aiTravelSimulationHomeHubZone = "corellia";
	String aiTravelSimulationHomeHubCity = "coronet";
	float aiTravelSimulationHomeHubX = -155.f;
	float aiTravelSimulationHomeHubY = -4722.f;
	String aiTravelSimulationHomeHubPurpose = "sell_resources";
	bool minerIntelligentTargetingEnabled = false;
	bool minerIntelligentTargetingTaskScheduled = false;
	String minerIntelligentTargetingMode = "off";
	int minerIntelligentTargetingIntervalSeconds = 300;
	int minerIntelligentTargetingMaxActiveMiners = 1;
	bool minerIntelligentTargetingRequireDemandWeightedPlan = true;
	bool minerIntelligentTargetingRequireAcceptedDensityTarget = true;
	bool minerIntelligentTargetingRequireValidPath = true;
	// P.4.5c: final-approach arrival radius (m). A miner keeps re-pathing toward
	// its true target until within this distance before stationing, so long
	// off-navmesh walks don't station hundreds of meters short (-> recovery churn).
	float minerIntelligentArrivalRadiusMeters = 15.f;
	bool minerIntelligentTargetingFallbackToConceptualLoop = false;
	int minerIntelligentTargetingRollbackOnFailureCount = 3;
	bool minerIntelligentTargetingLogDecisionSummary = true;
	bool minerIntelligentTargetingLogVerboseSwitchDecisions = false;
	bool legacyMinerConceptualLoopEnabled = false;
	bool legacyMinerAllowFallbackWhenNoIntelligentAssignment = false;
	bool legacyMinerAllowFallbackAfterIntelligentFailure = false;
	bool legacyMinerLogSuppression = true;
	Mutex minerWorkLoopDiagnosticsMutex;
	int legacyMinerLoopSuppressedCount = 0;
	int legacyMinerLoopStartedCount = 0;
	int intelligentMinerLoopStartedCount = 0;
	int staleMinerTaskIgnoredCount = 0;
	String lastLegacyMinerSuppressionReason;
	String lastStaleMinerTaskReason;
	bool minerIntelligentTargetingAssignmentEnabled = true;
	int minerIntelligentTargetingAssignmentTtlSeconds = 30;
	int minerIntelligentTargetingCandidateAssignmentTtlSeconds = 180;
	int minerIntelligentTargetingValidatedAssignmentTtlSeconds = 180;
	int minerIntelligentTargetingQueuedActivationTtlSeconds = 120;
	int minerIntelligentTargetingMovementArrivalTimeoutSeconds = 600;
	int minerIntelligentTargetingMovementArrivalTimeoutMinSeconds = 240;
	int minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds = 1200;
	float minerIntelligentTargetingMovementArrivalSecondsPerMeter = 0.75f;
	int minerIntelligentTargetingSampleStartedTimeoutSeconds = 180;
	bool minerIntelligentTargetingPreventNormalTtlForActiveMovement = true;
	bool minerIntelligentTargetingAssignmentReplaceOnlyWhenExpiredOrInvalid = true;
	bool minerIntelligentTargetingAssignmentClearOnSampleComplete = true;
	bool minerIntelligentTargetingAssignmentClearOnCombat = true;
	bool minerIntelligentTargetingAssignmentClearOnIncapOrDeath = true;
	bool minerIntelligentTargetingAssignmentClearOnZoneChange = true;
	bool minerIntelligentTargetingAssignmentLogLifecycle = true;
	bool minerIntelligentTargetingAssignmentLogRetained = false;
	bool minerMovementReadinessDiagnosticsEnabled = true;
	bool stationedMinerLifecycleEnabled = false;
	bool stationedMinerRepeatedSamplingEnabled = false;
	bool stationedMinerRequireDemandStillValid = true;
	bool stationedMinerRequireResourceStillActive = true;
	bool stationedMinerRequireSamePlanet = true;
	bool stationedMinerClearWhenReserveSatisfied = true;
	bool realResourceAcquisitionEnabled = false;
	bool acquisitionReadinessDiagnosticsEnabled = true;
	bool acquisitionRequireStationedLifecycle = true;
	bool acquisitionRequireVerifiedActivationPath = true;
	bool acquisitionRequireKnownResourceSpawnIdentity = true;
	bool acquisitionRequireDemandStillValid = true;
	bool acquisitionRequireReserveBelowTarget = true;
	int acquisitionMaxAcquisitionsPerInterval = 0;
	bool simulatedAcquisitionTransactionsEnabled = true;
	bool simulatedAcquisitionLogTransactions = true;
	int simulatedAcquisitionMaxLedgerEvents = 200;
	SimulatedAcquisitionRuntimeState* simulatedAcquisitionRuntime = nullptr;
	bool minerRecoveryEnabled = true;
	bool minerRecoveryDryRun = true;
	bool minerRecoveryAllowClearAssignment = true;
	bool minerRecoveryAllowNudgeToSafeNearbyPoint = false;
	bool minerRecoveryAllowTeleportToStationTarget = false;
	bool minerRecoveryAllowRespawnReplacement = false;
	bool minerRecoveryAdminActionsEnabled = false;
	bool minerRecoveryTaskScheduled = false;
	int minerRecoveryStuckCheckIntervalSeconds = 60;
	int minerRecoveryMovingStuckSeconds = 180;
	int minerRecoveryStationedSamplingGraceSeconds = 90;
	float minerRecoveryFarFromStationDistanceMeters = 32.f;
	int minerRecoveryMaxAutomaticRecoveriesPerInterval = 2;
	int minerRecoveryMaxRecoveriesPerMinerPerHour = 3;
	bool minerRecoveryLogRecoveryDecisions = true;
	Mutex minerRecoveryMutex;
	int minerRecoveryActionsTaken = 0;
	int minerRecoveryActionsSkipped = 0;
	uint64 minerRecoveryIntervalBucketStartMs = 0;
	uint64 minerRecoveryHourBucketStartMs = 0;
	int minerRecoveryAutomaticRecoveriesThisInterval = 0;
	VectorMap<uint64, int> minerRecoveryCountPerMinerThisHour;
	VectorMap<uint64, String> minerRecoveryLastActionByMiner;
	VectorMap<uint64, uint64> minerRecoveryLastActionAtMsByMiner;
	VectorMap<String, int> minerRecoveryReasonCounts;
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
	int minerIntelligentActivationHealthCandidateExpired = 0;
	int minerIntelligentActivationHealthValidatedExpired = 0;
	int minerIntelligentActivationHealthQueuedActivationTimeout = 0;
	int minerIntelligentActivationHealthMovementArrivalTimeout = 0;
	int minerIntelligentActivationHealthSampleTimeout = 0;
	int minerIntelligentActivationHealthExpiredWhileActivePrevented = 0;
	int minerIntelligentActivationHealthNormalTtlSkippedForActiveMovement = 0;
	int minerIntelligentActivationHealthCooldownSkips = 0;
	int minerIntelligentActivationHealthActiveCapSkips = 0;
	int minerIntelligentActivationHealthZoneSkips = 0;
	Mutex minerIntelligentTargetingAssignmentMutex;
	VectorMap<uint64, MinerIntelligentTargetAssignment> minerIntelligentTargetAssignments;
	uint64 nextMinerAssignmentGenerationId = 1;
	Vector<ShuttleportLocation> allShuttleports;
	Vector<SpawnGroup> spawnGroups;

	// Lua config loading / spawning
	void loadLuaConfig();
	void spawnConfiguredGroups();
	void scheduleConfiguredSpawnTask(int groupIndex, int spawnIndex, int delayMs);
	void runConfiguredSpawnTask(int groupIndex, int spawnIndex);
	void startControllerForAgent(AiAgent* agent, Reference<SimPlayerController*> ctrl);

	bool pickRandomShuttleport(ShuttleportLocation& out) const;
	bool isNearestShuttleBoardable(CreatureObject* c);
	void scheduleMinerSummaryTask();
	void runMinerSummaryTask();
	int countActiveMiners();
	void collectConceptualMinerTotals(Vector<String>& resourceNames, Vector<uint64>& amounts);
	// P.5.1: accumulate an exact-identity per-spawn deposit; drained by the
	// persistence task and flushed to the galaxy hive stockpile.
	void recordSpawnIdentifiedMinerYield(const MinerIntelligentTargetAssignment& assignment, int amount);
	void collectSpawnYieldAccumulators(Vector<MinerSpawnYieldAccumulator>& accumulators);
	void markSpawnYieldFlushed(uint64 resourceSpawnObjectId, uint64 flushedQuantity);
	void recordResourceAwareConceptualStockpileYield(const SimIntelligentYieldSnapshot& snapshot);
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
	uint64 recordMinerPathValidationSnapshot(uint64 minerID, MinerPathValidationSnapshot& snapshot);
	bool getMinerPathValidationSnapshot(uint64 minerID, MinerPathValidationSnapshot& snapshot);
	void scheduleDemandProfileSimulationTask();
	void runDemandProfileSimulationTask();
	void logDemandProfileSimulations();
	void refreshDemandProfileSimulationConfig();
	void scheduleDemandStateSimulationTask();
	void runDemandStateSimulationTask();
	void logDemandStateSimulations();
	// P.5.3: shared demand-state compute (extracted from logDemandStateSimulations)
	// so the crafter consumer selects targets from the same source of truth.
	void computeDemandStateResults(Vector<DemandStateSimulationResult>& results,
		bool& activeSnapshotAvailable, String& snapshotError);
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
	void flushSpawnIdentifiedLotsToHive();
	void refreshAiEconomyPersistenceConfig();
	void applyAiEconomyPersistenceConfig(LuaObject& persistenceConfig);
	void scheduleHiveReservationSelfTestTask();
	void applyHiveReservationSelfTestConfig(LuaObject& selfTestConfig);
	void scheduleHiveCrafterConsumerTask();
	void applyHiveCrafterConsumerConfig(LuaObject& crafterConfig);
	void applyPersistentStockpileDemandConfig(LuaObject& stockpileDemandConfig);
	void scheduleDemandWeightedMinerPlanSimulationTask();
	void runDemandWeightedMinerPlanSimulationTask();
	void refreshDemandWeightedMinerPlanSimulationConfig();
	void applyDemandWeightedMinerPlanSimulationConfig(LuaObject& demandWeightedConfig);
	void applyDemandWeightedMinerPlanDependencyConfig(LuaObject& managerConfig);
	void logDemandWeightedMinerPlanSimulations();
	void applyAiTravelSimulationConfig(LuaObject& travelSimulationConfig);
	void applyRealResourceAcquisitionConfig(LuaObject& acquisitionConfig);
	void applyMinerRecoveryConfig(LuaObject& recoveryConfig);
	void applyLegacyMinerLoopConfig(LuaObject& legacyLoopConfig);
	void scheduleMinerIntelligentTargetingTask();
	void runMinerIntelligentTargetingTask();
	void scheduleMinerRecoveryTask();
	void refreshMinerIntelligentTargetingConfig();
	void applyStationedMinerConfig(LuaObject& stationedConfig);
	void applyMinerIntelligentTargetingConfig(LuaObject& targetingConfig);
	void applyTravelConfig(LuaObject& travelConfig);
	void applyVehicleConfig(LuaObject& vehicleConfig);
	// Sets the agent's pvpStatusBitmask to baseBits, adding ObjectFlag::PLAYER
	// when simNpcPlayerDotEnabled. Call from spawn paths with the agent locked.
	void applySimNpcPresentation(AiAgent* agent, uint32 baseBits);
	// P.4.2: a path trust tier acceptable for activation. verifiedPath always;
	// directOverland only when travelEnableOverlandActivation is set.
	bool isActivationTrustAcceptable(const String& trustStatus) const;
	// P.4.5a: teleport the miner to the shuttle/starport nearest its target if it
	// meaningfully shortens the overland leg. Returns true if it teleported.
	bool tryStationTravelForActivation(uint64 minerID, const String& targetZone,
		float targetX, float targetY, float targetZ);
	// P.4.5b: cross-planet dispatch (proportional rebalance, player-mimetic).
	// runMinerPlanetDispatchTask() + recordInterplanetaryTravelBoarded() are
	// called externally (task / controller) and live in the public section.
	void scheduleMinerPlanetDispatchTask();
	void refreshMinerPlanetDispatchConfig();
	void logMinerPlanetDispatchDecisions();
	void logMinerIntelligentTargetingDecisions();
	bool getMinerIntelligentTargetAssignment(uint64 minerID, MinerIntelligentTargetAssignment& assignment);
	void putMinerIntelligentTargetAssignment(const MinerIntelligentTargetAssignment& assignment);
	void clearMinerIntelligentTargetAssignment(uint64 minerID, const String& reason, const String& mode);
	void recordMinerIntelligentTargetAssignmentLifecycle(uint64 minerID, const String& eventName, const String& detail);
	bool isMinerIntelligentTargetZoneAllowed(const String& zoneName);
	bool isMinerIntelligentAssignmentActive(const MinerIntelligentTargetAssignment& assignment);
	bool isMinerIntelligentAssignmentNormalTtlElapsed(const MinerIntelligentTargetAssignment& assignment, uint64 nowMs);
	uint64 getMinerIntelligentMovementArrivalTimeoutSeconds(const MinerIntelligentTargetAssignment& assignment);
	String getMinerIntelligentAssignmentTimeoutReason(MinerIntelligentTargetAssignment& assignment, uint64 nowMs, uint64& ageSeconds, uint64& timeoutSeconds, bool logNormalTtlSkip);
	int countActiveMinerIntelligentAssignments();
	bool isMinerIntelligentActivationOnCooldown(uint64 minerID, uint64 nowMs);
	void rememberMinerIntelligentActivation(uint64 minerID, uint64 nowMs);
	void recordMinerIntelligentActivationHealthEvent(const String& eventName);
	String getReachabilityDistanceBand(float distance) const;
	String getReachabilityResourceClass(const String& resourceType) const;
	String getReachabilityValidationOutcome(const MinerPathValidationSnapshot& snapshot) const;
	String getReachabilityFailureReason(const MinerPathValidationSnapshot& snapshot) const;
	void recordReachabilityCandidateGenerated(const MinerIntelligentTargetAssignment& assignment);
	void recordReachabilityAssignmentValidated(const MinerIntelligentTargetAssignment& assignment);
	void recordReachabilityCandidateRejected(const MinerIntelligentTargetAssignment& assignment, const String& reason);
	void recordReachabilityAssignmentActivated(const MinerIntelligentTargetAssignment& assignment);
	void recordReachabilitySampleCompleted(const MinerIntelligentTargetAssignment& assignment);
	void recordReachabilityStationedCoverage(const MinerIntelligentTargetAssignment& assignment);
	void recordReachabilityValidationSnapshot(const MinerPathValidationSnapshot& snapshot);
	void snapshotAndResetMinerIntelligentActivationHealth(int& attempts, int& started, int& arrivals, int& samplesCompleted, int& pathFailures, int& expired, int& cooldownSkips, int& activeCapSkips, int& zoneSkips);
	bool isSimulatedAcquisitionReadyForSample(const MinerIntelligentTargetAssignment& assignment, String& blockedReason);
	void recordSimulatedAcquisitionBlocked(const String& reason);
	uint64 getLastSimulatedAcquisitionAtMsForMiner(uint64 minerID);
	MinerRecoveryDiagnosticRow buildMinerRecoveryDiagnostic(
		const MinerIntelligentTargetAssignment& assignment, uint64 nowMs);
	JSONSerializationType serializeMinerRecoveryDiagnostic(
		const MinerRecoveryDiagnosticRow& row);
	void applyMinerRecoveryDecision(
		const MinerRecoveryDiagnosticRow& row, bool adminTriggered);

	String pickRandomTemplate(const SpawnGroup& g) const;
	bool isImperialForSpawn(const SpawnGroup& g, const String& templateName) const;

	void spawnFromConfig(const SpawnGroup& g, const ShuttleportLocation& loc, const String& templateName);

public:
	SimPlayerManager();

	// P.4.2: read-only travel config accessors used by the static density-target
	// search to skip resource pockets sitting over open water.
	bool isTravelRejectWaterTargets() const { return travelRejectWaterTargets; }
	float getTravelWaterMarginMeters() const { return travelWaterMarginMeters; }

	// P.4.4a vehicle mechanics (deploy/dismount called by scheduled tasks).
	bool deployAndMountMinerVehicle(uint64 minerID, String& resultOut);
	bool dismountAndStoreMinerVehicle(uint64 minerID, String& resultOut);
	void scheduleVehicleSelfTestTask();
	void runVehicleSelfTestTask();

	// P.4.4b mounted travel accessors (used by SimMinerController).
	bool isMountedTravelEnabled() const {
		return enabled && vehicleMechanicsEnabled && mountedTravelEnabled;
	}
	int getMountedTravelMinLegMeters() const { return mountedTravelMinLegMeters; }
	void recordMountedTravelLegStarted() { mountedTravelLegCount++; }

	~SimPlayerManager();

	// Called by ZoneServer on startup
	void initialize();
	void runMinerRecoveryTask();

	// The main logic to spawn a specific bot
	void spawnSimPlayer(const String& planet, float x, float y, const String& templateName);

	// Toggle logic
	void toggleBot(AiAgent* agent);

	static int getGameDerivedStationedSampleResultDelayMs();
	static int getGameDerivedStationedSampleIntervalMs();
	static int getGameDerivedStationedSampleIntervalSeconds();
	static int getGameDerivedMasterArtisanSurveySkill();
	static const char* getGameDerivedStationedSampleIntervalSource();
	static int getGameDerivedStationedSampleYield(float density);
	bool isIntelligentMinerWorkLoopOwnerEnabled() const;
	bool isLegacyConceptualMinerLoopAllowed() const;
	bool shouldLogLegacyMinerLoopSuppression() const;
	void recordLegacyMinerLoopSuppressed(uint64 minerID, const String& controllerState,
		bool assignmentPending, bool assignmentActive, bool assignmentStationed,
		uint64 assignmentGenerationId, const String& targetHash,
		const String& reason);
	void recordLegacyMinerLoopStarted(uint64 minerID, const String& reason);
	void recordIntelligentMinerLoopStarted(uint64 minerID, const String& reason);
	void recordStaleMinerTaskIgnored(uint64 minerID, const String& taskType,
		uint64 capturedGeneration, uint64 currentGeneration,
		const String& lifecycleState, uint64 assignmentGenerationId,
		const String& targetHash);

	uint64 recordConceptualMinerYield(const String& resourceName, int amount, uint64 sourceObjectID, bool logYield);
	void recordSimulatedAcquisitionTransactionFromController(uint64 minerID, int amount);
	uint64 recordIntelligentConceptualMinerYield(const String& conceptualLabel, int amount, uint64 minerID, bool logYield);
	bool transitionMinerIntelligentAssignmentToStationed(uint64 minerID, int yieldAmount, bool& scheduleRepeatedSample, int& delayMs, String& reason);
	void clearMinerIntelligentTargetAssignmentFromController(uint64 minerID, const String& reason);
	void clearMinerIntelligentTargetAssignmentOnSampleComplete(uint64 minerID);
	void recordMinerIntelligentTargetAssignmentLifecycleFromController(uint64 minerID, const String& eventName, const String& detail = "");
	// P.5.2: called by HiveReservationSelfTestTask (not a friend).
	void runHiveReservationSelfTestTask();
	// P.5.3: called by HiveCrafterConsumerTask (not a friend).
	void runHiveCrafterConsumerTask();
	// P.4.5b: called by MinerPlanetDispatchTask (not a friend) and by
	// SimMinerController when a dispatched miner boards its shuttle.
	void runMinerPlanetDispatchTask();
	void recordInterplanetaryTravelBoarded(uint64 minerID, const String& fromZone,
		const String& toZone, const String& starport, const String& reason);
	// P.4.5c: final-approach arrival radius, read by SimMinerController.
	float getMinerIntelligentArrivalRadiusMeters() const {
		return minerIntelligentArrivalRadiusMeters;
	}
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
