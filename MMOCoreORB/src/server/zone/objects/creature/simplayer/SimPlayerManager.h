/*
 * SimPlayerManager.h
 * Manager for handling SimPlayer population and lifecycle.
 */

#ifndef SIMPLAYERMANAGER_H_
#define SIMPLAYERMANAGER_H_

#include <atomic>
#include <memory>

#include "engine/util/Singleton.h"
#include "system/util/SynchronizedVectorMap.h"
#include "system/util/Vector.h"
#include "system/util/VectorMap.h"
#include "system/thread/Mutex.h"
#include "system/thread/atomic/AtomicLong.h"
#include "engine/util/u3d/Vector3.h"
#include "engine/util/JSONSerializationType.h"
#include "engine/lua/Lua.h"
#include "engine/util/Observer.h"

#include "SimPlayerController.h"
#include "server/zone/objects/cell/CellObject.h"
// P.8.7: the mission board owns real LairObjects. Included (rather than
// forward-declared) because the type lives in a namespace that this header
// only sees via the engine's using-declarations - a global `class LairObject;`
// would declare a DIFFERENT type and make every reference ambiguous.
#include "server/zone/objects/tangible/LairObject.h"
#include "server/zone/managers/creature/LairObserver.h"

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
class CellNavDiagRetryTask;
class CellNavDiagSpawnTask;
class HiveCrafterConsumerTask;
class SimHunterController;

enum PveMissionTerminalCityState {
	PVE_MISSION_TERMINAL_PENDING = 0,
	PVE_MISSION_TERMINAL_RESOLVED = 1,
	PVE_MISSION_TERMINAL_ABSENT = 2
};

struct SimBotIdentity {
	uint64 id = 0;
	String firstName;
	String lastName;
	String profession = "hunter";
	String homePlanet;
	String homeCity;
	int skillTier = 1;
	String createdAt;
	String lastSeenAt;
	int hunts = 0;
	int kills = 0;
	int deaths = 0;
	uint64 harvestUnits = 0;
	String assignmentSpecies;
	String assignmentResource;
	uint64 assignmentStamp = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveSpikeState {
	String phase = "SPAWNING";
	uint64 startedAtMs = 0;
	uint64 phaseStartedAtMs = 0;
	uint64 finishedAtMs = 0;
	uint64 hunterOid = 0;
	uint64 targetOid = 0;
	uint64 spawnBaselineCount = 0;
	uint64 spawnsTriggeredNearby = 0;
	int targetHealthAtEngage = 0;
	int targetActionAtEngage = 0;
	int targetMindAtEngage = 0;
	int hunterHealthAtEngage = 0;
	int hunterActionAtEngage = 0;
	int hunterMindAtEngage = 0;
	bool attackAccepted = false;
	bool aggroBack = false;
	bool damageDealtToTarget = false;
	bool damageTakenBySpike = false;
	bool targetDied = false;
	bool spikeBotDied = false;
	int destructionObserverFireCount = 0;
	bool observerParticipantVerified = false;
	bool observerRegistered = false;
	bool observerHandoffPending = false;
	bool cleanupComplete = false;
	String verdict = "PENDING";
	String failureFlag = "none";

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

// P.8.1: configuration and runtime snapshots for the solo PvE hunter. These
// are value types so controllers can copy a job without retaining pveMutex
// across world/agent locks.
struct PveHuntSpecies {
	String key;
	String planet;
	String huntGroundName;
	Vector3 huntGround;
	String lairTemplate;
	int missionDifficulty = 1;
	int lairBuildingLevel = 1;
	float lairSize = 20.f;
	String templateFilter;
	String requestedResourceType;
	String harvestKind;
	int estimatedHideUnits = 0;
	int estimatedBoneUnits = 0;
	int estimatedMeatUnits = 0;
	bool soloable = false;
	int minSkillTier = 1;
	Vector<String> eligibleHomeCities;
	bool usable = false;
	String unusableReason = "not_validated";

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveMissionTerminalLocation {
	String planet;
	String city;
	String terminalType;
	uint64 terminalOid = 0;
	Vector3 position;
	Reference<SceneObject*> terminal;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveBotMissionOffer {
	uint64 offerId = 0;
	uint64 identityId = 0;
	uint64 bodyOid = 0;
	String planet;
	uint64 terminalOid = 0;
	String terminalType;
	String lairTemplate;
	String missionBuilding;
	int difficulty = 1;
	int difficultyLevel = 1;
	int minDifficulty = 1;
	int maxDifficulty = 1;
	float size = 25.f;
	Vector3 advertisedPos;
	float bearingDeg = 0.f;
	Vector3 revealedPos;
	bool revealed = false;
	uint64 lairOid = 0;
	Vector<String> yieldResourceTypes;
	uint64 expectedYieldUnits = 0;
	uint64 issuedAtMs = 0;
	bool completed = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveMarketQualityWeight {
	String stat;
	float weight = 0.f;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveLairYieldEntry {
	String planet;
	String lairTemplate;
	String missionBuilding;
	int minDifficulty = 1;
	int maxDifficulty = 1;
	// The owning destroy-mission spawn group's ceiling. Stored so the market
	// matchmaker can evaluate the SAME strict level window that
	// selectPveBotMissionLairSpawn's tiers 1-2 use, instead of only the upper
	// bound (which is all tier 3 enforces).
	int minLevelCeiling = 20;
	float size = 25.f;
	Vector<String> resourceTypes;
	VectorMap<String, uint64> amountsByFamily;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveHuntLair {
	uint64 identityId = 0;
	uint64 bodyOid = 0;
	uint64 lairOid = 0;
	String speciesKey;
	String planet;
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;
	uint64 spawnedAtMs = 0;
	int kills = 0;
	bool alive = false;
	bool spawnInProgress = false;
	bool cleanupQueued = false;
	// Retained so the dashboard can report real wave progress
	// (LairObserver::getSpawnNumber): 1 = initial spawn, 2 = first-damage wave,
	// 3 = past-half-condition wave. The #/wilds column reads this.
	ManagedReference<LairObserver*> waveObserver;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveHuntOrder {
	uint64 identityId = 0;
	uint64 bodyOid = 0;
	uint64 issuedAtMs = 0;
	uint64 offerId = 0;
	uint64 targetOid = 0;
	uint64 lastCreditedTargetOid = 0;
	String homePlanet;
	String homeCity;
	String speciesKey;
	String requestedResourceType;
	String harvestKind;
	String demandProfileKey;
	String missionTerminalPlanet;
	String missionTerminalCity;
	String missionTerminalType;
	uint64 missionTerminalOid = 0;
	Vector3 missionTerminalPosition;
	String phase = "IDLE_HOME";
	String status = "ASSIGNED";
	int quota = 1;
	int kills = 0;
	int retreatCycles = 0;
	int missionAddsEngaged = 0;
	uint64 expectedYieldUnits = 0;
	uint64 harvestedUnits = 0;
	uint64 expectedHideUnits = 0;
	uint64 expectedBoneUnits = 0;
	uint64 expectedMeatUnits = 0;
	uint64 harvestedHideUnits = 0;
	uint64 harvestedBoneUnits = 0;
	uint64 harvestedMeatUnits = 0;
	uint64 spawnsTriggeredNearby = 0;
	String marketTargetPlanet;
	String marketTargetCity;
	String marketResourceType;
	String marketFamily;
	uint64 marketResourceSpawnOid = 0;
	float marketQualityScore = 0.f;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct PveBuffSpec {
	String name;
	uint32 crc = 0;
	float durationSeconds = 7200.f;
	int buffType = 0;
	uint8 attribute = 0;
	int modifier = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

// P.8.6 Phase 2: provider resolution snapshot consumed by the hunter
// controller. The cell reference is intentionally strong until the approach
// leg has submitted its cell-aware path.
struct PveBuffProviders {
	struct Provider {
		bool found = false;
		uint64 oid = 0;
		ManagedReference<CellObject*> cell;
		uint64 cellId = 0;
		Vector3 worldPos;
		Vector3 localPos;
	};

	Provider doctor;
	Provider musician;
	Provider dancer;
	bool pending = false;
};

// P.8.0b: zone-thread readers use an atomically published immutable copy. The
// value is zero for an active membership and a future timestamp for the
// removal grace window. The manager owns all mutations under pveMutex.
struct PvePresenceSnapshot {
	bool enabled = false;
	VectorMap<uint64, uint64> memberGraceUntilMs;
};

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
// origin + stats). Keyed hive-wide by resource-spawn object id and acquisition
// origin, not by miner.
struct MinerSpawnYieldAccumulator {
	uint64 resourceSpawnObjectId = 0;
	uint64 sessionQuantity = 0;
	// P.5.2: how much of sessionQuantity has already been written to the hive
	// lot, so each flush adds only the new delta (deposits and consumer draws
	// then compose instead of overwriting each other).
	uint64 lastFlushedQuantity = 0;
	String acquisitionOrigin = "conceptual_miner";
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

// P.5.4b: per-demand-profile crafting recipe (single-input first cut).
// C++ defaults come from createHiveCrafterRecipeDefinitions(); lua
// hiveCrafterConsumerConfig.recipes overrides per profile.
struct HiveCrafterRecipe {
	String profileKey;
	String goodKey;
	String goodName;
	String goodClassChain;
	int inputUnitsPerCraft = 25;
	int outputUnitsPerCraft = 1;
	// Parsed + surfaced now; enforced by the P.5.4d output governor.
	int finishedGoodTargetUnits = 200;

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
	friend class CellNavDiagRetryTask;
	friend class CellNavDiagSpawnTask;

	// Map of Creature ObjectID -> Controller
	SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;
	VectorMap<String, uint64> conceptualMinerTotals;
	Mutex conceptualMinerTotalsMutex;
	// P.5.1: exact-identity per-spawn hive deposits (galaxy-scoped).
	VectorMap<String, MinerSpawnYieldAccumulator> spawnYieldAccumulators;
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
	enum StarportInteriorWaypointResult {
		STARPORT_WAYPOINT_FOUND = 0,
		STARPORT_NO_INTERIOR = 1,
		STARPORT_RESOLVE_FAILED = 2
	};

	struct CellNavDiagConfig {
		bool enabled = false;
		String planet = "tatooine";
		float spawnX = 3395.f;
		float spawnY = -4775.f;
		float spawnZ = 5.f;
		float targetX = 3385.f;
		float targetY = -4811.f;
		float targetZ = 0.f;
		float doorwayX = 3383.f;
		float doorwayY = -4800.f;
		float exitX = 5000.f;
		float exitY = -5500.f;
		bool logEverySimLoopTick = true;
	};

	// Shared routed travel: one leg of a planned multi-leg journey. Departure
	// coordinates are stored separately from arrival coordinates so any
	// SimPlayer can run to the actual ticket collector without mixing the
	// collector's world and cell-local coordinate forms.
	struct SimTravelLeg {
		String destPlanet;
		String destCity;
		Vector3 arrivalPos;        // arrival pad (z re-derived at teleport)
		Vector3 departurePos;      // world-space run target / distance math
		Vector3 departureLocalPos; // cell-local path target; equals worldPos outdoors
		uint64 departureCellOid = 0; // 0 = outdoor
		bool departureIsCollector = false;
		bool interplanetary = false;
		bool finalLeg = false;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	struct PveRelocation {
	uint64 identityId = 0;
	String fromPlanet;
	String toPlanet;
	Vector<SimTravelLeg> legs;
	uint64 startedAtMs = 0;
	int legIndex = 0;
	String reason;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// P.8.7: one bounded real-buff hub round trip. The manager owns the route
	// and its lifecycle; the hunter controller owns the motion and provider
	// interaction choreography.
	struct PveBuffTrip {
	uint64 identityId = 0;
	uint64 bodyOid = 0;
	String huntPlanet;
	String huntCity;
	String hubPlanet;
	String hubCity;
	Vector<SimTravelLeg> legs;
	uint64 startedAtMs = 0;
	uint64 deadlineAtMs = 0;
	int legIndex = 0;
	bool returning = false;
	String summary;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// Compatibility name for existing PvP callers and serialized/dashboard
	// surfaces. The route representation itself is shared by all SimPlayers.
	typedef SimTravelLeg PvpTravelLeg;

	struct ShuttleportLocation {
		String planet;
		String name;
		Vector3 spawn;
		Vector3 hangout;
		// P.6.5a: exact PlanetTravelPoint name of this city's starport (from
		// scripts/managers/planet/planet_manager.lua) - resolves the routed
		// travel pad. Empty = city excluded from routed travel.
		String starportPoint;
		// P.6.5d: exact intra-planet PlanetTravelPoint name of this city's
		// shuttleport. Empty falls back to the configured spawn position.
		String shuttlePoint;
		// P.6.5d: the configured hangout is authoritative when true; otherwise
		// the resolver may derive an exterior cantina hangout.
		bool hangoutManual = false;
		// P.8.7: the city is not a general-population site. It is excluded from
		// every RANDOM/AUTOMATIC placement decision — random spawn placement,
		// PvE home-city rotation, PvP destination roulette — so adding a node
		// here cannot scatter miners or squads onto a new planet.
		//
		// It is NOT excluded from DELIBERATE PvE work destinations: fare-matrix
		// routing, mission-terminal discovery, market-dispatch destination
		// tuples, and nearest-city resolution for a hunter already standing
		// there all accept it. Sending a hunter to Dantooine for the resource
		// the market needs is the entire reason these nodes exist.
		bool routingOnly = false;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// P.6.5b: cached ticket-collector coordinates for a configured starport.
	// worldPos is used for all distance/arrival checks; localPos is passed to
	// PathFinderManager when cellOid is non-zero.
	struct PvpBoardingPoint {
		Vector3 worldPos;
		Vector3 localPos;
		uint64 cellOid = 0;
		bool resolved = false;
		bool fellBackToPad = false;
		String collectorTemplate;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// P.6.5b: full departure target returned to SimPvPController. Both
	// coordinate forms are intentional; a cell-aware path request cannot use
	// the world-space position as its local destination.
	struct PvpDepartureTarget {
		Vector3 worldPos;
		Vector3 localPos;
		uint64 cellOid = 0;
		bool isCollector = false;
		bool interplanetary = false;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// P.6.5d: resolved city-loop locations. The cache stores live travel-point
	// data and the validated exterior hangout selected from the city's cantina.
	// Entries are guarded by pvpSquadMutex; world scans happen before publish.
	struct PvpCityLocations {
		enum HangoutSource {
			HANGOUT_FALLBACK = 0,
			HANGOUT_MANUAL = 1,
			HANGOUT_CANTINA = 2
		};

		Vector3 shuttlePad;
		bool shuttlePadResolved = false;
		Vector3 hangout;
		int hangoutSource = HANGOUT_FALLBACK;
		int hangoutPathNodes = 0;
		// P.8.1: the same warmed city snapshot also carries a safe exterior
		// med-center dwell point for clone wound clearing.
		Vector3 medCenter;
		bool medCenterResolved = false;

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

	// P.6.6b: an enemy OID and its last refresh time shared by squadmates. The
	// row owns only in-memory combat contact state; it is not persisted.
	struct SimPvpSharedCombatTarget {
		uint64 enemyOid = 0;
		uint64 lastRefreshMs = 0;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// P.6.1 SimPvP squads: a persistent roster (leader + followers) that runs
	// player-mimetic starport loops and travels between cities via the proven
	// switchZone outdoor reposition. Gated by pvpConfig.enablePvpBots.
	struct SimPvpSquad {
		uint64 squadId = 0;
		bool imperial = false;
		int desiredSize = 1;
		uint64 leaderOid = 0;
		Vector<uint64> memberOids;      // live followers (leader excluded)
		Vector<SimPvpSharedCombatTarget> sharedCombatTargets;
		int pendingReplacements = 0;    // dead slots refilled at next city
		String planet;
		String city;
		Vector3 shuttlePos;
		Vector3 hangoutPos;
		bool leaderDeadPendingPromotion = false;
		bool travelTaskActive = false;
		bool reforming = false;
		uint64 reformAtMs = 0;
		uint64 formedAtMs = 0;
		uint64 lastTravelMs = 0;
		int travels = 0;
		int deaths = 0;
		// P.6.5d: rolling cohesion window, independent of city-loop age.
		int recentDeathCount = 0;
		uint64 recentDeathWindowStartMs = 0;
		bool breakOffPending = false;
		String avoidPlanet;
		String avoidCity;
		uint64 avoidExpiresAtMs = 0;
		int engagements = 0;
		// P.6.2 scouts + gank convergence: scout squads report contacts
		// instead of engaging; a patrol squad gets a pending convergence
		// destination which boardPvpSquad consumes instead of a random city.
		bool scout = false;
		uint64 lastConvergeMs = 0;      // per-squad converge cooldown anchor
		String convergePlanet;          // pending convergence destination
		String convergeCity;
		uint64 convergeExpiresAtMs = 0;
		// F.0.4.11: each bot exits an interplanetary arrival hollow through its
		// own cell route before the leader restarts the city loop.
		bool arrivalExitActive = false;
		// OIDs still expected to reach the outside. Each participant is removed
		// exactly once (on exit-complete OR death); the barrier finalizes when the
		// set empties. Idempotent + death-safe (a plain counter double-decrements
		// and rejects completion at zero when the last participant dies).
		Vector<uint64> arrivalExitPending;
		// Recovery latch: after a terminal collector-departure abandonment, suppress
		// routed re-planning until this time so the loiter->depart cycle falls back
		// to the simple city shuttle instead of reselecting the same failed leg.
		uint64 routedTravelSuppressUntilMs = 0;
		bool arrivalExitTransit = false;
		int arrivalExitDwellSeconds = 0;
		uint64 lastAnnounceMs = 0;      // P.6.3a per-squad spatial announce cooldown
		uint64 groupId = 0;             // P.6.3c GroupObject (0 = no players joined)
		// P.6.5a routed travel: remaining legs of the planned journey (index 0
		// = next to board); empty while idle at a destination city. An
		// unexpired convergence stamp drops the rest of a route (replan).
		Vector<SimTravelLeg> pendingRoute;
		// 0.2.1: route text stamped at plan time, spoken as MOVEOUT at the
		// pad (ready-to-travel) so the hangout DEPARTURE shout can't swallow
		// it via the global announce gap. Cleared when spoken or replanned.
		String pendingRouteAnnounce;
		String routeDestPlanet;
		String routeDestCity;
		int routeLegsTotal = 0;
		// P.6.6 diagnostic-only collector traversal evidence. A run covers the
		// leader plus the live members that were placed at the destination
		// collector; replacements spawned at the outdoor pad are not included.
		bool starportTraversalActive = false;
		int starportTraversalExpected = 0;
		int starportTraversalInterior = 0;
		int starportTraversalFallback = 0;
		int starportTraversalRuns = 0;
		int starportTraversalFullRuns = 0;
		uint64 starportTraversalLastRunMs = 0;
		String starportTraversalLastStatus = "none";
		Vector<uint64> starportTraversalParticipantOids;
		Vector<uint64> starportTraversalRecordedOids;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	// P.6.2: latest reported enemy contact per faction (the reporter's faction
	// is the one that converges). Guarded by pvpSquadMutex; expires by TTL.
	struct SimPvpFactionContact {
		bool valid = false;
		String planet;
		String city;
		uint64 reportedAtMs = 0;
		bool targetWasPlayer = false;
		uint64 reporterSquadId = 0;
		int reports = 0;
	};

private:
	bool enabled = false;
	CellNavDiagConfig cellNavDiagConfig;
	// Written once by the diagnostic spawn task, read on every SimPlayer's
	// findNextPosition/controller tick via the logging gate — hence atomic.
	AtomicLong cellNavDiagBotOid;
	int cellNavDiagResolveAttempts = 0;
	bool cellNavDiagRetryScheduled = false;
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
	// dryRun on so the build ships inert. NPC repositioning only - no economy/
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
	// F.0.4.11 ticket-collector travel. Default-off preserves the existing
	// outdoor pad/arrival choreography byte-for-byte.
	bool ticketCollectorTravelEnabled = false;
	float ticketCollectorBoardRadiusMeters = 8.f;
	int ticketCollectorApproachAttempts = 3;
	int ticketCollectorApproachTtlSeconds = 60;
	bool ticketCollectorFallbackToBoardFromNear = true;
	bool ticketCollectorTestForceNoCollector = false;
	// Horizontal slack on the starport-building AABB containment test so an
	// enclosed-hollow collector baked ~10m outside the collision box still
	// associates with its own starport (see resolveStarportInteriorWaypoint).
	float ticketCollectorInteriorContainmentMarginMeters = 15.f;
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
	// (master flag default off). Simulation-only object lifecycle - no economy/
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
	// P.5.3: first crafter consumer - demand-driven reserve+CONSUME that draws
	// hive stock down (the first real consumer). Simulation-only: consume
	// decrements the private hive ledger, no game state. C++ default off.
	bool hiveCrafterConsumerEnabled = false;
	bool hiveCrafterConsumerTaskScheduled = false;
	int hiveCrafterConsumerIntervalSeconds = 90;
	int hiveCrafterConsumerBatchQuantity = 25;
	int hiveCrafterConsumerMinOq = 0;
	bool hiveCrafterConsumerPreferShortage = true;
	bool hiveCrafterConsumerAllowAnyLotFallback = true;
	// P.5.4a: reserve via the profile's exact-type/family candidate list
	// (class-chain matching) instead of the single activeResource type.
	bool hiveCrafterUseFamilyMatching = false;
	// P.5.4b: consume raw -> deposit a finished_good hive lot per recipe.
	bool hiveCrafterProduceFinishedGoods = false;
	// Lua recipe overrides by profileKey (defaults live in
	// createHiveCrafterRecipeDefinitions in the .cpp). Written once at config
	// load, read by the crafter task.
	VectorMap<String, HiveCrafterRecipe> hiveCrafterRecipeOverrides;
	// Guarded runtime counters (task writes, dashboard reads).
	Mutex hiveCrafterConsumerMutex;
	uint64 hiveCrafterBatchesCompleted = 0;
	uint64 hiveCrafterUnitsConsumed = 0;
	uint64 hiveCrafterUnitsProduced = 0;
	String hiveCrafterLastProfile;
	String hiveCrafterLastReserveReason;
	bool hiveCrafterLastFallbackUsed = false;
	String hiveCrafterLastMatchedTier;
	String hiveCrafterLastMatchedQuery;
	String hiveCrafterLastGoodKey;
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

	// P.8 Phase 1: persistent PvE identity roster. The body maps are transient
	// and are always copied before any agent is locked. pveMutex is deliberately
	// independent from pvpSquadMutex; neither mutex is acquired while holding
	// the other, and neither is held while locking agents.
	VectorMap<uint64, SimBotIdentity> pveIdentities;
	VectorMap<uint64, uint64> pveIdentityBodyOids;
	VectorMap<uint64, uint64> pveBodyIdentityIds;
	VectorMap<uint64, uint64> pveRespawnDueAtMs;
	VectorMap<uint64, bool> pveDirtyIdentityIds;
	VectorMap<uint64, uint64> pvePresenceOids;
	VectorMap<uint64, uint64> pvePresenceSpawnCounts;
	Vector<PveHuntSpecies> pveHuntSpecies;
	Vector<PveMissionTerminalLocation> allMissionTerminals;
	VectorMap<String, int> missionTerminalCityState;
	VectorMap<uint64, PveHuntLair> pveHuntLairs;
	VectorMap<uint64, Vector<PveBotMissionOffer> > pveBotMissions;
	// C4 (finding #2): a terminal-visit epoch closes the window where a slow
	// offer generation commits a board for an identity whose order was concluded
	// mid-generation. pveTerminalVisitEpochSeq is GLOBALLY monotonic (never reset)
	// so a stale visit can never ABA-match a new one; the per-identity map holds
	// the epoch of that identity's current visit. All access under pveMutex.
	uint64 pveTerminalVisitEpochSeq = 0;
	VectorMap<uint64, uint64> pveTerminalVisitEpoch;
	Vector<PveLairYieldEntry> pveLairYieldIndex;
	VectorMap<uint64, PveRelocation> pveRelocations;
	VectorMap<uint64, PveBuffTrip> pveBuffTrips;
	VectorMap<uint64, uint64> pveMarketLastRelocationMs;
	VectorMap<uint64, uint64> pveMarketDwellSinceMs;
	uint64 pveNextMissionOfferId = 1;
	Vector<PveBuffSpec> pveHunterBuffs;
	Vector<PveBuffSpec> pveRealBuffFallbackSpecs;
	VectorMap<String, String> pveBuffProviderResolveStates;
	VectorMap<uint64, PveHuntOrder> pveHuntOrders;
	VectorMap<String, uint64> pveSessionHarvestByFamily;
	VectorMap<String, uint64> pveCreatureSupplyBootBaseline;
	String pveBaselineState = "pending";
	String pveBaselineLastError;
	int pveBaselineRetryCount = 0;
	uint64 pveBaselineNextRetryMs = 0;
	VectorMap<uint64, bool> pveDeathsReportedBodyOids;
	uint64 pvePresenceSpawnTotal = 0;
	uint64 pveHunterKillsTotal = 0;
	uint64 pveHunterDeathsTotal = 0;
	uint64 pveHunterAbandonsTotal = 0;
	uint64 pveHunterHarvestUnitsTotal = 0;
	uint64 pveHunterHarvestMisses = 0;
	uint64 pveHunterAnnouncementsTotal = 0;
	uint64 pveHunterLastAnnounceMs = 0;
	uint64 pveHunterLastSiteAnnounceMs = 0;
	uint64 pveMissionLairsSpawned = 0;
	uint64 pveMissionLairsCleaned = 0;
	uint64 pveCreatureKillsTotal = 0;
	uint64 pveLairsDestroyedTotal = 0;
	uint64 pveMissionsCompletedTotal = 0;
	uint64 pveMissionsAbandonedTotal = 0;
	VectorMap<String, uint64> pveAbandonReasons;
	String pveMarketDemandedFamily;
	String pveMarketWinningResourceType;
	String pveMarketWinningPlanet;
	float pveMarketWinningQualityScore = 0.f;
	uint64 pveDoctorInteractions = 0;
	uint64 pveDancerWatches = 0;
	uint64 pveMusicianListens = 0;
	uint64 pveBuffDetoursSkipped = 0;
	uint64 pveSyntheticFallbacks = 0;
	uint64 pveBuffTripsStarted = 0;
	uint64 pveBuffTripsCompleted = 0;
	uint64 pveBuffTripsFallback = 0;
	VectorMap<uint64, String> pveBuffLastSourceByBody;
	VectorMap<uint64, uint64> pveHunterLastAnnounceByIdentity;
	std::shared_ptr<const PvePresenceSnapshot> pvePresenceSnapshot;
	Mutex pveMutex;
	PveSpikeState pveSpike;
	Reference<Observer*> pveSpikeObserver;

	bool pveEnabled = false;
	bool pveHunterBotsEnabled = false;
	bool pveMissionHuntEnabled = false;
	bool pveMissionBoardEnabled = false;
	bool pveRealBuffsEnabled = false;
	bool pveRealBuffsFallbackSynthetic = true;
	bool pveRealBuffHubsEnabled = false;
	Vector<String> pveRealBuffHubKeys;
	int pveMaxBuffTripsPerHunt = 1;
	int pveBuffTripDeadlineSeconds = 1800;
	int pveRealBuffReapplySeconds = 900;
	float pveBuffProviderScanRadiusMeters = 400.f;
	String pveDoctorProviderName = "Doctor Buffer";
	String pveMusicianProviderName = "Musician Buffer";
	String pveDancerProviderName = "Dancer Buffer";
	String pveEntertainerTemplate = "entertainer";
	String pveDoctorTemplate = "smart_doctor_buffer";
	int pveDoctorInteractionTimeoutMs = 45000;
	int pveEntertainerDwellMs = 4000;
	float pveProviderApproachRangeMeters = 8.f;
	float pveMissionSpawnDistanceMeters = 200.f;
	float pveMissionTerminalScanRadiusMeters = 600.f;
	int pveMissionMaxSpawnPointTries = 32;
	int pveMissionMaxSimultaneousAdds = 3;
	int pveMissionAddsAbandonCycles = 8;
	int pveMissionTerminalDwellSeconds = 5;
	int pveMissionTerminalResolveWaitCycles = 10;
	int pveMissionLairTimeoutSeconds = 1800;
	int pveMissionMaxActiveLairs = 6;
	int pveNavmeshModeDebounceTicks = 2;
	int pveNavmeshRepathTries = 3;
	bool pveWorldPresenceEnabled = false;
	bool pveSpikeEnabled = false;
	bool pveAcquisitionLedgerEnabled = false;
	bool pveAcquisitionLedgerMinerCreatureExclusion = true;
	bool pveLocationBasedEligibility = false;
	float pveDispatchRadiusMeters = 2500.f;
	Vector<String> pveMissionBoardAcceptedTerminalTypes;
	int pveMissionBoardMaxHeldMissions = 2;
	float pveMissionBoardSameDirectionArcDegrees = 60.f;
	int pveMissionBoardBaseDistanceMeters = 1000;
	int pveMissionBoardDifficultyDistanceFactor = 0;
	int pveMissionBoardRandomDistanceMeters = 1000;
	int pveMissionBoardDifficultyRandomDistance = 0;
	float pveMissionBoardLairRevealRadiusMeters = 120.f;
	bool pveMissionBoardLairEngageAfterFieldClear = true;
	int pveMissionBoardMaxOfferAgeSeconds = 1800;
	// A wilderness point validated at generation frequently goes stale (a
	// no-build object appears) before the hunter arrives; when true, reveal
	// relocates the lair to the nearest clear point around the advertised
	// waypoint instead of abandoning the mission. See revealPveBotMissionLair.
	bool pveMissionBoardRevealRelocateEnabled = true;
	// Terminal offer-generation retry: on a partial/empty board the hunter dwells
	// this long and re-generates, up to this many attempts, before accepting a
	// single offer or (if still empty) abandoning. See beginMissionAccept.
	int pveMissionBoardOfferRetrySeconds = 25;
	int pveMissionBoardOfferMaxAttempts = 3;
	bool pveMarketDispatchEnabled = false;
	Vector<String> pveMarketDispatchFamilies;
	VectorMap<String, Vector<PveMarketQualityWeight> >
		pveMarketQualityWeights;
	float pveMarketMinQualityScore = 0.f;
	int pveMarketMaxConcurrentRelocations = 1;
	int pveMarketMinHuntersPerActivePlanet = 1;
	int pveMarketRelocationCooldownSeconds = 1800;
	int pveMarketRelocationMinDwellSeconds = 900;
	Vector<String> pveAcquisitionLedgerCreatureFamilies;
	Vector<String> pveAcquisitionLedgerCreatureClassMarkers;
	VectorMap<String, uint64> pveAcquisitionLedgerFamilyReserveTargets;
	uint64 pveAcquisitionLedgerFamilyReserveCap = 5000;
	VectorMap<String, uint64> pveAcquisitionLedgerFamilyAllocationCeilingUnits;
	VectorMap<String, float> pveAcquisitionLedgerFamilyAllocationCeilingFractions;
	float pveAcquisitionLedgerReservePressureFloor = 25.f;
	int pveAcquisitionLedgerHuntTimeEstimateSeconds = 600;
	uint64 pveAcquisitionLedgerBaselineRetryBackoffMs = 5000;
	bool pveTurfSplitEffective = false;
	bool pveTurfSplitEffectiveLatched = false;
	bool pveTurfSplitGateWarningIssued = false;
	int pveMaxHunters = 6;
	int pveSkillTier = 1;
	int pveMaintenanceIntervalSeconds = 30;
	int pveRespawnDelaySeconds = 120;
	int pveSpikeWorldWaitTimeoutSeconds = 300;
	int pveSpikeCombatTimeoutSeconds = 180;
	int pveSpikeScanRadiusMeters = 96;
	String pveHunterWeaponTemplate = "object/weapon/ranged/rifle/rifle_cdef.iff";
	int pveMaxHuntDistanceMeters = 6000;
	float pveRetreatHamPct = 30.f;
	float pveResumeHamPct = 70.f;
	int pveMaxRetreatCycles = 3;
	float pveRetreatRangeMeters = 40.f;
	int pveCloneWoundAmount = 500;
	int pveHunterActiveTickSeconds = 2;
	int pveHuntQuota = 1;
	int pveHuntTimeoutSeconds = 1800;
	float pveHunterScanRadiusMeters = 96.f;
	float pveHunterWeaponRangeMeters = 48.f;
	int pveAnnounceCooldownSeconds = 90;
	int pveAnnounceSiteGapSeconds = 300;
	bool pveHuntGroundsValidated = false;
	String pveSpikeHunterTemplate = "artisan";
	String pveSpikeTargetTemplateFilter;
	String pveSpikeSpawnArea;
	String pveSpikePlanet;
	Vector3 pveSpikePosition;
	bool pveSpikeHasExplicitPos = false;
	Vector<String> pveHunterTemplates;
	bool pveRosterLoaded = false;
	bool pveDatabaseAvailable = false;
	bool pveBootReady = false;
	// One-per-boot loud warning when hunters are enabled without a
	// current-boot spike PASS (the verdict advises the owner; it is not a
	// runtime interlock - see governPvePopulation).
	bool pveHunterEnableWarned = false;
	bool pveMaintenanceTaskScheduled = false;
	uint64 pveLastRosterFlushMs = 0;
	int pveRosterFlushIntervalSeconds = 60;
	int pveNextHomeCityIndex = 0;

	void applyPveConfig(LuaObject& pveConfig);
	void loadPveIdentityRoster();
	void mintPveIdentitiesIfNeeded();
	void flushPveIdentityRoster(bool force);
	void updatePveBodyLifecycles(uint64 nowMs);
	void governPvePopulation(uint64 nowMs);
	void runPveHunterMatchmaker(uint64 nowMs);
	void validatePveHuntGrounds();
	void attachPveHunterController(const SimBotIdentity& identity, AiAgent* body);
	AiAgent* spawnPveIdentityBody(const SimBotIdentity& identity);
	void recordPveHunterHarvest(uint64 identityId, uint64 targetOid,
		const String& harvestKind, const String& requestedResourceType);
	// Copies reservations and the monotonic session harvest tally under one
	// pveMutex acquisition so demand math observes one consistent ledger point.
	void computeReservedInboundByProfileFamily(
		VectorMap<String, uint64>& reservedInboundByProfileFamily,
		VectorMap<String, uint64>& sessionHarvestByFamily);
	bool preparePveCreatureFamilySupply(
		const Vector<String>& creatureFamilies,
		const VectorMap<String, uint64>& sessionHarvestByFamily,
		VectorMap<String, uint64>& familySupplyByFamily);
	void clearPveHunterOrderLocked(uint64 identityId, const String& status);
	void runPveSpikeIfNeeded(uint64 nowMs);
	void advancePveSpike(uint64 nowMs);
	bool spawnPveSpikeActors(uint64 nowMs);
	bool engagePveSpike(uint64 nowMs);
	bool selectPveSpikeTarget();
	void registerPveDestructionObserver(uint64 targetOid, uint64 participantOid);
	void cleanupPveSpike();
	void publishPvePresenceSnapshotLocked(uint64 nowMs);
	void expirePvePresenceMembers(uint64 nowMs);
	bool publishSimPresenceMember(uint64 oid);
	void removeSimPresenceMemberAfterWorldExit(uint64 oid, uint64 nowMs);
	void drainSimPresenceBodies(uint64 nowMs);
	void resolvePveMissionTerminals();
	void configurePveMissionTerminalCitiesLocked(
		const Vector<PveHuntSpecies>& species);
	bool choosePveHuntSpawnPoint(const PveHuntSpecies& species,
		const Vector3& missionPos, Vector3& spawnPos, Zone*& zoneOut);
	void runPveHuntLairJanitor(uint64 nowMs);
	void queuePveHuntLairCleanup(uint64 bodyOid, const String& reason);
	void finishPveHuntLairCleanup(uint64 bodyOid, uint64 lairOid,
		const String& reason);
	bool selectPveBotMissionLairSpawn(const String& planet, int hunterLevel,
		PveBotMissionOffer& offer, const String& desiredFamily = "",
		const String& desiredResourceType = "");
	// diagIdentityId is DIAGNOSTIC ONLY: it correlates missiondiag.log lines with
	// the OFFERS_BEGIN that bracket them, since offer generation can run for two
	// hunters on different threads and the lines would otherwise interleave
	// unattributably. It never affects placement.
	bool choosePveBotMissionPosition(const PveMissionTerminalLocation& terminal,
		int difficultyLevel, const String& planet, Vector3& position,
		float& bearing, uint64 diagIdentityId = 0);
	void registerPveBotMissionLairObserver(LairObject* lair, uint64 bodyOid,
		uint64 lairOid);
	void buildPveLairYieldIndex();
	JSONSerializationType getPveActivityDashboard();
	JSONSerializationType getPveSpikeDashboard();

	// Lua config loading / spawning
	void loadLuaConfig();
	void spawnConfiguredGroups();
	void scheduleConfiguredSpawnTask(int groupIndex, int spawnIndex, int delayMs);
	void runConfiguredSpawnTask(int groupIndex, int spawnIndex);

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
	void markSpawnYieldFlushed(const String& accumulatorKey,
		uint64 flushedQuantity);
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
		bool& activeSnapshotAvailable, String& snapshotError,
		bool logPersistentSummary = true);
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
	HiveCrafterRecipe getHiveCrafterRecipeForProfile(const String& profileKey);
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
	void spawnCellNavDiagnosticBot();
	bool resolveCellNavDiagnosticRoute(Vector3& worldPos, Vector3& localPos,
		ManagedReference<CellObject*>& cell);
	void scheduleCellNavDiagnosticRetry();
	void runCellNavDiagnosticRetry();

	// --- P.6.1 SimPvP squads (config, roster, travel, upkeep) ---------------
	// All C++ defaults ship OFF; lua pvpConfig enables and tunes at runtime
	// (refreshed every maintenance interval, same pattern as travelConfig).
	bool pvpEnabled = false;
	int pvpSquadsPerFaction = 1;
	int pvpSquadSize = 4;
	float pvpScanRadiusMeters = 40.f;
	// P.6.6 controller-driven combat engagement. The rollout is fail-closed;
	// Lua pvpConfig.combat supplies the live values when explicitly enabled.
	bool pvpControllerDrivenEngage = false;
	// P.6.6b squad aggro sharing is independently gated so the P.6.6 combat
	// lane remains behaviorally unchanged when the feature is off.
	bool pvpSquadAggroSharing = false;
	float pvpSquadAggroConvergeRadiusMeters = 300.f;
	int pvpSquadAggroConvergeTimeoutMillis = 60000;
	int pvpSquadAggroTargetTtlSeconds = 8;
	int pvpSquadAggroFailedTargetIgnoreSeconds = 10;
	float pvpCombatApproachRadiusMeters = 100.f;
	float pvpCombatReapproachHysteresisMeters = 8.f;
	float pvpCombatArrivalToleranceMeters = 4.f;
	int pvpCombatApproachTimeoutMillis = 15000;
	bool pvpCombatLosGateDamage = false;
	bool pvpCombatAllowInCellEngage = false;
	uint32 pvpCombatTickMillis = 0;
	float pvpCombatFallbackWeaponRangeMeters = 64.f;
	bool pvpCombatLogMovement = false;
	// Disengage distance: a bot in combat whose target has fled beyond this
	// (or died/left) drops combat instead of chasing across the map. Sits just
	// above effective ranged weapon range (~64m) so real fights aren't cut off
	// but 100m chases are. Also the phantom-combat stalemate guard's range.
	float pvpCombatLeashMeters = 72.f;
	int pvpLoiterMinSeconds = 60;
	int pvpLoiterMaxSeconds = 180;
	bool pvpAllowBotVsBotCombat = false;
	bool pvpLogStateTransitions = false;
	int pvpRespawnDelaySeconds = 120;
	int pvpMaintenanceIntervalSeconds = 30;
	int pvpShuttleWaitIntervalSeconds = 5;
	int pvpShuttleWaitMaxAttempts = 24;
	// P.6.5d break-off cohesion. C++ defaults remain disabled/conservative;
	// pvpConfig.cohesion supplies the live rollout values.
	bool pvpBreakOffEnabled = false;
	int pvpBreakOffDeaths = 2;
	int pvpBreakOffWindowSeconds = 120;
	int pvpAvoidCitySeconds = 600;
	// P.6.5e combat-progress stalemate break. C++ defaults keep the feature
	// disabled until the runtime Lua cohesion block enables it.
	int pvpStalemateBreakSeconds = 0;
	int pvpStalemateGraceSeconds = 20;
	// 0.2.1: minimum seconds between the pad MOVEOUT callout and boarding, so
	// a ship already sitting at the port can't yank the squad ~5s after the
	// route is spoken. 0 = off (pre-hotfix behavior); board-anyway unaffected.
	int pvpMinDepartureNoticeSeconds = 0;
	int pvpCorpseCleanupDelaySeconds = 60;
	bool pvpRecoveryEnabled = true;
	bool pvpRecoveryDryRun = true;
	float pvpMemberFarMeters = 64.f;
	int pvpStateTtlSeconds = 600;
	int pvpMaxRecoveryActionsPerInterval = 2;
	struct PvpTemplateChoice {
		String templateName;
		int weight = 1;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};
	Vector<PvpTemplateChoice> pvpImperialTemplates;
	Vector<PvpTemplateChoice> pvpRebelTemplates;
	bool pvpRankedJediEnabled = false;
	bool pvpFrsFromNpcJediEnabled = false;
	// P.7.4c: stock getArmorReduction early-returns for AiAgent defenders
	// BEFORE the jedi mitigation block — an NPC's Force Armor/Shield buffs
	// never mitigated anything. Flag gates the AiAgent-branch mitigation.
	bool pvpJediNpcMitigation = false;
	float pvpNpcFrsXpFactor = 1.f;
	int pvpNpcFrsXpDailyCap = 0;
	float pvpNpcFrsMinContributionPct = 0.10f;
	struct PvpNpcFrsAwardStats {
		uint64 playerID = 0;
		int dayKey = 0;
		int awardsToday = 0;
		int xpToday = 0;
		int capHitsToday = 0;
		int totalAwards = 0;
		int totalXp = 0;
		int capHitsTotal = 0;
		uint64 lastAwardMs = 0;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};
	VectorMap<uint64, PvpNpcFrsAwardStats> pvpNpcFrsAwardStats;
	uint64 pvpNpcFrsXpAwardedTotal = 0;
	uint64 pvpNpcFrsXpCapHitsTotal = 0;
	bool pvpMaintenanceTaskScheduled = false;
	uint64 nextPvpSquadId = 1;
	Vector<SimPvpSquad> pvpSquads;
	Mutex pvpSquadMutex;
	int pvpTravelsTotal = 0;
	int pvpDeathsTotal = 0;
	int pvpPlayerEngagementsTotal = 0;
	int pvpBotEngagementsTotal = 0;
	int pvpCombatConvergencesTotal = 0;
	int pvpRecoveryActionsTotal = 0;
	int pvpPromotionsTotal = 0;
	int pvpSquadReformsTotal = 0;
	int pvpBoardAnywayTotal = 0;
	// P.6.2 scouts + gank convergence (all C++ defaults off/conservative).
	bool pvpScoutsEnabled = false;
	int pvpScoutSquadsPerFaction = 1;
	int pvpScoutSquadSize = 1;
	float pvpScoutScanRadiusMeters = 64.f;
	bool pvpScoutReportOnly = true;
	int pvpScoutReportIntervalSeconds = 30;
	int pvpContactTtlSeconds = 300;
	int pvpConvergeCooldownSeconds = 600;
	SimPvpFactionContact pvpImperialContact;   // guarded by pvpSquadMutex
	SimPvpFactionContact pvpRebelContact;      // guarded by pvpSquadMutex
	VectorMap<String, uint64> pvpCityConvergeCooldowns; // key faction:planet:city
	int pvpContactsReportedTotal = 0;
	int pvpConvergencesTotal = 0;
	// P.6.3a player-facing comms: squad leaders speak (spatial chat) on key
	// events so nearby players hear the PvP happening. Gated + rate-limited.
	bool pvpCommsSpatialEnabled = false;
	int pvpCommsAnnounceCooldownSeconds = 45;  // per-squad min gap
	int pvpCommsGlobalMinGapSeconds = 4;       // any-squad min gap (anti-spam)
	uint64 pvpLastGlobalAnnounceMs = 0;        // guarded by pvpSquadMutex
	int pvpAnnouncementsTotal = 0;
	// P.6.3b faction chat rooms (GCW.Rebel / GCW.Imperial). Leaders post
	// arrivals/contacts; entry is gated to the room's faction (optionally
	// overt-only) via a flag-guarded hook in ChatManager::handleChatEnterRoomById.
	bool pvpCommsFactionRoomsEnabled = false;
	bool pvpCommsFactionRoomRequireOvert = false;
	bool pvpFactionRoomsCreated = false;       // guarded by pvpSquadMutex
	uint32 pvpRebelRoomID = 0;                 // set once at creation
	uint32 pvpImperialRoomID = 0;
    int pvpFactionRoomPostsTotal = 0;
	int pvpFactionRoomJoinsBlockedTotal = 0;
	void ensurePvpFactionRooms();
	void postPvpFactionRoom(bool imperial, const String& sender, const String& text);
	// P.6.3c player grouping: a player can join a squad's own GroupObject
	// (NPC leader + players only). Leadership is only ever transferred to the
	// squad's NEW NPC leader on promotion, or the group is disbanded on a full
	// wipe - a player can never become the group leader.
	bool pvpCommsPlayerGroupingEnabled = false;
	int pvpCommsMaxPlayersPerSquad = 5;
	float pvpCommsJoinRangeMeters = 48.f;
	// P.7.4c: deliver an NPC defender's jedi-mitigation combat spam to the
	// attacking player (otherwise invisible — NPCs have no client).
	bool pvpCommsShowNpcMitigation = false;
	int pvpGroupsFormedTotal = 0;
	int pvpPlayersJoinedTotal = 0;
	int pvpPlayersLeftTotal = 0;
	int pvpGroupsDisbandedTotal = 0;
	// squadId lookup by group id, so a group-chat message resolves its squad
	// without scanning (guarded by pvpSquadMutex).
	int findPvpSquadIndexByGroup(uint64 groupId) const;
	// Group ops (never called while holding pvpSquadMutex). Snapshot squad
	// state under the mutex, then operate on the group/agents unlocked. Players
	// LEAVE via the stock /leavegroup command (engine-correct locking); we only
	// reconcile a now-empty group's stale id in maintenance.
	bool addPlayerToSquadGroup(uint64 squadId, CreatureObject* player);
	void disbandSquadGroup(uint64 groupId, const String& reason);
	void transferSquadGroupLeadership(uint64 groupId, AiAgent* oldLeader,
		AiAgent* newLeader);
	void postSquadGroupChat(uint64 groupId, const String& sender,
		const String& text);
	void reconcilePvpSquadGroups();
	// P.6.3c: keep the group's NPC roster == the squad's live members (owner
	// wants the whole squad visible in a joined player's group). Idempotent;
	// called at join and each maintenance tick for squads that have a group.
	void syncSquadGroupMembers(uint64 squadId);

	// P.6.5-0 travel diagnostics spike (read-only, one-shot per boot).
	// Dumps the interplanetary fare matrix (the planet-connectivity truth the
	// P.6.5 router will read), resolves each configured city's nearest
	// starport, and computationally tests a street->ticket-collector path
	// into each configured starport (PathFinderManager world->cell) without
	// moving any agent. Results: SimPvpTravelSpike log lines + dashboard
	// pvpActivity.travelDiagnostics. Outcome decides P.6.5b boarding realism
	// (interior collector run vs outdoor entrance fallback).
	struct PvpTravelSpikePoint {
		String zoneName;
		String pointName;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};
	struct PvpTravelSpikeInteriorResult {
		String zoneName;
		String pointName;
		String status;            // ok|zoneMissing|pointMissing|noCollector|pathFailed|exception
		String collectorTemplate;
		bool collectorInCell = false;
		bool pathable = false;
		int pathNodes = 0;
		float collectorX = 0.f;
		float collectorY = 0.f;
		float collectorZ = 0.f;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};
	bool pvpTravelDumpGraph = false;
	bool pvpTravelTestInteriorPaths = false;
	Vector<PvpTravelSpikePoint> pvpTravelInteriorPathPoints;
	bool pvpTravelSpikeRan = false;                    // guarded by pvpSquadMutex
	Vector<String> pvpTravelSpikeFareLines;            // guarded by pvpSquadMutex
	Vector<String> pvpTravelSpikeStarportLines;        // guarded by pvpSquadMutex
	Vector<PvpTravelSpikeInteriorResult> pvpTravelSpikeInteriorResults; // guarded by pvpSquadMutex
	void runPvpTravelDiagnosticsIfNeeded();
	void runPvpTravelDiagnostics();
	bool resolvePvpBoardingPoint(const ShuttleportLocation& location,
		PvpBoardingPoint& result);
	bool resolvePvpCityLocations(const ShuttleportLocation& location,
		PvpCityLocations& result);

	// --- P.6.5a routed travel (owner-approved; ai-pvp-mimetic-travel-design.md)
	// Squads travel like players: intra-planet hops are free-form, cross-planet
	// legs require BOTH cities' starports AND a fare-matrix route (getTravelFare
	// > 0 - the exact connectivity players see). Journeys are BFS-planned over
	// the configured city pool; each leg reuses the proven switchZone boarding,
	// with brief transit stops at connection ports. C++ default OFF.
	bool pvpRoutedTravelEnabled = false;
	Vector<String> pvpTravelMainPlanets;
	int pvpTravelOffMainChancePct = 25;   // chance an off-main city is accepted
	int pvpTravelMaxLegsPerRoute = 3;
	int pvpTravelTransitDwellMinSeconds = 20;
	int pvpTravelTransitDwellMaxSeconds = 45;
	String pvpStagingRebelPlanet;         // squads form up at faction staging
	String pvpStagingRebelCity;
	String pvpStagingImperialPlanet;
	String pvpStagingImperialCity;
	int pvpRoutesPlannedTotal = 0;        // guarded by pvpSquadMutex
	int pvpRouteLegsExecutedTotal = 0;    // guarded by pvpSquadMutex
	int pvpRouteHopRoutesTotal = 0;       // routes with >1 leg
	int pvpTransitStopsTotal = 0;         // non-final legs boarded
	int pvpRouteFallbacksTotal = 0;       // routed on, plan failed → legacy pick
	int pvpBreakOffsTotal = 0;             // guarded by pvpSquadMutex
	int pvpStalemateBreaksTotal = 0;       // guarded by pvpSquadMutex
	// P.6.5b departure-port realism. Collector boarding and hot-arrival are
	// independently gated; the latter is populated by Phase 2.
	bool pvpUseCollectorBoarding = false;
	bool pvpAvoidHotArrival = false;
	bool pvpBoardOnActualShuttle = true;
	float pvpCollectorScanRadiusMeters = 175.f;
	float pvpCollectorZSanityMeters = 10.f;
	float pvpCollectorJitterMeters = 0.f;
	VectorMap<String, PvpBoardingPoint> pvpBoardingPointCache; // pvpSquadMutex
	// P.6.5d city-location cache; scans are performed outside the mutex and
	// immutable snapshots are published under it.
	bool pvpUseCantinaHangouts = false;
	float pvpCantinaScanRadiusMeters = 400.f;
	VectorMap<String, PvpCityLocations> pvpCityLocationsCache; // pvpSquadMutex
	// One-shot post-boot warmup (maintenance thread) so dashboard rows are
	// complete without the dashboard ever resolving; see resolver guard.
	bool pvpCityLocationsWarmedUp = false;
	int pvpCollectorBoardingsTotal = 0; // guarded by pvpSquadMutex
	int pvpCollectorFallbacksTotal = 0; // guarded by pvpSquadMutex
	int pvpTacticalArrivalsTotal = 0; // Phase 2; guarded by pvpSquadMutex
	// Orphan-bot sweep (log-only diagnostics for the owner's "unlinked bots
	// standing at a starport" report): live sim PvP bots that no squad roster
	// claims. Runs each maintenance tick; SimPvpOrphanBot log lines.
	int pvpOrphanBotsDetectedTotal = 0;   // guarded by pvpSquadMutex
	int pvpOrphanBotsLastSweep = 0;       // guarded by pvpSquadMutex
	void sweepPvpOrphanBots();
	// Plans a route for the squad (consumes an unexpired convergence stamp as
	// the destination) and stores it on the squad. Resolves travel points and
	// connectivity OUTSIDE the squad mutex; picks/stores under it. Returns
	// false → caller falls back to the legacy random pick.
	bool planPvpRoute(uint64 squadId, const SimPvpSquad& snapshot,
		String& summaryOut, bool& convergenceOut);
	// Squad-free fare-matrix route planner used by PvP, PvE, and future
	// SimPlayer travel callers. World/planet lookups happen without the squad
	// mutex.
	bool planSimTravelRoute(const String& fromPlanet, const String& fromCity,
		const String& toPlanet, const String& toCity,
		bool allowIntraPlanetShuttle, Vector<SimTravelLeg>& legsOut,
		String& summaryOut);
	// Returns the nearest configured city on a planet that has a routable
	// starport. Empty starport entries are intentionally excluded.
	// allowRoutingOnly has NO default on purpose: every caller must state
	// whether a routing-only city is an acceptable answer. PvE work
	// destinations say true (that is the whole reason those nodes exist);
	// anything doing random/automatic placement says false.
	int travelDiagHeartbeatSeconds = 10;

public:
	int getTravelDiagHeartbeatSeconds() const {
		return travelDiagHeartbeatSeconds;
	}

private:
	bool getNearestRoutableCity(const String& planet, const Vector3& from,
		bool allowRoutingOnly,
		ShuttleportLocation& out) const;
	// Pops the next planned leg. A fresh unexpired convergence stamp drops the
	// remaining route instead (returns false → caller replans to the contact).
	bool popNextPvpRouteLeg(uint64 squadId, SimTravelLeg& legOut, int& remainingOut);
	int findShuttleportIndex(const String& planet, const String& city) const;

	void applyPvpConfig(LuaObject& pvpConfig);
	void refreshPvpConfig();
	void spawnPvpSquad(bool imperial, bool scout);
	// Destroys whatever bots remain of a squad row being dropped (reform of a
	// leader-missing squad can hold LIVE members) — never leak orphans. Same
	// destroy choreography as despawnPvpSquads.
	void despawnPvpSquadRemnants(const SimPvpSquad& squad, const String& reason);
	// P.6.2: consume unexpired faction contacts - pick an eligible patrol
	// squad and send it to the contact's city (per-squad + per-city cooldowns).
	void dispatchPvpConvergence(uint64 nowMs);
	// Caller must hold pvpSquadMutex. Registers/refreshes the reporter
	// faction's contact from the squad's current city.
	void notePvpContactLocked(const SimPvpSquad& squad, bool targetWasPlayer);
	AiAgent* spawnPvpBotAgent(Zone* zone, const Vector3& position,
		const String& templateName, bool imperial, bool leader,
		AiAgent* leaderAgent, float formationOffsetX, float formationOffsetY);
	void boardPvpSquad(uint64 squadId);
	void checkPvpBreakOff(uint64 squadId, bool wasLeader, bool countedDeath);
	void schedulePvpBotCleanup(uint64 oid, int delaySeconds);
	void despawnPvpSquads(const String& reason);
	String pickPvpTemplate(bool imperial) const;
	void loadPvpTemplateChoices(LuaObject& templateList,
		Vector<PvpTemplateChoice>& choices);
	// Caller must hold pvpSquadMutex. Returns -1 when the squad is gone.
	int findPvpSquadIndex(uint64 squadId) const;
	// Caller must hold pvpSquadMutex. A city is hot when an opposing squad is
	// stationed there or this faction has an unexpired enemy contact there.
	bool isPvpCityHotLocked(const String& planet, const String& city,
		bool imperial, uint64 squadId) const;

public:
	SimPlayerManager();

	// F.0.4.11: shared travel resolvers. World scans happen without an agent
	// lock; the resolver takes only short object snapshots and validates the
	// cell target with the portal-aware path finder.
	StarportInteriorWaypointResult resolveStarportInteriorWaypoint(
		Zone* zone, const Vector3& starportNearWorld,
		const Vector3& pathStartWorld, Vector3& outWorld,
		Vector3& outLocal, ManagedReference<CellObject*>& outCell);
	bool resolveNearestTicketCollector(Zone* zone, const Vector3& nearWorld,
		Vector3& outWorld, Vector3& outLocal,
		ManagedReference<CellObject*>& outCell, uint64& outOid);

	bool isTicketCollectorTravelEnabled() const {
		return enabled && ticketCollectorTravelEnabled;
	}
	float getTicketCollectorBoardRadiusMeters() const {
		return ticketCollectorBoardRadiusMeters;
	}
	int getTicketCollectorApproachAttempts() const {
		return ticketCollectorApproachAttempts;
	}
	int getTicketCollectorApproachTtlSeconds() const {
		return ticketCollectorApproachTtlSeconds;
	}
	bool isTicketCollectorFallbackToBoardFromNear() const {
		return ticketCollectorFallbackToBoardFromNear;
	}
	bool isTicketCollectorTestForceNoCollector() const {
		return ticketCollectorTestForceNoCollector;
	}

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
	bool isCellNavDiagBot(uint64 oid) const {
		return oid != 0 && oid == cellNavDiagBotOid.get();
	}
	bool getCellNavDiagLogEveryTick() const {
		return cellNavDiagConfig.logEverySimLoopTick;
	}

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
	// P.8.0b: this predicate is used by hot zone/AI paths. It returns true for
	// real players first, then consults only the published presence snapshot for
	// opted-in PlayerBot bodies; it never acquires pveMutex.
	bool isPlayerOrSimPresenceCreature(CreatureObject* creature) const;
	bool isSimPresenceCreature(CreatureObject* creature) const;
	void recordSimPresenceSpawn(uint64 oid);

	// P.8 Phase 1: the foundation task owns every roster SQL operation. The
	// destruction handoff is intentionally public because queued lambdas are not
	// friends; it only updates in-memory spike state and never touches SQL.
	void schedulePveFoundationMaintenanceTask();
	void runPveFoundationMaintenanceTask();
	void handlePveDestructionHandoff(uint64 targetOid, uint64 participantOid,
		bool participantVerified);
	void handlePveHunterDestructionHandoff(uint64 hunterOid, uint64 targetOid,
		bool participantVerified);
	void onPveHunterDied(uint64 identityId, uint64 bodyOid);
	void recordPveHunterPhase(uint64 identityId, uint64 bodyOid,
		const String& phase, uint64 targetOid = 0);
	void recordPveHunterKill(uint64 identityId, uint64 bodyOid,
		uint64 targetOid, const String& harvestKind,
		const String& requestedResourceType, bool participantVerified);
	void handlePveHunterTelemetryDestructionHandoff(uint64 identityId,
		uint64 bodyOid, uint64 creatureOid, bool participantVerified);
	void recordPveHunterAbandoned(uint64 identityId, uint64 bodyOid,
		const String& reason);
	void recordPveHunterCompleted(uint64 identityId, uint64 bodyOid);
	bool getNearestMissionTerminal(const String& planet, const String& city,
		const Vector3& fromPosition, PveMissionTerminalLocation& result,
		int& cityState);
	bool getNearestGeneralMissionTerminal(const String& planet,
		const Vector3& fromPosition, PveMissionTerminalLocation& result,
		int& cityState);
	bool resolvePveHunterDispatchLocation(uint64 bodyOid,
		float dispatchRadiusMeters, String& currentPlanet,
		ShuttleportLocation& city, PveMissionTerminalLocation& terminal);
	bool spawnPveHuntLair(uint64 bodyOid, const PveHuntSpecies& species,
		const Vector3& missionPos);
	bool generatePveBotMissionOffers(uint64 identityId, uint64 bodyOid,
		const PveMissionTerminalLocation& terminal, int hunterLevel,
		Vector<PveBotMissionOffer>& offers, uint64 visitEpoch);
	// Opens a fresh terminal visit: verifies an active order for identity/body,
	// drops any stale board, assigns+returns a new globally-monotonic visit
	// epoch (0 = no active order, caller must not proceed). The controller passes
	// this epoch back into generatePveBotMissionOffers so a commit that finishes
	// after the order concluded is discarded. All work under pveMutex.
	uint64 openTerminalVisit(uint64 identityId, uint64 bodyOid);
	bool getPveBotMissionOffer(uint64 identityId, uint64 offerId,
		PveBotMissionOffer& offer);
	// Tri-state so a transient capacity/consistency miss defers the reveal
	// (retry next tick) instead of abandoning a perfectly good mission.
	enum PveLairRevealResult {
		PVE_LAIR_REVEAL_OK = 0,
		PVE_LAIR_REVEAL_DEFER = 1,
		PVE_LAIR_REVEAL_FAILED = 2
	};
	PveLairRevealResult revealPveBotMissionLair(uint64 bodyOid, uint64 offerId);
	void onPveBotMissionLairDestroyed(uint64 bodyOid, uint64 lairOid);
	bool planPveHunterRelocation(uint64 identityId, uint64 bodyOid,
		const String& targetPlanet, const String& targetCity,
		const String& reason, PveRelocation& relocation);
	bool getPveHunterRelocation(uint64 identityId, PveRelocation& relocation);
	void advancePveHunterRelocation(uint64 identityId);
	void finishPveHunterRelocation(uint64 identityId, bool success,
		const String& actualPlanet);
	bool isPveMarketDispatchEnabled() const {
		return pveMarketDispatchEnabled;
	}
	bool isPveBuffHubsEnabled() const { return pveRealBuffHubsEnabled; }
	int getPveMaxBuffTripsPerHunt() const { return pveMaxBuffTripsPerHunt; }
	int getPveBuffTripDeadlineSeconds() const {
		return pveBuffTripDeadlineSeconds;
	}
	bool getPveHunterCurrentRoutableCity(uint64 bodyOid,
		String& currentPlanet, ShuttleportLocation& city);
	bool planPveHunterBuffTrip(uint64 identityId, uint64 bodyOid,
		bool needDoctor, bool needEntertainer, PveBuffTrip& trip);
	bool getPveHunterBuffTrip(uint64 identityId, PveBuffTrip& trip);
	void advancePveHunterBuffTrip(uint64 identityId);
	bool beginPveHunterBuffReturn(uint64 identityId);
	void finishPveHunterBuffTrip(uint64 identityId, bool success);
	bool getPveHuntLair(uint64 bodyOid, PveHuntLair& result);
	void requestPveHuntLairCleanup(uint64 bodyOid, const String& reason);
	void recordPveHunterMissionTerminal(uint64 identityId, uint64 bodyOid,
		const String& planet, const String& city, const Vector3& position,
		const String& terminalType = "", uint64 terminalOid = 0);
	void recordPveHunterMissionAdds(uint64 identityId, uint64 bodyOid,
		int adds);
	void recordPveDoctorInteraction();
	void recordPveDancerWatch();
	void recordPveMusicianListen();
	void recordPveBuffDetourSkipped();
	void recordPveSyntheticFallback(uint64 bodyOid);
	void recordPveBuffSource(uint64 bodyOid, const String& source);
	bool isPveMissionHuntEnabled() const { return pveMissionHuntEnabled; }
	int getPveBotHunterLevelForBody(uint64 bodyOid) const;
	int getPveBotHunterLevel(AiAgent* hunter) const;
	bool isPveMissionBoardEnabled() const { return pveMissionBoardEnabled; }
	int getPveMissionBoardMaxHeldMissions() const {
		return pveMissionBoardMaxHeldMissions;
	}
	int getPveMissionBoardOfferRetrySeconds() const {
		return pveMissionBoardOfferRetrySeconds;
	}
	int getPveMissionBoardOfferMaxAttempts() const {
		return pveMissionBoardOfferMaxAttempts;
	}
	float getPveMissionBoardLairRevealRadiusMeters() const {
		return pveMissionBoardLairRevealRadiusMeters;
	}
	bool getPveMissionBoardLairEngageAfterFieldClear() const {
		return pveMissionBoardLairEngageAfterFieldClear;
	}
	int getPveMissionBoardMaxOfferAgeSeconds() const {
		return pveMissionBoardMaxOfferAgeSeconds;
	}
	bool isPveRealBuffsEnabled() const { return pveRealBuffsEnabled; }
	bool isPveRealBuffsFallbackSyntheticEnabled() const {
		return pveRealBuffsFallbackSynthetic;
	}
	bool getPveHunterSpecies(const String& key, PveHuntSpecies& species);
	bool getPveHunterOrder(uint64 identityId, PveHuntOrder& order);
	void getPveHunterBuffs(Vector<PveBuffSpec>& buffs);
	void getPveRealBuffFallbackSpecs(Vector<PveBuffSpec>& buffs);
	void getPveTrackedBuffCrcs(Vector<uint32>& crcs) const;
	int getPveRealBuffReapplyThresholdSeconds() const {
		return pveRealBuffReapplySeconds;
	}
	float getPveBuffProviderScanRadiusMeters() const {
		return pveBuffProviderScanRadiusMeters;
	}
	String getPveDoctorProviderName() const { return pveDoctorProviderName; }
	String getPveMusicianProviderName() const { return pveMusicianProviderName; }
	String getPveDancerProviderName() const { return pveDancerProviderName; }
	String getPveEntertainerTemplate() const { return pveEntertainerTemplate; }
	String getPveDoctorTemplate() const { return pveDoctorTemplate; }
	int getPveDoctorInteractionTimeoutMs() const {
		return pveDoctorInteractionTimeoutMs;
	}
	int getPveEntertainerDwellMs() const { return pveEntertainerDwellMs; }
	float getPveProviderApproachRangeMeters() const {
		return pveProviderApproachRangeMeters;
	}
	float getPveHunterRetreatHamPct() const { return pveRetreatHamPct; }
	float getPveHunterResumeHamPct() const { return pveResumeHamPct; }
	int getPveHunterMaxRetreatCycles() const { return pveMaxRetreatCycles; }
	float getPveHunterRetreatRangeMeters() const { return pveRetreatRangeMeters; }
	int getPveHunterActiveTickSeconds() const { return pveHunterActiveTickSeconds; }
	int getPveHunterTimeoutSeconds() const { return pveHuntTimeoutSeconds; }
	float getPveHunterScanRadiusMeters() const { return pveHunterScanRadiusMeters; }
	float getPveHunterWeaponRangeMeters() const { return pveHunterWeaponRangeMeters; }
	int getPveMissionTerminalDwellSeconds() const {
		return pveMissionTerminalDwellSeconds;
	}
	int getPveMissionTerminalResolveWaitCycles() const {
		return pveMissionTerminalResolveWaitCycles;
	}
	int getPveMissionMaxSimultaneousAdds() const {
		return pveMissionMaxSimultaneousAdds;
	}
	int getPveMissionAddsAbandonCycles() const {
		return pveMissionAddsAbandonCycles;
	}
	int getPveNavmeshModeDebounceTicks() const {
		return pveNavmeshModeDebounceTicks;
	}
	int getPveNavmeshRepathTries() const {
		return pveNavmeshRepathTries;
	}
	int getPveHunterAnnounceCooldownSeconds() const { return pveAnnounceCooldownSeconds; }
	bool getPveHomeLocations(const String& planet, const String& city,
		Vector3& cantina, Vector3& medCenter, Vector3& home);
	bool resolvePveBuffProviders(const String& planet, const String& city,
		PveBuffProviders& out);
	void announcePveHunterEvent(uint64 bodyOid, const String& site,
		const String& detail = "");

	// --- P.6.1 SimPvP squads: task- and controller-called entry points.
	// (Scheduled tasks are not friends; these must stay public.)
	void schedulePvpMaintenanceTask();
	void runPvpMaintenanceTask();
	void runPvpShuttleWaitTask(uint64 squadId, int attempts);
	void runPvpBotCleanupTask(uint64 oid);
	// Called by SimPvPController when the leader is posted at the shuttleport.
	void onPvpSquadReadyToTravel(uint64 squadId);
	// Called by each leader/member after its own arrival hollow exit completes.
	void onPvpArrivalExitComplete(uint64 squadId, uint64 oid);
	void abandonPvpRoutedTravel(uint64 squadId);
	bool isPvpArrivalExitComplete(uint64 squadId);
	// Called by SimPvPController before entering PVP_TO_SHUTTLE. Plans the
	// route early enough for the controller to run to the correct departure
	// point, and returns both coordinate forms for that run.
	bool onPvpSquadDepartureIntent(uint64 squadId,
		PvpDepartureTarget& target);
	// Called (once per life) by SimPvpBotController when a bot dies.
	void onPvpBotDied(uint64 squadId, uint64 oid);
	void beginPvpStarportTraversalDiagnostic(uint64 squadId,
		const Vector<uint64>& participantOids);
	void recordPvpStarportTraversalParticipant(uint64 squadId, uint64 oid,
		const String& result);
	void recordPvpEngagement(uint64 squadId, bool targetWasPlayer);
	// P.6.6b: publish/query the squad's short-lived shared combat contacts.
	// Query liveness is resolved after the squad mutex is released; callers
	// must never hold pvpSquadMutex while locking an agent.
	void recordPvpSquadCombatTarget(uint64 squadId, uint64 enemyOid);
	void getPvpSquadSharedCombatTargets(uint64 squadId, uint64 excludeOid,
		Vector<uint64>& outLiveOids);
	void recordPvpSquadConvergence(uint64 squadId);
	void recordPvpStalemateBreak(uint64 squadId, uint64 oid,
		uint64 defenderOid, uint64 idleMs);
	// P.6.2: called (throttled) by a report-only scout's scan on contact.
	void reportPvpContact(uint64 squadId, bool targetWasPlayer);

	// P.6.3a: squad-leader spatial announcements. The controller fires an event
	// at a phase transition; the manager (config- and cooldown-gated) resolves
	// the leader agent and broadcasts a faction-flavored line to nearby players.
	enum PvpAnnounceEvent {
		PVP_ANNOUNCE_ARRIVAL = 0,   // posted up at the starport
		PVP_ANNOUNCE_DEPARTURE,     // area clear, moving on
		PVP_ANNOUNCE_CONTACT,       // enemy spotted / engaging
		PVP_ANNOUNCE_CONVERGE,      // breaking off to reinforce a fight
		PVP_ANNOUNCE_MOVEOUT,       // P.6.5a: boarding with a planned route
		PVP_ANNOUNCE_RETREAT        // P.6.5d: breaking off after squad deaths
	};
	// detail: appended to the spatial line and used in the room/group post -
	// P.6.5a passes the human-readable route ("Route: kor vella, then coronet
	// (corellia)") so players know where the squad is heading. MOVEOUT bypasses
	// the per-squad announce cooldown (route info must not be swallowed by the
	// DEPARTURE callout seconds earlier); RETREAT bypasses both gaps.
	void announcePvpEvent(uint64 squadId, int eventType,
		const String& detail = "");

	// P.6.3b: called by the flag-gated hook in ChatManager::handleChatEnterRoom
	// ById. isPvpFactionRoom returns false unless faction rooms + gating are
	// enabled AND the id is one of ours, so the hook is inert when off.
	bool isPvpFactionRoom(uint32 roomID) const;
	bool isPvpFactionRoomJoinAllowed(CreatureObject* player, uint32 roomID) const;
	void recordPvpFactionRoomJoinBlocked();

	// P.6.3c: called by the flag-gated hooks in ChatManagerImplementation.
	// onPlayerSpatialChat parses join keywords ("join pvp group", "join group
	// with <name>"). onPvpGroupChat parses in-group commands (status/where/
	// leave). isPvpSquadGroup lets the group-chat hook skip non-squad groups.
	void onPlayerSpatialChat(CreatureObject* player, const UnicodeString& message);
	void onPvpGroupChat(CreatureObject* player, uint64 groupId,
		const UnicodeString& message);
	bool isPvpSquadGroup(uint64 groupId) const;
	// P.6.3c: gate for the joinGroup core patch - true only when grouping is
	// enabled AND oid is a current squad leader NPC.
	bool isPvpSquadLeaderNpc(uint64 oid) const;
	// P.6.3c: called by the joinGroup core patch after a player ACCEPTS an
	// invite from a squad-leader NPC (records the group + syncs the squad in).
	void onPlayerJoinedSquadGroup(uint64 leaderOid, CreatureObject* player);

	// Read-only pvpConfig accessors for the controllers (runtime-refreshed).
	float getPvpScanRadiusMeters() const { return pvpScanRadiusMeters; }
	bool isPvpControllerDrivenEngageEnabled() const { return pvpControllerDrivenEngage; }
	bool isPvpSquadAggroSharingEnabled() const { return pvpSquadAggroSharing; }
	float getPvpSquadAggroConvergeRadiusMeters() const {
		return pvpSquadAggroConvergeRadiusMeters;
	}
	int getPvpSquadAggroConvergeTimeoutMillis() const {
		return pvpSquadAggroConvergeTimeoutMillis;
	}
	int getPvpSquadAggroTargetTtlSeconds() const {
		return pvpSquadAggroTargetTtlSeconds;
	}
	int getPvpSquadAggroFailedTargetIgnoreSeconds() const {
		return pvpSquadAggroFailedTargetIgnoreSeconds;
	}
	float getPvpCombatApproachRadiusMeters() const { return pvpCombatApproachRadiusMeters; }
	float getPvpCombatReapproachHysteresisMeters() const { return pvpCombatReapproachHysteresisMeters; }
	float getPvpCombatArrivalToleranceMeters() const { return pvpCombatArrivalToleranceMeters; }
	int getPvpCombatApproachTimeoutMillis() const { return pvpCombatApproachTimeoutMillis; }
	bool isPvpCombatLosGateDamageEnabled() const { return pvpCombatLosGateDamage; }
	bool isPvpCombatAllowInCellEngageEnabled() const { return pvpCombatAllowInCellEngage; }
	uint32 getPvpCombatTickMillis() const { return pvpCombatTickMillis; }
	float getPvpCombatFallbackWeaponRangeMeters() const { return pvpCombatFallbackWeaponRangeMeters; }
	bool isPvpCombatLogMovementEnabled() const { return pvpCombatLogMovement; }
	bool isPvpCombatMovementDiagnosticsEnabled() const {
		return pvpControllerDrivenEngage &&
			(pvpCombatLogMovement || pvpLogStateTransitions);
	}
	float getPvpCombatLeashMeters() const { return pvpCombatLeashMeters; }
	int getPvpStalemateBreakSeconds() const { return pvpStalemateBreakSeconds; }
	int getPvpStalemateGraceSeconds() const { return pvpStalemateGraceSeconds; }
	bool isPvpBotVsBotCombatEnabled() const { return pvpAllowBotVsBotCombat; }
	bool isPvpRankedJediEnabled() const { return pvpRankedJediEnabled; }
	bool isPvpNpcFrsXpEnabled() const { return pvpFrsFromNpcJediEnabled; }
	float getPvpNpcFrsXpFactor() const { return pvpNpcFrsXpFactor; }
	float getPvpNpcFrsMinContributionPct() const { return pvpNpcFrsMinContributionPct; }
	int recordPvpNpcFrsXpAward(uint64 playerID, int requestedXp);
	void recordPvpNpcFrsXpCapHit(uint64 playerID);
	int getPvpLoiterMinSeconds() const { return pvpLoiterMinSeconds; }
	int getPvpLoiterMaxSeconds() const { return pvpLoiterMaxSeconds; }
	bool isPvpLogStateTransitionsEnabled() const { return pvpLogStateTransitions; }
	// P.7.4c: an NPC defender has no client, so its Force Armor/Shield/Absorb
	// mitigation spam was silently dropped — when enabled, the attacking
	// PLAYER receives it instead (flag-gated hook in
	// CombatManager::sendMitigationCombatSpam).
	bool isPvpNpcMitigationSpamEnabled() const { return pvpCommsShowNpcMitigation; }
	// P.7.4c: gates the AiAgent-defender jedi mitigation in
	// CombatManager::getArmorReduction (stock code early-returned before it).
	bool isPvpJediNpcMitigationEnabled() const { return pvpJediNpcMitigation; }
	float getPvpScoutScanRadiusMeters() const { return pvpScoutScanRadiusMeters; }
	bool isPvpScoutReportOnly() const { return pvpScoutReportOnly; }
	int getPvpScoutReportIntervalSeconds() const { return pvpScoutReportIntervalSeconds; }
};

#endif /* SIMPLAYERMANAGER_H_ */
