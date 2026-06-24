/*
 * SimPlayerManager.cpp
 * Fixed Attackable Flags
 */

#include "SimPlayerManager.h"
#include "SimPvPController.h"
#include "server/zone/ZoneServer.h"
#include "server/ServerCore.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/managers/creature/CreatureTemplateManager.h"
#include "server/zone/managers/name/NameManager.h"
#include "server/zone/objects/creature/ai/CreatureTemplate.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"
#include "templates/params/creature/ObjectFlag.h"
#include "server/zone/managers/planet/PlanetManager.h"
#include "server/zone/managers/planet/PlanetTravelPoint.h"
#include "server/zone/managers/resource/ResourceManager.h"
#include "server/zone/managers/resource/resourcespawner/ResourceSpawner.h"
#include "server/zone/managers/resource/resourcespawner/resourcemap/ResourceMap.h"
#include "server/zone/managers/aieconomy/AiEconomyManager.h"
#include "server/zone/managers/auction/AuctionManager.h"
#include "server/zone/managers/auction/AuctionsMap.h"
#include "server/zone/managers/auction/TerminalListVector.h"
#include "server/zone/objects/auction/AuctionItem.h"
#include "server/zone/objects/resource/ResourceContainer.h"
#include "server/zone/objects/resource/ResourceSpawn.h"
#include "server/zone/objects/pathfinding/NavArea.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "system/thread/ReadLocker.h"

#define DEBUG_SIMPLAYER

class SimMinerSummaryTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMinerSummaryTask();
    }
};

class ResourceIntelligenceTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runResourceIntelligenceTask();
    }
};

class MinerTargetRecommendationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMinerTargetRecommendationTask();
    }
};

class MinerTargetSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMinerTargetSimulationTask();
    }
};

class MinerDensityTargetSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMinerDensityTargetSimulationTask();
    }
};

class MinerPathValidationSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMinerPathValidationSimulationTask();
    }
};

class DemandProfileSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runDemandProfileSimulationTask();
    }
};

class DemandStateSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runDemandStateSimulationTask();
    }
};

class MarketSupplyObservationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMarketSupplyObservationTask();
    }
};

class StockpileSnapshotSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runStockpileSnapshotSimulationTask();
    }
};

class DemandWeightedMinerPlanSimulationTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runDemandWeightedMinerPlanSimulationTask();
    }
};

class AiEconomyConceptualTotalsPersistenceTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runAiEconomyPersistenceTask();
    }
};

class MinerIntelligentTargetingTask : public Task {
public:
    void run() override {
        SimPlayerManager::instance()->runMinerIntelligentTargetingTask();
    }
};

class MinerPathValidationTask : public Task {
    uint64 minerID;
    uint64 assignmentGenerationId;
    String targetHash;
    String zoneName;
    String profileKey;
    String resourceName;
    String resourceType;
    String targetSource;
    Vector3 startPosition;
    Vector3 targetPosition;
    float density;
    float directDistance;
    int maxPathDistance;
    int maxPathNodes;
    bool acceptedDensityTarget;
    bool minerInNavmesh;
    ManagedReference<Zone*> zone;

public:
    MinerPathValidationTask(
            uint64 minerID,
            uint64 assignmentGenerationId,
            const String& targetHash,
            const String& zoneName,
            const String& profileKey,
            const String& resourceName,
            const String& resourceType,
            const String& targetSource,
            const Vector3& startPosition,
            const Vector3& targetPosition,
            float density,
            float directDistance,
            int maxPathDistance,
            int maxPathNodes,
            bool acceptedDensityTarget,
            bool minerInNavmesh,
            Zone* zone)
        : minerID(minerID),
          assignmentGenerationId(assignmentGenerationId),
          targetHash(targetHash),
          zoneName(zoneName),
          profileKey(profileKey),
          resourceName(resourceName),
          resourceType(resourceType),
          targetSource(targetSource),
          startPosition(startPosition),
          targetPosition(targetPosition),
          density(density),
          directDistance(directDistance),
          maxPathDistance(maxPathDistance),
          maxPathNodes(maxPathNodes),
          acceptedDensityTarget(acceptedDensityTarget),
          minerInNavmesh(minerInNavmesh),
          zone(zone) {
    }

    void run() override;
};

static String buildMinerAssignmentTargetHash(
        const String& targetSource,
        const String& profileKey,
        const String& resourceName,
        const String& resourceType,
        const String& zoneName,
        float targetX,
        float targetY,
        float targetZ) {
    int bucketX = static_cast<int>(targetX * 10.f);
    int bucketY = static_cast<int>(targetY * 10.f);
    int bucketZ = static_cast<int>(targetZ * 10.f);

    return targetSource + "|" + profileKey + "|" + resourceName + "|" +
        resourceType + "|" + zoneName + "|" + String::valueOf(bucketX) +
        "|" + String::valueOf(bucketY) + "|" + String::valueOf(bucketZ);
}

static String buildMinerAssignmentTargetHash(
        const MinerIntelligentTargetAssignment& assignment) {
    return buildMinerAssignmentTargetHash(
        assignment.targetSource,
        assignment.selectedProfileKey,
        assignment.targetResourceName,
        assignment.targetResourceType,
        assignment.targetZoneName,
        assignment.targetX,
        assignment.targetY,
        assignment.targetZ);
}

static String buildMinerAssignmentTargetHash(
        const MinerPathValidationSnapshot& snapshot) {
    return buildMinerAssignmentTargetHash(
        snapshot.targetSource,
        snapshot.profileKey,
        snapshot.resourceName,
        snapshot.resourceType,
        snapshot.zoneName,
        snapshot.targetX,
        snapshot.targetY,
        snapshot.targetZ);
}

static bool isMinerAssignmentLifecycleActiveStatus(const String& status) {
    return status == "queued" ||
        status == "activation_started" ||
        status == "sample_started" ||
        status == "stationed" ||
        status == "sample_complete";
}

static bool minerValidationSnapshotMatchesAssignment(
        const MinerIntelligentTargetAssignment& assignment,
        const MinerPathValidationSnapshot& snapshot) {
    String assignmentHash = assignment.targetHash.isEmpty() ?
        buildMinerAssignmentTargetHash(assignment) : assignment.targetHash;
    String snapshotHash = snapshot.targetHash.isEmpty() ?
        buildMinerAssignmentTargetHash(snapshot) : snapshot.targetHash;

    if (assignment.assignmentGenerationId > 0 &&
            snapshot.assignmentGenerationId > 0 &&
            assignment.assignmentGenerationId != snapshot.assignmentGenerationId)
        return false;

    if (!assignmentHash.isEmpty() && !snapshotHash.isEmpty())
        return assignmentHash == snapshotHash;

    float dx = assignment.targetX - snapshot.targetX;
    float dy = assignment.targetY - snapshot.targetY;
    float dz = assignment.targetZ - snapshot.targetZ;

    return assignment.targetSource == snapshot.targetSource &&
        assignment.selectedProfileKey == snapshot.profileKey &&
        assignment.targetResourceName == snapshot.resourceName &&
        assignment.targetResourceType == snapshot.resourceType &&
        assignment.targetZoneName == snapshot.zoneName &&
        (dx * dx + dy * dy + dz * dz) <= 4.f;
}

struct ResourceIntelligenceEntry {
    uint64 objectID = 0;
    String name;
    String type;
    String classChain;
    String zones;
    bool inShift = false;
    unsigned long despawned = 0;
    int surveyToolType = 0;
    int oq = 0;
    int cd = 0;
    int dr = 0;
    int hr = 0;
    int fl = 0;
    int ma = 0;
    int pe = 0;
    int sr = 0;
    int ut = 0;
    int cr = 0;
    int genericScore = 0;
    int weaponsmithScore = 0;
    int armorsmithScore = 0;
    int chefScore = 0;
    int architectScore = 0;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct ResourceScoringProfile {
    String key;
    String category;
    String description;
    Vector<String> requiredTypes;
    Vector<String> preferredStats;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DemandProfileDefinition {
    String key;
    String category;
    Vector<String> activePhases;
    Vector<String> exactTypes;
    Vector<String> premiumFamilies;
    Vector<String> bulkFamilies;
    Vector<String> preferredStats;
    Vector<int> statWeights;
    bool serverBestSensitive = false;
    bool stockpileSensitive = false;
    bool scoreBulkQuality = true;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DemandProfileMatch {
    bool eligible = false;
    bool exact = false;
    bool premium = false;
    bool bulk = false;
    bool qualityScored = false;
    String matchedType;
    int baseScore = 0;
    int demandScore = 0;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DemandStateSimulationResult {
    String profileKey;
    String state;
    String supplyConfidence;
    String supplyLabels;
    uint64 desiredReserve = 0;
    uint64 aiConceptualSupply = 0;
    uint64 marketObservedSupply = 0;
    uint64 persistentStockpileSupply = 0;
    uint64 persistentStockpileQuantityMatched = 0;
    uint64 totalKnownSupply = 0;
    uint64 shortageUnits = 0;
    uint64 surplusUnits = 0;
    int marketListingsMatched = 0;
    int persistentStockpileLotsMatched = 0;
    float marketCheapestPricePerUnit = -1.f;
    float marketMedianPricePerUnit = -1.f;
    float reserveRatio = 0.f;
    float shortagePressure = 0.f;
    float opportunityPressure = 0.f;
    float pressureScore = 0.f;
    bool hasActiveOpportunity = false;
    bool activeProfileAvailableForPhase = true;
    String marketSupplyConfidence = "none";
    String persistentStockpileConfidence = "none";
    String persistentStockpileLabels = "none";
    String persistentStockpileMode = "disabled";
    String persistentStockpileStatus = "disabled";
    String marketTopResource;
    String marketTopType;
    ResourceIntelligenceEntry activeResource;
    DemandProfileMatch activeMatch;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MarketListingSnapshot {
    uint64 objectID = 0;
    uint64 vendorID = 0;
    uint64 ownerID = 0;
    int price = 0;
    bool onBazaar = false;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MarketSupplyRow {
    ResourceIntelligenceEntry resource;
    uint64 quantity = 0;
    int price = 0;
    float pricePerUnit = -1.f;
    String sourceType;
    String planet;
    uint64 ownerID = 0;
    uint64 vendorID = 0;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MarketProfileSupplyAggregate {
    String profileKey;
    uint64 quantity = 0;
    uint64 topQuantity = 0;
    int listings = 0;
    float cheapestPricePerUnit = -1.f;
    float medianPricePerUnit = -1.f;
    String confidence = "none";
    String topResource;
    String topType;
    Vector<float> pricesPerUnit;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct StockpileSnapshotLot {
    String conceptualLabel;
    uint64 quantity = 0;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct StockpileMarketReference {
    String profileKey;
    String resourceName;
    String resourceType;
    String confidence;
    uint64 quantity = 0;
    int listings = 0;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DemandWeightedMinerSnapshot {
    uint64 objectID = 0;
    String zoneName;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DemandWeightedMinerTarget {
    int resourceIndex = -1;
    DemandProfileMatch match;
    bool samePlanet = false;
    float adjustedResourceScore = 0.f;

    bool isValid() const {
        return resourceIndex >= 0 && match.eligible;
    }

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DemandWeightedMinerCandidate {
    int resultIndex = -1;
    DemandWeightedMinerTarget target;
    float locationAdjustedPressure = 0.f;
    float balancedScore = 0.f;
    int existingAssignments = 0;
    bool exceedsProfileCap = false;

    bool isValid() const {
        return resultIndex >= 0 && target.isValid();
    }

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MinerIntelligentTargetingMinerSnapshot {
    uint64 objectID = 0;
    String zoneName;
    Vector3 position;
    bool inNavmesh = false;
    bool dead = false;
    bool incapacitated = false;
    bool inCombat = false;
    ManagedReference<Zone*> zone;

    bool isValid() const {
        return objectID != 0 && zone != nullptr;
    }

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
    lua = new Lua();
    lua->init();
}

SimPlayerManager::~SimPlayerManager() {
    if (lua != nullptr) {
        delete lua;
        lua = nullptr;
    }
}

static int clampMinerInt(int value, int currentValue, int minValue, int maxValue) {
    if (value == 0)
        return currentValue;

    if (value < minValue)
        return minValue;

    if (value > maxValue)
        return maxValue;

    return value;
}

static int clampIntRange(int value, int minValue, int maxValue) {
    if (value < minValue)
        return minValue;

    if (value > maxValue)
        return maxValue;

    return value;
}

static float clampFloatRange(float value, float minValue, float maxValue) {
    if (value < minValue)
        return minValue;

    if (value > maxValue)
        return maxValue;

    return value;
}

static void addScorePart(int value, int weight, int& weightedTotal, int& totalWeight) {
    if (value <= 0 || weight <= 0)
        return;

    weightedTotal += value * weight;
    totalWeight += weight;
}

static int finishScore(int weightedTotal, int totalWeight) {
    if (totalWeight <= 0)
        return 0;

    return weightedTotal / totalWeight;
}

static JSONSerializationType buildReachabilityFunnelJSON(
        const MinerReachabilityCalibrationBucket& bucket);
static JSONSerializationType buildReachabilityDensityJSON(
        const MinerReachabilityCalibrationBucket& bucket);
static JSONSerializationType buildReachabilityBucketRowsJSON(
        VectorMap<String, MinerReachabilityCalibrationBucket>& buckets,
        const String& keyField);
static JSONSerializationType buildReachabilityOutcomeRowsJSON(
        VectorMap<String, MinerReachabilityValidationOutcome>& outcomes);
static JSONSerializationType buildReachabilityFailureRowsJSON(
        VectorMap<String, int>& reasons);
static JSONSerializationType buildReachabilityMemoryJSON(
        bool memoryEnabled,
        bool preferenceEnabled,
        int ttlSeconds,
        int bucketSizeMeters,
        int maxRows,
        int minAttemptsBeforePenalty,
        float verifiedPathScoreBonus,
        float sampleCompleteScoreBonus,
        float repeatedFailurePenalty,
        float longDistancePenalty512Plus,
        bool planetPenaltyEnabled,
        bool resourcePenaltyEnabled);

static int getResourceAttribute(ResourceSpawn* spawn, const String& attributeName) {
    if (spawn == nullptr)
        return 0;

    return spawn->getValueOf(attributeName);
}

static bool collectResourceIntelligenceSnapshot(Vector<ResourceIntelligenceEntry>& entries, String& errorMessage) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();

    if (zoneServer == nullptr) {
        errorMessage = "ZoneServer unavailable";
        return false;
    }

    ManagedReference<ResourceManager*> resourceManager = zoneServer->getResourceManager();

    if (resourceManager == nullptr) {
        errorMessage = "ResourceManager unavailable";
        return false;
    }

    Vector<ManagedReference<ResourceSpawn*> > spawns;

    {
        ReadLocker managerLocker(resourceManager);
        ResourceSpawner* spawner = resourceManager->getResourceSpawner();

        if (spawner == nullptr || spawner->getResourceMap() == nullptr) {
            errorMessage = "ResourceSpawner/resource map unavailable";
            return false;
        }

        ResourceMap* resourceMap = spawner->getResourceMap();
        int resourceCount = resourceMap->size();

        for (int i = 0; i < resourceCount; ++i) {
            ManagedReference<ResourceSpawn*> spawn = resourceMap->get(i);

            if (spawn != nullptr)
                spawns.add(spawn);
        }
    }

    for (int i = 0; i < spawns.size(); ++i) {
        ManagedReference<ResourceSpawn*> spawn = spawns.get(i);
        Locker spawnLocker(spawn);

        if (!spawn->inShift())
            continue;

        ResourceIntelligenceEntry entry;
        entry.objectID = spawn->getObjectID();
        entry.name = spawn->getName();
        entry.type = spawn->getType();
        entry.inShift = true;
        entry.despawned = spawn->getDespawned();
        entry.surveyToolType = spawn->getSurveyToolType();

        for (int classIndex = 0; classIndex < 12; ++classIndex) {
            String stfClass = spawn->getStfClass(classIndex);

            if (stfClass.isEmpty())
                break;

            if (!entry.classChain.isEmpty())
                entry.classChain += ">";

            entry.classChain += stfClass;
        }

        for (int zoneIndex = 0; zoneIndex < spawn->getSpawnMapSize(); ++zoneIndex) {
            String zoneName = spawn->getSpawnMapZone(zoneIndex);

            if (zoneName.isEmpty())
                continue;

            if (!entry.zones.isEmpty())
                entry.zones += ",";

            entry.zones += zoneName;
        }

        entry.oq = getResourceAttribute(spawn, "res_quality");
        entry.cd = getResourceAttribute(spawn, "res_conductivity");
        entry.dr = getResourceAttribute(spawn, "res_decay_resist");
        entry.hr = getResourceAttribute(spawn, "res_heat_resist");
        entry.fl = getResourceAttribute(spawn, "res_flavor");
        entry.ma = getResourceAttribute(spawn, "res_malleability");
        entry.pe = getResourceAttribute(spawn, "res_potential_energy");
        entry.sr = getResourceAttribute(spawn, "res_shock_resistance");
        entry.ut = getResourceAttribute(spawn, "res_toughness");
        entry.cr = getResourceAttribute(spawn, "res_cold_resist");

        entries.add(entry);
    }

    return true;
}

static void addProfileValue(Vector<String>& values, const String& value) {
    if (!value.isEmpty())
        values.add(value);
}

static ResourceScoringProfile createResourceScoringProfile(const String& key, const String& category, const String& description) {
    ResourceScoringProfile profile;
    profile.key = key;
    profile.category = category;
    profile.description = description;

    return profile;
}

static Vector<ResourceScoringProfile> createCuratedResourceScoringProfiles() {
    Vector<ResourceScoringProfile> profiles;

    ResourceScoringProfile weaponsmith = createResourceScoringProfile("weaponsmith_dl44", "weaponsmith", "DL44-style weapon profile");
    addProfileValue(weaponsmith.requiredTypes, "metal");
    addProfileValue(weaponsmith.requiredTypes, "copper");
    addProfileValue(weaponsmith.requiredTypes, "aluminum");
    addProfileValue(weaponsmith.preferredStats, "CD");
    addProfileValue(weaponsmith.preferredStats, "OQ");
    addProfileValue(weaponsmith.preferredStats, "SR");
    profiles.add(weaponsmith);

    ResourceScoringProfile chef = createResourceScoringProfile("chef_ahrisa", "chef", "Ahrisa food profile");
    addProfileValue(chef.requiredTypes, "vegetable");
    addProfileValue(chef.requiredTypes, "fruit");
    addProfileValue(chef.preferredStats, "OQ");
    addProfileValue(chef.preferredStats, "PE");
    addProfileValue(chef.preferredStats, "FL");
    addProfileValue(chef.preferredStats, "DR");
    profiles.add(chef);

    ResourceScoringProfile architect = createResourceScoringProfile("architect_mining_unit", "architect", "Mining unit component profile");
    addProfileValue(architect.requiredTypes, "steel");
    addProfileValue(architect.requiredTypes, "metal");
    addProfileValue(architect.requiredTypes, "gas");
    addProfileValue(architect.preferredStats, "UT");
    addProfileValue(architect.preferredStats, "HR");
    addProfileValue(architect.preferredStats, "SR");
    addProfileValue(architect.preferredStats, "OQ");
    profiles.add(architect);

    return profiles;
}

static bool resourceTypeMatches(const ResourceIntelligenceEntry& entry, const String& requiredType) {
    if (requiredType.isEmpty())
        return false;

    if (entry.type == requiredType || entry.type.beginsWith(requiredType + "_"))
        return true;

    if (entry.classChain.isEmpty())
        return false;

    String chain = String(">") + entry.classChain + ">";
    String needle = String(">") + requiredType + ">";

    return chain.indexOf(needle) >= 0;
}

static bool resourceMatchesAnyFamily(const ResourceIntelligenceEntry& entry, const char* const* families, int familyCount) {
    for (int i = 0; i < familyCount; ++i) {
        if (resourceTypeMatches(entry, String(families[i])))
            return true;
    }

    return false;
}

static bool broadScoreFamilyAllowsResource(const ResourceIntelligenceEntry& entry, int scoreFamily) {
    if (scoreFamily == 0)
        return true;

    switch (scoreFamily) {
    case 1: {
        const char* families[] = {"metal", "copper", "aluminum", "steel", "iron", "gemstone", "ore", "mineral"};
        return resourceMatchesAnyFamily(entry, families, 8);
    }
    case 2: {
        const char* families[] = {"metal", "steel", "iron", "copper", "aluminum", "bone", "hide", "chitin", "fiberplast", "wool", "synthetic", "ore", "mineral"};
        return resourceMatchesAnyFamily(entry, families, 13);
    }
    case 3: {
        const char* families[] = {"meat", "vegetable", "fruit", "cereal", "seafood", "water", "milk"};
        return resourceMatchesAnyFamily(entry, families, 7);
    }
    case 4: {
        const char* families[] = {"metal", "steel", "iron", "aluminum", "copper", "ore", "mineral", "gas", "chemical", "water", "energy", "wood", "fiberplast", "crystalline", "gemstone"};
        return resourceMatchesAnyFamily(entry, families, 15);
    }
    default:
        return false;
    }
}

static String getBestMatchedResourceType(const ResourceIntelligenceEntry& entry, const ResourceScoringProfile& profile) {
    String bestMatch;

    for (int i = 0; i < profile.requiredTypes.size(); ++i) {
        String requiredType = profile.requiredTypes.get(i);

        if (!resourceTypeMatches(entry, requiredType))
            continue;

        if (bestMatch.isEmpty() || requiredType.length() > bestMatch.length())
            bestMatch = requiredType;
    }

    return bestMatch;
}

static int getProfileStatValue(const ResourceIntelligenceEntry& entry, const String& stat) {
    if (stat == "OQ")
        return entry.oq;
    if (stat == "CD")
        return entry.cd;
    if (stat == "DR")
        return entry.dr;
    if (stat == "HR")
        return entry.hr;
    if (stat == "FL")
        return entry.fl;
    if (stat == "MA")
        return entry.ma;
    if (stat == "PE")
        return entry.pe;
    if (stat == "SR")
        return entry.sr;
    if (stat == "UT")
        return entry.ut;
    if (stat == "CR")
        return entry.cr;

    return 0;
}

static void addDemandValue(Vector<String>& values, const String& value) {
    if (!value.isEmpty())
        values.add(value);
}

static void addDemandStat(DemandProfileDefinition& profile, const String& stat, int weight) {
    if (stat.isEmpty() || weight <= 0)
        return;

    profile.preferredStats.add(stat);
    profile.statWeights.add(weight);
}

static DemandProfileDefinition createDemandProfile(
        const String& key,
        const String& category,
        bool serverBestSensitive,
        bool stockpileSensitive) {
    DemandProfileDefinition profile;
    profile.key = key;
    profile.category = category;
    profile.serverBestSensitive = serverBestSensitive;
    profile.stockpileSensitive = stockpileSensitive;

    return profile;
}

static Vector<DemandProfileDefinition> createDemandProfileDefinitions() {
    Vector<DemandProfileDefinition> profiles;

    DemandProfileDefinition composite = createDemandProfile(
        "composite_armor_supply", "armorsmith", true, true);
    addDemandValue(composite.activePhases, "mature_server");
    addDemandValue(composite.activePhases, "resource_rush");
    addDemandValue(composite.activePhases, "stockpile_phase");
    addDemandValue(composite.exactTypes, "ore_intrusive");
    addDemandValue(composite.exactTypes, "fuel_petrochem_solid_known");
    addDemandValue(composite.exactTypes, "fiberplast_naboo");
    addDemandValue(composite.exactTypes, "copper_beyrllius");
    addDemandValue(composite.exactTypes, "hide_wooly");
    addDemandValue(composite.exactTypes, "iron_colat");
    addDemandValue(composite.exactTypes, "steel_kiirium");
    addDemandValue(composite.exactTypes, "copper_polysteel");
    addDemandValue(composite.exactTypes, "petrochem_inert_polymer");
    addDemandValue(composite.exactTypes, "gemstone_armophous");
    addDemandValue(composite.premiumFamilies, "steel");
    addDemandValue(composite.premiumFamilies, "aluminum");
    addDemandValue(composite.premiumFamilies, "fiberplast");
    addDemandValue(composite.premiumFamilies, "hide_wooly");
    addDemandValue(composite.premiumFamilies, "petrochem_inert");
    addDemandValue(composite.premiumFamilies, "gemstone_armophous");
    addDemandValue(composite.bulkFamilies, "metal");
    addDemandValue(composite.bulkFamilies, "ore");
    addDemandStat(composite, "OQ", 3);
    addDemandStat(composite, "SR", 3);
    addDemandStat(composite, "UT", 3);
    addDemandStat(composite, "MA", 2);
    profiles.add(composite);

    DemandProfileDefinition weaponStaples = createDemandProfile(
        "master_weaponsmith_staples", "weaponsmith", true, true);
    addDemandValue(weaponStaples.activePhases, "early_server");
    addDemandValue(weaponStaples.activePhases, "mature_server");
    addDemandValue(weaponStaples.activePhases, "resource_rush");
    addDemandValue(weaponStaples.activePhases, "stockpile_phase");
    addDemandValue(weaponStaples.exactTypes, "steel_rhodium");
    addDemandValue(weaponStaples.exactTypes, "steel_duralloy");
    addDemandValue(weaponStaples.exactTypes, "steel_duranium");
    addDemandValue(weaponStaples.exactTypes, "copper_diatium");
    addDemandValue(weaponStaples.exactTypes, "ore_carbonate_ostrine");
    addDemandValue(weaponStaples.exactTypes, "aluminum_phrik");
    addDemandValue(weaponStaples.exactTypes, "gas_reactive_irolunn");
    addDemandValue(weaponStaples.exactTypes, "aluminum_chromium");
    addDemandValue(weaponStaples.exactTypes, "wood_deciduous_corellia");
    addDemandValue(weaponStaples.exactTypes, "aluminum_linksteel");
    addDemandValue(weaponStaples.exactTypes, "copper_desh");
    addDemandValue(weaponStaples.exactTypes, "copper_platinite");
    addDemandValue(weaponStaples.premiumFamilies, "steel");
    addDemandValue(weaponStaples.premiumFamilies, "copper");
    addDemandValue(weaponStaples.premiumFamilies, "aluminum");
    addDemandValue(weaponStaples.premiumFamilies, "petrochem_inert");
    addDemandValue(weaponStaples.premiumFamilies, "gas_reactive");
    addDemandValue(weaponStaples.premiumFamilies, "gemstone");
    addDemandValue(weaponStaples.bulkFamilies, "metal");
    addDemandValue(weaponStaples.bulkFamilies, "iron");
    addDemandValue(weaponStaples.bulkFamilies, "wood");
    addDemandStat(weaponStaples, "CD", 4);
    addDemandStat(weaponStaples, "OQ", 3);
    addDemandStat(weaponStaples, "SR", 3);
    addDemandStat(weaponStaples, "UT", 2);
    addDemandStat(weaponStaples, "HR", 2);
    profiles.add(weaponStaples);

    DemandProfileDefinition weaponComponents = createDemandProfile(
        "high_damage_weapon_components", "weaponsmith", true, true);
    addDemandValue(weaponComponents.activePhases, "mature_server");
    addDemandValue(weaponComponents.activePhases, "resource_rush");
    addDemandValue(weaponComponents.activePhases, "stockpile_phase");
    addDemandValue(weaponComponents.exactTypes, "steel_rhodium");
    addDemandValue(weaponComponents.exactTypes, "steel_duralloy");
    addDemandValue(weaponComponents.exactTypes, "steel_duranium");
    addDemandValue(weaponComponents.exactTypes, "copper_diatium");
    addDemandValue(weaponComponents.exactTypes, "ore_carbonate_ostrine");
    addDemandValue(weaponComponents.exactTypes, "aluminum_phrik");
    addDemandValue(weaponComponents.exactTypes, "gas_reactive_irolunn");
    addDemandValue(weaponComponents.exactTypes, "armophous_ryll");
    addDemandValue(weaponComponents.exactTypes, "copper_desh");
    addDemandValue(weaponComponents.exactTypes, "copper_platinite");
    addDemandValue(weaponComponents.premiumFamilies, "steel");
    addDemandValue(weaponComponents.premiumFamilies, "copper");
    addDemandValue(weaponComponents.premiumFamilies, "aluminum");
    addDemandValue(weaponComponents.premiumFamilies, "petrochem_inert");
    addDemandValue(weaponComponents.premiumFamilies, "gas_reactive");
    addDemandValue(weaponComponents.premiumFamilies, "gemstone_armophous");
    addDemandValue(weaponComponents.bulkFamilies, "metal");
    addDemandValue(weaponComponents.bulkFamilies, "iron");
    addDemandStat(weaponComponents, "CD", 5);
    addDemandStat(weaponComponents, "SR", 3);
    addDemandStat(weaponComponents, "OQ", 2);
    addDemandStat(weaponComponents, "UT", 2);
    addDemandStat(weaponComponents, "HR", 2);
    profiles.add(weaponComponents);

    DemandProfileDefinition chefBuffs = createDemandProfile(
        "chef_buff_foods", "chef", true, true);
    addDemandValue(chefBuffs.activePhases, "early_server");
    addDemandValue(chefBuffs.activePhases, "mature_server");
    addDemandValue(chefBuffs.activePhases, "resource_rush");
    addDemandValue(chefBuffs.activePhases, "stockpile_phase");
    addDemandValue(chefBuffs.exactTypes, "fruit_fruits");
    addDemandValue(chefBuffs.exactTypes, "fruit_berries");
    addDemandValue(chefBuffs.exactTypes, "wheat_wild");
    addDemandValue(chefBuffs.exactTypes, "wheat_domesticated");
    addDemandValue(chefBuffs.exactTypes, "vegetable_tubers");
    addDemandValue(chefBuffs.exactTypes, "vegetable_fungi");
    addDemandValue(chefBuffs.exactTypes, "meat_carnivore");
    addDemandValue(chefBuffs.premiumFamilies, "seafood");
    addDemandValue(chefBuffs.premiumFamilies, "meat_egg");
    addDemandValue(chefBuffs.premiumFamilies, "fruit");
    addDemandValue(chefBuffs.premiumFamilies, "vegetable");
    addDemandValue(chefBuffs.premiumFamilies, "vegetable_beans");
    addDemandValue(chefBuffs.premiumFamilies, "vegetable_tubers");
    addDemandValue(chefBuffs.premiumFamilies, "vegetable_greens");
    addDemandValue(chefBuffs.premiumFamilies, "cereal");
    addDemandValue(chefBuffs.premiumFamilies, "wheat");
    addDemandValue(chefBuffs.premiumFamilies, "rice");
    addDemandValue(chefBuffs.premiumFamilies, "corn");
    addDemandValue(chefBuffs.premiumFamilies, "oats");
    addDemandValue(chefBuffs.premiumFamilies, "meat");
    addDemandValue(chefBuffs.premiumFamilies, "milk");
    addDemandValue(chefBuffs.premiumFamilies, "water");
    addDemandStat(chefBuffs, "PE", 4);
    addDemandStat(chefBuffs, "FL", 4);
    addDemandStat(chefBuffs, "OQ", 3);
    addDemandStat(chefBuffs, "DR", 3);
    addDemandStat(chefBuffs, "SR", 1);
    profiles.add(chefBuffs);

    DemandProfileDefinition chefHighValue = createDemandProfile(
        "chef_high_value_consumables", "chef", true, true);
    addDemandValue(chefHighValue.activePhases, "mature_server");
    addDemandValue(chefHighValue.activePhases, "resource_rush");
    addDemandValue(chefHighValue.activePhases, "stockpile_phase");
    addDemandValue(chefHighValue.exactTypes, "fruit_fruits");
    addDemandValue(chefHighValue.exactTypes, "fruit_berries");
    addDemandValue(chefHighValue.exactTypes, "wheat_wild");
    addDemandValue(chefHighValue.exactTypes, "vegetable_tubers");
    addDemandValue(chefHighValue.exactTypes, "meat_carnivore");
    addDemandValue(chefHighValue.premiumFamilies, "seafood");
    addDemandValue(chefHighValue.premiumFamilies, "meat_egg");
    addDemandValue(chefHighValue.premiumFamilies, "fruit");
    addDemandValue(chefHighValue.premiumFamilies, "vegetable");
    addDemandValue(chefHighValue.premiumFamilies, "vegetable_beans");
    addDemandValue(chefHighValue.premiumFamilies, "vegetable_tubers");
    addDemandValue(chefHighValue.premiumFamilies, "vegetable_greens");
    addDemandValue(chefHighValue.premiumFamilies, "cereal");
    addDemandValue(chefHighValue.premiumFamilies, "wheat");
    addDemandValue(chefHighValue.premiumFamilies, "rice");
    addDemandValue(chefHighValue.premiumFamilies, "corn");
    addDemandValue(chefHighValue.premiumFamilies, "oats");
    addDemandValue(chefHighValue.premiumFamilies, "meat");
    addDemandValue(chefHighValue.premiumFamilies, "milk");
    addDemandValue(chefHighValue.premiumFamilies, "water");
    addDemandStat(chefHighValue, "PE", 5);
    addDemandStat(chefHighValue, "FL", 4);
    addDemandStat(chefHighValue, "DR", 3);
    addDemandStat(chefHighValue, "OQ", 2);
    profiles.add(chefHighValue);

    DemandProfileDefinition infrastructure = createDemandProfile(
        "production_infrastructure", "architect", true, true);
    addDemandValue(infrastructure.activePhases, "early_server");
    addDemandValue(infrastructure.activePhases, "mature_server");
    addDemandValue(infrastructure.activePhases, "resource_rush");
    addDemandValue(infrastructure.activePhases, "stockpile_phase");
    addDemandValue(infrastructure.premiumFamilies, "steel");
    addDemandValue(infrastructure.premiumFamilies, "metal");
    addDemandValue(infrastructure.bulkFamilies, "ore");
    addDemandValue(infrastructure.bulkFamilies, "chemical");
    addDemandValue(infrastructure.bulkFamilies, "gas_inert");
    addDemandValue(infrastructure.bulkFamilies, "gas_reactive");
    infrastructure.scoreBulkQuality = false;
    addDemandStat(infrastructure, "UT", 4);
    addDemandStat(infrastructure, "HR", 3);
    addDemandStat(infrastructure, "SR", 3);
    addDemandStat(infrastructure, "MA", 2);
    addDemandStat(infrastructure, "OQ", 2);
    addDemandStat(infrastructure, "DR", 2);
    profiles.add(infrastructure);

    return profiles;
}

static bool demandProfileActiveForPhase(const DemandProfileDefinition& profile, const String& serverPhase) {
    for (int i = 0; i < profile.activePhases.size(); ++i) {
        if (profile.activePhases.get(i) == serverPhase)
            return true;
    }

    return false;
}

static String getExactDemandTypeMatch(const ResourceIntelligenceEntry& entry, const DemandProfileDefinition& profile) {
    String bestMatch;

    for (int i = 0; i < profile.exactTypes.size(); ++i) {
        String exactType = profile.exactTypes.get(i);

        if (!resourceTypeMatches(entry, exactType))
            continue;

        if (bestMatch.isEmpty() || exactType.length() > bestMatch.length())
            bestMatch = exactType;
    }

    return bestMatch;
}

static String getBestDemandFamilyMatch(const ResourceIntelligenceEntry& entry, const Vector<String>& families) {
    String bestMatch;

    for (int i = 0; i < families.size(); ++i) {
        String family = families.get(i);

        if (!resourceTypeMatches(entry, family))
            continue;

        if (bestMatch.isEmpty() || family.length() > bestMatch.length())
            bestMatch = family;
    }

    return bestMatch;
}

static String formatDemandFamilyLabel(const String& matchedType) {
    if (matchedType == "meat_egg")
        return "egg";

    if (matchedType == "vegetable_beans")
        return "beans";

    if (matchedType == "vegetable_tubers")
        return "tubers";

    if (matchedType == "vegetable_greens")
        return "greens";

    if (matchedType.beginsWith("petrochem_") || matchedType.beginsWith("fuel_petrochem_"))
        return "petrochem";

    if (matchedType.beginsWith("gemstone_"))
        return "gemstone";

    if (matchedType.beginsWith("ore_"))
        return "ore";

    return matchedType;
}

static DemandProfileMatch evaluateDemandProfileResource(
        const ResourceIntelligenceEntry& entry,
        const DemandProfileDefinition& profile,
        float weight,
        int priority) {
    DemandProfileMatch match;
    String exactType = getExactDemandTypeMatch(entry, profile);
    String premiumFamily = getBestDemandFamilyMatch(entry, profile.premiumFamilies);
    String bulkFamily = getBestDemandFamilyMatch(entry, profile.bulkFamilies);
    int eligibilityBonus = 0;

    if (!exactType.isEmpty()) {
        match.exact = true;
        match.premium = true;
        match.matchedType = exactType;
        eligibilityBonus = 100;
    } else if (!premiumFamily.isEmpty()) {
        match.premium = true;
        match.matchedType = premiumFamily;
        eligibilityBonus = 50;
    } else if (!bulkFamily.isEmpty()) {
        match.bulk = true;
        match.matchedType = bulkFamily;
        eligibilityBonus = 10;
    } else {
        return match;
    }

    if (match.bulk && !profile.scoreBulkQuality) {
        // Bulk eligibility represents supply volume, not a server-best quality claim.
        match.baseScore = 1;
    } else {
        int weightedTotal = 0;
        int totalWeight = 0;

        for (int i = 0; i < profile.preferredStats.size() && i < profile.statWeights.size(); ++i) {
            addScorePart(
                getProfileStatValue(entry, profile.preferredStats.get(i)),
                profile.statWeights.get(i),
                weightedTotal,
                totalWeight);
        }

        match.baseScore = finishScore(weightedTotal, totalWeight);
        match.qualityScored = true;
    }

    if (match.baseScore <= 0 || weight <= 0.f || priority <= 0)
        return match;

    float priorityScale = static_cast<float>(priority) / 100.f;
    match.demandScore = static_cast<int>((match.baseScore + eligibilityBonus) * weight * priorityScale);
    match.eligible = match.demandScore > 0;

    return match;
}

static String formatDemandStatReason(const ResourceIntelligenceEntry& entry, const DemandProfileDefinition& profile) {
    String reason;

    for (int i = 0; i < profile.preferredStats.size(); ++i) {
        String stat = profile.preferredStats.get(i);
        int value = getProfileStatValue(entry, stat);

        if (value <= 0)
            continue;

        if (!reason.isEmpty())
            reason += "/";

        reason += stat + "=" + String::valueOf(value);
    }

    return reason.isEmpty() ? String("no weighted stats available") : String("weighted ") + reason;
}

static String formatDemandProfileSimulationLine(
        const DemandProfileDefinition& profile,
        const ResourceIntelligenceEntry& entry,
        const DemandProfileMatch& match,
        const String& serverPhase,
        float weight,
        int priority,
        int rank) {
    String eligibilityReason;

    if (match.exact) {
        eligibilityReason = String("exactType=") + match.matchedType;
    } else {
        eligibilityReason = String("eligibleFamily=") + formatDemandFamilyLabel(match.matchedType);
    }

    String qualityReason = match.qualityScored ?
        formatDemandStatReason(entry, profile) :
        String("bulk supply; quality not scored");

    return String("DemandProfileSimulation profile=") + profile.key +
        " category=" + profile.category +
        " phase=" + serverPhase +
        " rank=" + String::valueOf(rank) +
        " resource=" + entry.name +
        " type=" + entry.type +
        " zones=" + (entry.zones.isEmpty() ? String("unknown") : entry.zones) +
        " demandScore=" + String::valueOf(match.demandScore) +
        " baseScore=" + String::valueOf(match.baseScore) +
        " priority=" + String::valueOf(priority) +
        " weight=" + String::valueOf(weight) +
        " reason=" + eligibilityReason +
        "; " + qualityReason +
        "; premiumQuality=" + (match.premium ? String("true") : String("false")) +
        "; bulkEligible=" + (match.bulk ? String("true") : String("false")) +
        "; stockpileSensitive=" + (profile.stockpileSensitive ? String("true") : String("false")) +
        "; serverBestSensitive=" + (profile.serverBestSensitive ? String("true") : String("false")) +
        " mode=log-only";
}

static bool findDemandProfileDefinition(
        const Vector<DemandProfileDefinition>& profiles,
        const String& profileKey,
        DemandProfileDefinition& result) {
    for (int i = 0; i < profiles.size(); ++i) {
        DemandProfileDefinition profile = profiles.get(i);

        if (profile.key == profileKey) {
            result = profile;
            return true;
        }
    }

    return false;
}

static bool demandStateProfileUsesConceptualLabel(const String& profileKey, const String& resourceLabel) {
    if (profileKey == "composite_armor_supply" ||
            profileKey == "master_weaponsmith_staples" ||
            profileKey == "high_damage_weapon_components") {
        return resourceLabel == "copper" || resourceLabel == "iron";
    }

    if (profileKey == "chef_buff_foods" || profileKey == "chef_high_value_consumables")
        return resourceLabel == "water";

    if (profileKey == "production_infrastructure")
        return resourceLabel == "copper" || resourceLabel == "iron" || resourceLabel == "gas";

    return false;
}

static void addUniqueLabel(Vector<String>& labels, const String& label) {
    if (label.isEmpty() || labels.contains(label))
        return;

    labels.add(label);
}

static void addIntCounter(VectorMap<String, int>& counters, const String& key, int amount = 1) {
    if (key.isEmpty())
        return;

    int value = counters.contains(key) ? counters.get(key) : 0;
    counters.put(key, value + amount);
}

static void addUint64Counter(VectorMap<String, uint64>& counters, const String& key, uint64 amount) {
    if (key.isEmpty() || amount == 0)
        return;

    uint64 value = counters.contains(key) ? counters.get(key) : 0;
    counters.put(key, value + amount);
}

static String getDemandProfilesForConceptualLabel(
        const Vector<DemandProfileDefinition>& profiles,
        const String& label) {
    String result;
    String normalizedLabel = label.toLowerCase();

    for (int i = 0; i < profiles.size(); ++i) {
        DemandProfileDefinition profile = profiles.get(i);

        if (!demandStateProfileUsesConceptualLabel(
                profile.key, normalizedLabel))
            continue;

        if (!result.isEmpty())
            result += ",";

        result += profile.key;
    }

    return result.isEmpty() ? String("none") : result;
}

static uint64 estimateConceptualDemandStateSupply(
        const String& profileKey,
        const Vector<String>& resourceNames,
        const Vector<uint64>& amounts,
        String& supplyConfidence,
        String& supplyLabels) {
    uint64 total = 0;

    for (int i = 0; i < resourceNames.size() && i < amounts.size(); ++i) {
        String resourceLabel = resourceNames.get(i).toLowerCase();

        if (!demandStateProfileUsesConceptualLabel(profileKey, resourceLabel))
            continue;

        uint64 amount = amounts.get(i);
        total += amount;

        if (!supplyLabels.isEmpty())
            supplyLabels += ",";

        supplyLabels += resourceLabel + "=" + String::valueOf(amount);
    }

    if (supplyLabels.isEmpty()) {
        supplyConfidence = "none";
        supplyLabels = "none";
    } else {
        supplyConfidence = "coarse_family";
    }

    return total;
}

static uint64 estimatePersistentConceptualDemandStateSupply(
        const String& profileKey,
        const VectorMap<String, uint64>& labelQuantities,
        int& lotsMatched,
        String& supplyConfidence,
        String& supplyLabels) {
    uint64 total = 0;
    lotsMatched = 0;
    supplyLabels = "";

    for (int i = 0; i < labelQuantities.size(); ++i) {
        String resourceLabel = labelQuantities.elementAt(i).getKey().toLowerCase();

        if (!demandStateProfileUsesConceptualLabel(profileKey, resourceLabel))
            continue;

        uint64 amount = labelQuantities.get(i);

        if (amount == 0)
            continue;

        total += amount;
        lotsMatched++;

        if (!supplyLabels.isEmpty())
            supplyLabels += ",";

        supplyLabels += resourceLabel + "=" + String::valueOf(amount);
    }

    if (supplyLabels.isEmpty()) {
        supplyConfidence = "none";
        supplyLabels = "none";
    } else {
        supplyConfidence = "conceptual_label";
    }

    return total;
}

static String formatDemandStateOpportunityReason(
        const DemandProfileMatch& match,
        const ResourceIntelligenceEntry& entry,
        const DemandProfileDefinition& profile) {
    if (!match.eligible)
        return "no eligible active resource";

    String eligibility = match.exact ?
        String("exactType=") + match.matchedType :
        String("eligibleFamily=") + formatDemandFamilyLabel(match.matchedType);
    String quality = match.qualityScored ?
        formatDemandStatReason(entry, profile) :
        String("bulk supply; quality not scored");

    return eligibility + "; " + quality;
}

static void calculateDemandStatePressure(
        DemandStateSimulationResult& result,
        float lowThreshold,
        float criticalThreshold,
        float shortageWeight,
        float activeOpportunityWeight,
        float surplusDampening) {
    result.totalKnownSupply = result.aiConceptualSupply +
        result.marketObservedSupply + result.persistentStockpileSupply;

    if (result.desiredReserve == 0) {
        result.state = "disabledReserve";
        return;
    }

    result.reserveRatio =
        static_cast<float>(result.totalKnownSupply) /
        static_cast<float>(result.desiredReserve);

    if (result.totalKnownSupply < result.desiredReserve)
        result.shortageUnits = result.desiredReserve - result.totalKnownSupply;
    else
        result.surplusUnits = result.totalKnownSupply - result.desiredReserve;

    if (result.reserveRatio <= criticalThreshold)
        result.state = "critical";
    else if (result.reserveRatio <= lowThreshold)
        result.state = "low";
    else if (result.totalKnownSupply < result.desiredReserve)
        result.state = "target";
    else
        result.state = "surplus";

    float shortageRatio = 1.f - Math::min(result.reserveRatio, 1.f);
    result.shortagePressure = shortageRatio * 1000.f * shortageWeight;
    result.opportunityPressure =
        static_cast<float>(result.activeMatch.demandScore) *
        activeOpportunityWeight;

    if (result.state == "surplus")
        result.pressureScore = result.opportunityPressure * surplusDampening;
    else
        result.pressureScore =
            result.shortagePressure + result.opportunityPressure;
}

static void collectMarketListingSnapshots(
        TerminalListVector& terminalLists,
        bool onBazaar,
        int maxListings,
        int& listingsExamined,
        Vector<MarketListingSnapshot>& snapshots) {
    for (int terminalIndex = 0;
            terminalIndex < terminalLists.size() && listingsExamined < maxListings;
            ++terminalIndex) {
        Reference<TerminalItemList*> terminalList = terminalLists.get(terminalIndex);

        if (terminalList == nullptr)
            continue;

        Vector<ManagedReference<AuctionItem*> > listings;

        {
            ReadLocker listLocker(terminalList);

            for (int listingIndex = 0;
                    listingIndex < terminalList->size() &&
                    listingsExamined < maxListings;
                    ++listingIndex) {
                ManagedReference<AuctionItem*> listing = terminalList->get(listingIndex);
                listingsExamined++;

                if (listing != nullptr)
                    listings.add(listing);
            }
        }

        for (int listingIndex = 0; listingIndex < listings.size(); ++listingIndex) {
            ManagedReference<AuctionItem*> listing = listings.get(listingIndex);

            if (listing == nullptr)
                continue;

            Locker listingLocker(listing);

            if (listing->getStatus() != AuctionItem::FORSALE ||
                    listing->isOnBazaar() != onBazaar) {
                continue;
            }

            MarketListingSnapshot snapshot;
            snapshot.objectID = listing->getAuctionedItemObjectID();
            snapshot.vendorID = listing->getVendorID();
            snapshot.ownerID = listing->getOwnerID();
            snapshot.price = listing->getPrice();
            snapshot.onBazaar = onBazaar;
            snapshots.add(snapshot);
        }
    }
}

static MarketProfileSupplyAggregate* findMarketProfileAggregate(
        Vector<MarketProfileSupplyAggregate>& aggregates,
        const String& profileKey) {
    for (int i = 0; i < aggregates.size(); ++i) {
        if (aggregates.get(i).profileKey == profileKey)
            return &aggregates.get(i);
    }

    return nullptr;
}

static float calculateMedianPrice(Vector<float>& prices) {
    if (prices.size() == 0)
        return -1.f;

    for (int i = 0; i < prices.size(); ++i) {
        for (int j = i + 1; j < prices.size(); ++j) {
            if (prices.get(j) >= prices.get(i))
                continue;

            float swap = prices.get(i);
            prices.set(i, prices.get(j));
            prices.set(j, swap);
        }
    }

    int middle = prices.size() / 2;

    if ((prices.size() % 2) == 1)
        return prices.get(middle);

    return (prices.get(middle - 1) + prices.get(middle)) / 2.f;
}

static String combineSupplyConfidence(
        const String& conceptualConfidence,
        const String& marketConfidence) {
    if (marketConfidence == "exact_type")
        return "exact_type";

    if (marketConfidence == "coarse_family" || conceptualConfidence == "coarse_family")
        return "coarse_family";

    if (marketConfidence == "conceptual_label" || conceptualConfidence == "conceptual_label")
        return "conceptual_label";

    return "none";
}

static int calculateProfileScore(const ResourceIntelligenceEntry& entry, const ResourceScoringProfile& profile) {
    if (getBestMatchedResourceType(entry, profile).isEmpty())
        return 0;

    int weightedTotal = 0;
    int totalWeight = 0;

    for (int i = 0; i < profile.preferredStats.size(); ++i) {
        String stat = profile.preferredStats.get(i);
        int weight = (i == 0) ? 3 : 2;

        addScorePart(getProfileStatValue(entry, stat), weight, weightedTotal, totalWeight);
    }

    return finishScore(weightedTotal, totalWeight);
}

static bool configuredProfileKeyEnabled(const Vector<String>& configuredKeys, const String& key) {
    if (configuredKeys.size() == 0)
        return true;

    for (int i = 0; i < configuredKeys.size(); ++i) {
        if (configuredKeys.get(i) == key)
            return true;
    }

    return false;
}

static String formatProfileStatReason(const ResourceIntelligenceEntry& entry, const ResourceScoringProfile& profile) {
    String reason;

    for (int i = 0; i < profile.preferredStats.size(); ++i) {
        String stat = profile.preferredStats.get(i);
        int value = getProfileStatValue(entry, stat);

        if (value <= 0)
            continue;

        if (!reason.isEmpty())
            reason += "/";

        reason += stat + "=" + String::valueOf(value);
    }

    if (reason.isEmpty())
        return "eligible resource family";

    return String("high ") + reason;
}

static String formatProfileRequiredTypes(const ResourceScoringProfile& profile) {
    String requiredTypes;

    for (int i = 0; i < profile.requiredTypes.size(); ++i) {
        if (!requiredTypes.isEmpty())
            requiredTypes += "/";

        requiredTypes += profile.requiredTypes.get(i);
    }

    return requiredTypes.isEmpty() ? String("none") : requiredTypes;
}

static String formatResourceScoringProfileLine(const ResourceScoringProfile& profile, const ResourceIntelligenceEntry& entry, int score, const String& matchedType) {
    String line = String("ResourceIntelligence profile ") + profile.key +
        " category=" + profile.category +
        " topResource=" + entry.name +
        " type=" + entry.type +
        " score=" + String::valueOf(score) +
        " reason=eligible " + (matchedType.isEmpty() ? String("unknown") : matchedType) +
        " family; " + formatProfileStatReason(entry, profile) +
        " description=\"" + profile.description + "\"" +
        " mode=log-only";

    return line;
}

static bool resourceAvailableInZone(const ResourceIntelligenceEntry& entry, const String& zoneName) {
    if (zoneName.isEmpty() || zoneName == "unknown" || entry.zones.isEmpty())
        return false;

    String zones = String(",") + entry.zones + ",";
    String needle = String(",") + zoneName + ",";

    return zones.indexOf(needle) >= 0;
}

static String formatMinerTargetRecommendationLine(uint64 minerID, const String& zoneName, const ResourceScoringProfile& profile, const ResourceIntelligenceEntry& entry, int score, const String& matchedType, bool travelRequired) {
    String line = String("MinerTargetRecommendation miner=") + String::valueOf(minerID) +
        " zone=" + (zoneName.isEmpty() ? String("unknown") : zoneName) +
        " profile=" + profile.key +
        " category=" + profile.category +
        " target=" + entry.name +
        " type=" + entry.type +
        " zones=" + (entry.zones.isEmpty() ? String("unknown") : entry.zones) +
        " score=" + String::valueOf(score) +
        " reason=eligible " + (matchedType.isEmpty() ? String("unknown") : matchedType) +
        " family; " + formatProfileStatReason(entry, profile);

    if (travelRequired)
        line += " travelRequired=true";

    line += " mode=log-only";

    return line;
}

struct MinerTargetSimulationPlan {
    int profileIndex = -1;
    int resourceIndex = -1;
    int baseScore = 0;
    int adjustedScore = 0;
    bool samePlanet = false;
    bool fallbackProfile = false;
    String matchedType;

    bool isValid() const {
        return profileIndex >= 0 && resourceIndex >= 0 && baseScore > 0;
    }
};

static float getMinerTargetSimulationProfileWeight(VectorMap<String, float>& profileWeights, const String& profileKey) {
    if (!profileWeights.contains(profileKey))
        return 1.0f;

    return profileWeights.get(profileKey);
}

static MinerTargetSimulationPlan selectMinerTargetSimulationPlan(
        const Vector<ResourceIntelligenceEntry>& entries,
        const Vector<ResourceScoringProfile>& profiles,
        int profileIndex,
        const String& zoneName,
        float profileWeight,
        bool preferSamePlanet,
        int samePlanetBonus,
        int travelPenalty) {
    MinerTargetSimulationPlan plan;

    if (profileIndex < 0 || profileIndex >= profiles.size() || profileWeight <= 0.f)
        return plan;

    ResourceScoringProfile profile = profiles.get(profileIndex);
    int bestAdjustedScore = -1000000;

    for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
        ResourceIntelligenceEntry entry = entries.get(entryIndex);
        String matchedType = getBestMatchedResourceType(entry, profile);

        if (matchedType.isEmpty())
            continue;

        int baseScore = calculateProfileScore(entry, profile);

        if (baseScore <= 0)
            continue;

        bool samePlanet = resourceAvailableInZone(entry, zoneName);
        int adjustedScore = static_cast<int>(baseScore * profileWeight);

        if (preferSamePlanet)
            adjustedScore += samePlanet ? samePlanetBonus : -travelPenalty;

        if (plan.isValid() && adjustedScore <= bestAdjustedScore)
            continue;

        plan.profileIndex = profileIndex;
        plan.resourceIndex = entryIndex;
        plan.baseScore = baseScore;
        plan.adjustedScore = adjustedScore;
        plan.samePlanet = samePlanet;
        plan.matchedType = matchedType;
        bestAdjustedScore = adjustedScore;
    }

    return plan;
}

static MinerTargetSimulationPlan selectAssignedMinerTargetSimulationPlan(
        const Vector<ResourceIntelligenceEntry>& entries,
        const Vector<ResourceScoringProfile>& profiles,
        const Vector<int>& enabledProfileIndexes,
        VectorMap<String, float>& profileWeights,
        int minerOrdinal,
        const String& zoneName,
        bool preferSamePlanet,
        int samePlanetBonus,
        int travelPenalty,
        int& assignedProfileIndex) {
    MinerTargetSimulationPlan selectedPlan;
    assignedProfileIndex = -1;

    if (enabledProfileIndexes.size() == 0)
        return selectedPlan;

    int assignedEnabledIndex = minerOrdinal % enabledProfileIndexes.size();
    assignedProfileIndex = enabledProfileIndexes.get(assignedEnabledIndex);
    ResourceScoringProfile assignedProfile = profiles.get(assignedProfileIndex);
    float assignedWeight = getMinerTargetSimulationProfileWeight(profileWeights, assignedProfile.key);

    selectedPlan = selectMinerTargetSimulationPlan(
        entries,
        profiles,
        assignedProfileIndex,
        zoneName,
        assignedWeight,
        preferSamePlanet,
        samePlanetBonus,
        travelPenalty);

    if (selectedPlan.isValid())
        return selectedPlan;

    for (int enabledIndex = 0; enabledIndex < enabledProfileIndexes.size(); ++enabledIndex) {
        int fallbackProfileIndex = enabledProfileIndexes.get(enabledIndex);

        if (fallbackProfileIndex == assignedProfileIndex)
            continue;

        ResourceScoringProfile fallbackProfile = profiles.get(fallbackProfileIndex);
        float fallbackWeight = getMinerTargetSimulationProfileWeight(profileWeights, fallbackProfile.key);
        MinerTargetSimulationPlan fallbackPlan = selectMinerTargetSimulationPlan(
            entries,
            profiles,
            fallbackProfileIndex,
            zoneName,
            fallbackWeight,
            preferSamePlanet,
            samePlanetBonus,
            travelPenalty);

        if (!fallbackPlan.isValid())
            continue;

        fallbackPlan.fallbackProfile = true;

        if (!selectedPlan.isValid() || fallbackPlan.adjustedScore > selectedPlan.adjustedScore)
            selectedPlan = fallbackPlan;
    }

    return selectedPlan;
}

static String formatMinerTargetSimulationLine(
        uint64 minerID,
        const String& zoneName,
        const ResourceScoringProfile& profile,
        const ResourceIntelligenceEntry& entry,
        const MinerTargetSimulationPlan& plan,
        const String& assignmentMode) {
    String assignmentReason = plan.fallbackProfile ?
        String("fallback from unavailable round_robin profile") :
        String("round_robin profile assignment");

    return String("MinerTargetSimulation miner=") + String::valueOf(minerID) +
        " zone=" + (zoneName.isEmpty() ? String("unknown") : zoneName) +
        " assignedProfile=" + profile.key +
        " category=" + profile.category +
        " target=" + entry.name +
        " type=" + entry.type +
        " zones=" + (entry.zones.isEmpty() ? String("unknown") : entry.zones) +
        " baseScore=" + String::valueOf(plan.baseScore) +
        " adjustedScore=" + String::valueOf(plan.adjustedScore) +
        " samePlanet=" + (plan.samePlanet ? String("true") : String("false")) +
        " travelRequired=" + (plan.samePlanet ? String("false") : String("true")) +
        " assignmentMode=" + assignmentMode +
        " reason=" + assignmentReason +
        "; eligible " + (plan.matchedType.isEmpty() ? String("unknown") : plan.matchedType) +
        " family; " + formatProfileStatReason(entry, profile) +
        " mode=simulation-only";
}

static DemandWeightedMinerTarget selectDemandWeightedMinerTarget(
        const Vector<ResourceIntelligenceEntry>& entries,
        const DemandProfileDefinition& profile,
        const String& zoneName,
        int samePlanetBonus,
        int travelPenalty) {
    DemandWeightedMinerTarget target;

    for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
        ResourceIntelligenceEntry entry = entries.get(entryIndex);
        DemandProfileMatch match =
            evaluateDemandProfileResource(entry, profile, 1.f, 100);

        if (!match.eligible)
            continue;

        bool samePlanet = resourceAvailableInZone(entry, zoneName);
        float adjustedScore = static_cast<float>(match.demandScore) +
            (samePlanet ? static_cast<float>(samePlanetBonus) :
                -static_cast<float>(travelPenalty));

        if (target.isValid() && adjustedScore <= target.adjustedResourceScore)
            continue;

        target.resourceIndex = entryIndex;
        target.match = match;
        target.samePlanet = samePlanet;
        target.adjustedResourceScore = adjustedScore;
    }

    return target;
}

struct DemandWeightedPlanSelection {
    bool valid = false;
    DemandWeightedMinerCandidate selected;
    DemandStateSimulationResult selectedResult;
    DemandProfileDefinition selectedProfile;
    ResourceIntelligenceEntry selectedResource;
    String assignmentReason = "none";
    bool strongPressureOverflow = false;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

static void buildDemandWeightedPressureResultsForMiners(
        const Vector<ResourceIntelligenceEntry>& entries,
        const Vector<DemandProfileDefinition>& profiles,
        const Vector<String>& conceptualResourceNames,
        const Vector<uint64>& conceptualAmounts,
        VectorMap<String, uint64>& marketQuantities,
        VectorMap<String, int>& profileEnabled,
        VectorMap<String, int>& desiredReserve,
        VectorMap<String, float>& lowStockThreshold,
        VectorMap<String, float>& criticalStockThreshold,
        const String& serverPhase,
        float shortageWeight,
        float activeOpportunityWeight,
        float surplusDampening,
        float minimumPressureThreshold,
        Vector<DemandStateSimulationResult>& pressureResults,
        int& profilesDisabled,
        int& profilesInactivePhase,
        int& profilesBelowPressure,
        int& profilesNoEligibleResource) {
    pressureResults.removeAll();
    profilesDisabled = 0;
    profilesInactivePhase = 0;
    profilesBelowPressure = 0;
    profilesNoEligibleResource = 0;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        DemandProfileDefinition profile = profiles.get(profileIndex);
        bool enabled = !profileEnabled.contains(profile.key) ||
            profileEnabled.get(profile.key) != 0;

        if (!enabled) {
            profilesDisabled++;
            continue;
        }

        if (!demandProfileActiveForPhase(profile, serverPhase)) {
            profilesInactivePhase++;
            continue;
        }

        DemandStateSimulationResult result;
        result.profileKey = profile.key;
        result.desiredReserve = desiredReserve.contains(profile.key) ?
            static_cast<uint64>(desiredReserve.get(profile.key)) : 0;
        result.aiConceptualSupply = estimateConceptualDemandStateSupply(
            profile.key,
            conceptualResourceNames,
            conceptualAmounts,
            result.supplyConfidence,
            result.supplyLabels);
        result.marketObservedSupply = marketQuantities.contains(profile.key) ?
            marketQuantities.get(profile.key) : 0;
        float lowThreshold = lowStockThreshold.contains(profile.key) ?
            lowStockThreshold.get(profile.key) : 0.35f;
        float criticalThreshold = criticalStockThreshold.contains(profile.key) ?
            criticalStockThreshold.get(profile.key) : 0.10f;

        for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            ResourceIntelligenceEntry entry = entries.get(entryIndex);
            DemandProfileMatch match =
                evaluateDemandProfileResource(entry, profile, 1.f, 100);

            if (!match.eligible ||
                    (result.hasActiveOpportunity &&
                     match.demandScore <= result.activeMatch.demandScore)) {
                continue;
            }

            result.hasActiveOpportunity = true;
            result.activeResource = entry;
            result.activeMatch = match;
        }

        calculateDemandStatePressure(
            result,
            lowThreshold,
            criticalThreshold,
            shortageWeight,
            activeOpportunityWeight,
            surplusDampening);

        if (!result.hasActiveOpportunity) {
            profilesNoEligibleResource++;
            continue;
        }

        if (result.pressureScore < minimumPressureThreshold) {
            profilesBelowPressure++;
            continue;
        }

        pressureResults.add(result);
    }
}

static DemandWeightedPlanSelection selectDemandWeightedMinerPlanForValidation(
        const Vector<ResourceIntelligenceEntry>& entries,
        const Vector<DemandProfileDefinition>& profiles,
        const Vector<DemandStateSimulationResult>& pressureResults,
        VectorMap<String, int>& assignmentsByProfile,
        const String& zoneName,
        int samePlanetBonus,
        int travelPenalty,
        int maxMinersPerProfile,
        float strongPressureRatio) {
    DemandWeightedPlanSelection selection;
    DemandWeightedMinerCandidate bestWithinCap;
    DemandWeightedMinerCandidate strongestCapped;
    DemandWeightedMinerCandidate secondStrongestCapped;

    for (int resultIndex = 0; resultIndex < pressureResults.size(); ++resultIndex) {
        DemandStateSimulationResult result = pressureResults.get(resultIndex);
        DemandProfileDefinition profile;

        if (!findDemandProfileDefinition(profiles, result.profileKey, profile))
            continue;

        DemandWeightedMinerTarget target =
            selectDemandWeightedMinerTarget(
                entries, profile, zoneName, samePlanetBonus, travelPenalty);

        if (!target.isValid())
            continue;

        int assignmentCount = assignmentsByProfile.contains(result.profileKey) ?
            assignmentsByProfile.get(result.profileKey) : 0;
        DemandWeightedMinerCandidate candidate;
        candidate.resultIndex = resultIndex;
        candidate.target = target;
        candidate.existingAssignments = assignmentCount;
        candidate.exceedsProfileCap = assignmentCount >= maxMinersPerProfile;
        candidate.locationAdjustedPressure = result.pressureScore +
            (target.samePlanet ?
                static_cast<float>(samePlanetBonus) :
                -static_cast<float>(travelPenalty));
        candidate.balancedScore =
            candidate.locationAdjustedPressure /
            static_cast<float>(assignmentCount + 1);

        if (!candidate.exceedsProfileCap &&
                (!bestWithinCap.isValid() ||
                 candidate.balancedScore > bestWithinCap.balancedScore)) {
            bestWithinCap = candidate;
        }

        if (candidate.exceedsProfileCap &&
                (!strongestCapped.isValid() ||
                 candidate.locationAdjustedPressure >
                    strongestCapped.locationAdjustedPressure)) {
            secondStrongestCapped = strongestCapped;
            strongestCapped = candidate;
        } else if (candidate.exceedsProfileCap &&
                (!secondStrongestCapped.isValid() ||
                 candidate.locationAdjustedPressure >
                    secondStrongestCapped.locationAdjustedPressure)) {
            secondStrongestCapped = candidate;
        }
    }

    DemandWeightedMinerCandidate selected;
    bool strongPressureOverflow = false;

    if (bestWithinCap.isValid()) {
        selected = bestWithinCap;

        if (strongestCapped.isValid()) {
            DemandStateSimulationResult overflowResult =
                pressureResults.get(strongestCapped.resultIndex);
            DemandStateSimulationResult boundedResult =
                pressureResults.get(bestWithinCap.resultIndex);

            if (overflowResult.pressureScore >=
                    boundedResult.pressureScore * strongPressureRatio) {
                selected = strongestCapped;
                strongPressureOverflow = true;
            }
        }
    } else if (strongestCapped.isValid()) {
        DemandStateSimulationResult strongestResult =
            pressureResults.get(strongestCapped.resultIndex);
        bool overflowJustified = !secondStrongestCapped.isValid();

        if (secondStrongestCapped.isValid()) {
            DemandStateSimulationResult secondResult =
                pressureResults.get(secondStrongestCapped.resultIndex);
            overflowJustified = strongestResult.pressureScore >=
                secondResult.pressureScore * strongPressureRatio;
        }

        if (overflowJustified) {
            selected = strongestCapped;
            strongPressureOverflow = true;
        }
    }

    if (!selected.isValid())
        return selection;

    selection.valid = true;
    selection.selected = selected;
    selection.selectedResult = pressureResults.get(selected.resultIndex);
    findDemandProfileDefinition(
        profiles, selection.selectedResult.profileKey, selection.selectedProfile);
    selection.selectedResource = entries.get(selected.target.resourceIndex);
    selection.strongPressureOverflow = strongPressureOverflow;

    int newAssignmentCount = selected.existingAssignments + 1;
    assignmentsByProfile.put(selection.selectedResult.profileKey, newAssignmentCount);

    if (strongPressureOverflow) {
        selection.assignmentReason =
            "strong pressure justified profile cap overflow";
    } else if (selected.existingAssignments > 0) {
        selection.assignmentReason = "load-balanced demand pressure";
    } else {
        selection.assignmentReason = "highest demand pressure";
    }

    selection.assignmentReason += selected.target.samePlanet ?
        String("; same-planet opportunity") :
        String("; travel-required opportunity");

    return selection;
}

struct MinerDensityTargetCandidate {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float density = 0.f;
    float distance = 0.f;
    float adjustedScore = 0.f;
    float legacyAdjustedScore = 0.f;
    float reachabilityAdjustedScore = 0.f;
    float reachabilityConfidence = 0.f;
    String reachabilityMemoryKey;
    int searchRadius = 0;
    int samplesChecked = 0;
    bool navmeshChecked = false;

    bool isValid() const {
        return density > 0.f && searchRadius > 0;
    }

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct MinerDensityTargetDiagnostics {
    int candidateCount = 0;
    int acceptedCandidateRank = 0;
    int acceptableCandidateCount = 0;
    String searchedRadii;
    String rejectReason = "noValidCandidate";
    String bestRejectedReason;
    MinerDensityTargetCandidate bestObservedCandidate;
    MinerDensityTargetCandidate bestRejectedCandidate;

    bool hasBestRejectedCandidate() const {
        return bestRejectedCandidate.density > 0.f;
    }
};

struct NavAreaDensitySample {
    String cacheKey;
    String planet;
    String navAreaName;
    String sourceRole;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    String lastValidationResult;
    uint64 validationTimestampMs = 0;
    int useCount = 0;
    int rejectionCount = 0;
    float confidence = 0.f;
    uint64 generatedAtMs = 0;

    bool isValid() const {
        return !cacheKey.isEmpty() && !planet.isEmpty() &&
            !navAreaName.isEmpty() && generatedAtMs > 0;
    }

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct NavAreaDensitySelectionDiagnostics {
    bool enabled = false;
    bool shadowMode = true;
    bool activeMode = false;
    bool runtimeOnly = true;
    int navAreaCandidatesConsidered = 0;
    int navAreaSamplesGenerated = 0;
    int navAreaSampleCacheHits = 0;
    int navAreaSampleCacheMisses = 0;
    int navAreaSamplesValidated = 0;
    int navAreaSamplesRejected = 0;
    int densityCandidatesConsidered = 0;
    float densitySelectedCandidateScore = 0.f;
    String densitySelectionMode = "disabled";
    int pathValidationBudgetUsed = 0;
    int pathValidationSkippedBudget = 0;
    int fallbackToLegacySamplingCount = 0;
    int directFallbackPathCount = 0;
    int confirmedPathCount = 0;
    int indoorCandidateRejectedCount = 0;
    int sampleAttemptsUsed = 0;
    int sampleAttemptBudget = 0;
    int sampleBudgetExhaustedCount = 0;
    int pathValidationBudget = 0;
    uint64 updatedAtMs = 0;
    VectorMap<String, int> rejectionReasons;
};

static Mutex navAreaDensitySelectionMutex;
static Vector<NavAreaDensitySample> navAreaDensitySampleCache;
static NavAreaDensitySelectionDiagnostics navAreaDensityDiagnostics;

static void addNavAreaDensityRejectionReasonNoLock(const String& reason) {
    String key = reason.isEmpty() ? String("unknown") : reason;

    if (navAreaDensityDiagnostics.rejectionReasons.contains(key)) {
        navAreaDensityDiagnostics.rejectionReasons.put(
            key, navAreaDensityDiagnostics.rejectionReasons.get(key) + 1);
    } else {
        navAreaDensityDiagnostics.rejectionReasons.put(key, 1);
    }
}

static void resetNavAreaDensitySelectionDiagnostics(
        bool enabled,
        bool shadowMode,
        int sampleAttemptBudget,
        int pathValidationBudget) {
    Locker locker(&navAreaDensitySelectionMutex);
    navAreaDensityDiagnostics = NavAreaDensitySelectionDiagnostics();
    navAreaDensityDiagnostics.enabled = enabled;
    navAreaDensityDiagnostics.shadowMode = shadowMode;
    navAreaDensityDiagnostics.activeMode = enabled && !shadowMode;
    navAreaDensityDiagnostics.sampleAttemptBudget = sampleAttemptBudget;
    navAreaDensityDiagnostics.pathValidationBudget = pathValidationBudget;
    navAreaDensityDiagnostics.updatedAtMs = System::getMiliTime();
}

static String classifyNavAreaDensityRole(NavArea* area) {
    if (area == nullptr)
        return "unknown";

    String name = area->getMeshName().toLowerCase();

    if (name.isEmpty())
        name = area->getAreaName().toLowerCase();

    if (area->isCityRegion() || name.indexOf("city") >= 0 ||
            name.indexOf("coronet") >= 0 || name.indexOf("theed") >= 0 ||
            name.indexOf("mos_") >= 0)
        return "city";

    if (area->isNamedRegion() || name.indexOf("poi") >= 0 ||
            name.indexOf("outpost") >= 0 || name.indexOf("retreat") >= 0 ||
            name.indexOf("fort") >= 0 || name.indexOf("ruins") >= 0)
        return "poi_region";

    return "region";
}

static bool isGenericInteriorNavAreaName(const String& name) {
    String lowered = name.toLowerCase();
    return lowered.indexOf("interior") >= 0 ||
        lowered.indexOf("building") >= 0 ||
        lowered.indexOf("cell") >= 0;
}

static String navAreaDensityCacheKey(const String& planet, const String& areaName) {
    return planet + ":" + areaName;
}

static String getNavAreaDensityName(NavArea* area) {
    if (area == nullptr)
        return "unknown";

    String name = area->getMeshName();

    if (name.isEmpty())
        name = area->getAreaName();

    if (name.isEmpty())
        name = String::valueOf(area->getObjectID());

    return name;
}

static String getContainingNavAreaDensityName(
        Zone* zone, float x, float y, String& sourceRole) {
    sourceRole = "unknown";

    if (zone == nullptr)
        return "";

    SortedVector<ManagedReference<NavArea*>> areas;
    zone->getInRangeNavMeshes(x, y, &areas, false);

    for (const auto& area : areas) {
        if (area == nullptr || !area->containsPoint(x, y))
            continue;

        sourceRole = classifyNavAreaDensityRole(area);
        return getNavAreaDensityName(area);
    }

    return "";
}

static bool findCachedNavAreaDensitySampleNoLock(
        const String& cacheKey,
        uint64 nowMs,
        uint64 ttlMs,
        NavAreaDensitySample& sample) {
    int bestIndex = -1;
    float bestScore = -1000000.f;

    for (int i = 0; i < navAreaDensitySampleCache.size(); ++i) {
        NavAreaDensitySample candidate = navAreaDensitySampleCache.get(i);

        if (candidate.cacheKey != cacheKey)
            continue;

        if (ttlMs > 0 && candidate.generatedAtMs > 0 &&
                nowMs > candidate.generatedAtMs + ttlMs)
            continue;

        float score = candidate.confidence * 100.f -
            static_cast<float>(candidate.useCount * 5) -
            static_cast<float>(candidate.rejectionCount * 20);

        if (bestIndex < 0 || score > bestScore) {
            bestIndex = i;
            bestScore = score;
        }
    }

    if (bestIndex < 0)
        return false;

    sample = navAreaDensitySampleCache.get(bestIndex);
    return true;
}

static void storeNavAreaDensitySampleNoLock(
        const NavAreaDensitySample& sample,
        int maxSamplesPerArea) {
    int sameAreaCount = 0;
    int oldestIndex = -1;
    uint64 oldestGeneratedAt = 0;

    for (int i = 0; i < navAreaDensitySampleCache.size(); ++i) {
        NavAreaDensitySample existing = navAreaDensitySampleCache.get(i);

        if (existing.cacheKey != sample.cacheKey)
            continue;

        sameAreaCount++;

        if (oldestIndex < 0 || existing.generatedAtMs < oldestGeneratedAt) {
            oldestIndex = i;
            oldestGeneratedAt = existing.generatedAtMs;
        }
    }

    if (sameAreaCount >= maxSamplesPerArea && oldestIndex >= 0)
        navAreaDensitySampleCache.remove(oldestIndex);

    navAreaDensitySampleCache.add(sample);
}

static void markNavAreaDensitySampleUseNoLock(
        const NavAreaDensitySample& sample,
        bool accepted) {
    for (int i = 0; i < navAreaDensitySampleCache.size(); ++i) {
        NavAreaDensitySample existing = navAreaDensitySampleCache.get(i);

        if (existing.cacheKey != sample.cacheKey ||
                existing.x != sample.x || existing.y != sample.y)
            continue;

        if (accepted)
            existing.useCount++;
        else
            existing.rejectionCount++;

        navAreaDensitySampleCache.set(i, existing);
        return;
    }
}

static ManagedReference<ResourceSpawn*> getResourceSpawnForDensitySelection(
        const ResourceIntelligenceEntry& resource) {
    ManagedReference<ResourceSpawn*> resourceSpawn;

    if (resource.name.isEmpty())
        return resourceSpawn;

    ZoneServer* zoneServer = ServerCore::getZoneServer();

    if (zoneServer == nullptr)
        return resourceSpawn;

    ManagedReference<ResourceManager*> resourceManager =
        zoneServer->getResourceManager();

    if (resourceManager == nullptr)
        return resourceSpawn;

    ReadLocker managerLocker(resourceManager);
    ResourceSpawner* spawner = resourceManager->getResourceSpawner();
    ResourceMap* resourceMap =
        spawner == nullptr ? nullptr : spawner->getResourceMap();
    String resourceKey = resource.name.toLowerCase();

    if (resourceMap != nullptr && resourceMap->contains(resourceKey))
        resourceSpawn = resourceMap->get(resourceKey);

    return resourceSpawn;
}

static bool evaluateNavAreaDensitySelection(
        uint64 minerID,
        const String& profileKey,
        const ResourceIntelligenceEntry& resource,
        Zone* zone,
        const Vector3& minerPosition,
        bool enableSelection,
        bool shadowMode,
        int cacheTtlSeconds,
        int maxSamplesPerArea,
        int maxSampleAttemptsPerCycle,
        int maxPathValidationsPerCycle,
        bool avoidGenericInteriors,
        bool preferCityAndPoiRegions,
        float minAcceptableDensity,
        float distancePenaltyPerMeter,
        MinerDensityTargetCandidate& navAreaCandidate,
        String& selectionMode,
        String& reason,
        String& sourceAreaName,
        String& sourceRole) {
    selectionMode = "disabled";
    reason = "featureDisabled";
    sourceAreaName = "none";
    sourceRole = "none";

    if (!enableSelection && !shadowMode)
        return false;

    if (zone == nullptr) {
        Locker locker(&navAreaDensitySelectionMutex);
        navAreaDensityDiagnostics.fallbackToLegacySamplingCount++;
        addNavAreaDensityRejectionReasonNoLock("noZone");
        return false;
    }

    String zoneName = zone->getZoneName();
    SortedVector<ManagedReference<NavArea*>> areas;
    zone->getInRangeNavMeshes(
        minerPosition.getX(), minerPosition.getY(), &areas, false);

    if (areas.size() == 0) {
        Locker locker(&navAreaDensitySelectionMutex);
        navAreaDensityDiagnostics.navAreaSampleCacheMisses++;
        navAreaDensityDiagnostics.fallbackToLegacySamplingCount++;
        navAreaDensityDiagnostics.densitySelectionMode = "legacy_no_navarea";
        addNavAreaDensityRejectionReasonNoLock("noNavAreaAtOrigin");
        reason = "noNavAreaAtOrigin";
        selectionMode = "legacy_no_navarea";
        return false;
    }

    ManagedReference<ResourceSpawn*> resourceSpawn =
        getResourceSpawnForDensitySelection(resource);

    if (resourceSpawn == nullptr) {
        Locker locker(&navAreaDensitySelectionMutex);
        navAreaDensityDiagnostics.fallbackToLegacySamplingCount++;
        navAreaDensityDiagnostics.densitySelectionMode =
            "legacy_no_density_map";
        addNavAreaDensityRejectionReasonNoLock("noDensityMap");
        reason = "noDensityMap";
        selectionMode = "legacy_no_density_map";
        return false;
    }

    uint64 nowMs = System::getMiliTime();
    uint64 ttlMs = static_cast<uint64>(cacheTtlSeconds) * 1000;
    Vector<NavAreaDensitySample> samplesToScore;

    for (const auto& area : areas) {
        if (area == nullptr)
            continue;

        String areaName = getNavAreaDensityName(area);
        String role = classifyNavAreaDensityRole(area);

        {
            Locker locker(&navAreaDensitySelectionMutex);
            navAreaDensityDiagnostics.navAreaCandidatesConsidered++;
        }

        if (avoidGenericInteriors && isGenericInteriorNavAreaName(areaName)) {
            Locker locker(&navAreaDensitySelectionMutex);
            navAreaDensityDiagnostics.indoorCandidateRejectedCount++;
            addNavAreaDensityRejectionReasonNoLock("genericInterior");
            continue;
        }

        if (preferCityAndPoiRegions &&
                role != "city" && role != "poi_region" &&
                areas.size() > 1)
            continue;

        String cacheKey = navAreaDensityCacheKey(zoneName, areaName);
        NavAreaDensitySample cachedSample;
        bool hasCachedSample = false;

        {
            Locker locker(&navAreaDensitySelectionMutex);
            hasCachedSample = findCachedNavAreaDensitySampleNoLock(
                cacheKey, nowMs, ttlMs, cachedSample);

            if (hasCachedSample)
                navAreaDensityDiagnostics.navAreaSampleCacheHits++;
            else
                navAreaDensityDiagnostics.navAreaSampleCacheMisses++;
        }

        if (hasCachedSample) {
            samplesToScore.add(cachedSample);
            continue;
        }

        for (int sampleIndex = 0; sampleIndex < maxSamplesPerArea; ++sampleIndex) {
            {
                Locker locker(&navAreaDensitySelectionMutex);

                if (navAreaDensityDiagnostics.sampleAttemptsUsed >=
                        maxSampleAttemptsPerCycle) {
                    navAreaDensityDiagnostics.sampleBudgetExhaustedCount++;
                    addNavAreaDensityRejectionReasonNoLock("sampleBudgetExhausted");
                    break;
                }

                navAreaDensityDiagnostics.sampleAttemptsUsed++;
            }

            Vector3 areaCenter = area->getWorldPosition();
            float areaRadius = area->getRadius();

            if (areaRadius < 16.f)
                areaRadius = 16.f;
            else if (areaRadius > 512.f)
                areaRadius = 512.f;

            Sphere sphere(areaCenter, areaRadius);
            Vector3 point;

            if (!PathFinderManager::instance()->getSpawnPointInArea(
                    sphere, zone, point, false)) {
                Locker locker(&navAreaDensitySelectionMutex);
                navAreaDensityDiagnostics.navAreaSamplesRejected++;
                addNavAreaDensityRejectionReasonNoLock("detourSampleFailed");
                continue;
            }

            if (!zone->isWithinBoundaries(point)) {
                Locker locker(&navAreaDensitySelectionMutex);
                navAreaDensityDiagnostics.navAreaSamplesRejected++;
                addNavAreaDensityRejectionReasonNoLock("outsideZoneBoundaries");
                continue;
            }

            String pointRole;
            String pointAreaName = getContainingNavAreaDensityName(
                zone, point.getX(), point.getY(), pointRole);

            if (pointAreaName.isEmpty()) {
                Locker locker(&navAreaDensitySelectionMutex);
                navAreaDensityDiagnostics.navAreaSamplesRejected++;
                addNavAreaDensityRejectionReasonNoLock("rawFallbackNotAccepted");
                continue;
            }

            NavAreaDensitySample sample;
            sample.planet = zoneName;
            sample.navAreaName = pointAreaName;
            sample.sourceRole = pointRole;
            sample.cacheKey = navAreaDensityCacheKey(zoneName, pointAreaName);
            sample.x = point.getX();
            sample.y = point.getY();
            sample.z = point.getZ();
            sample.lastValidationResult = "detour_random_point";
            sample.validationTimestampMs = nowMs;
            sample.confidence = 0.75f;
            sample.generatedAtMs = nowMs;

            {
                Locker locker(&navAreaDensitySelectionMutex);
                navAreaDensityDiagnostics.navAreaSamplesGenerated++;
                navAreaDensityDiagnostics.navAreaSamplesValidated++;
                navAreaDensityDiagnostics.confirmedPathCount++;
                storeNavAreaDensitySampleNoLock(sample, maxSamplesPerArea);
            }

            samplesToScore.add(sample);
        }
    }

    if (samplesToScore.size() == 0) {
        Locker locker(&navAreaDensitySelectionMutex);
        navAreaDensityDiagnostics.fallbackToLegacySamplingCount++;
        navAreaDensityDiagnostics.densitySelectionMode =
            "legacy_no_navarea_samples";
        addNavAreaDensityRejectionReasonNoLock("noNavAreaSamples");
        reason = "noNavAreaSamples";
        selectionMode = "legacy_no_navarea_samples";
        return false;
    }

    NavAreaDensitySample bestSample;
    float bestScore = -1000000.f;
    float bestDensity = 0.f;
    float bestDistance = 0.f;

    {
        Locker spawnLocker(resourceSpawn);

        if (!resourceSpawn->inShift()) {
            Locker locker(&navAreaDensitySelectionMutex);
            navAreaDensityDiagnostics.fallbackToLegacySamplingCount++;
            navAreaDensityDiagnostics.densitySelectionMode =
                "legacy_no_density_map";
            addNavAreaDensityRejectionReasonNoLock("resourceNotInShift");
            reason = "resourceNotInShift";
            selectionMode = "legacy_no_density_map";
            return false;
        }

        for (int sampleIndex = 0; sampleIndex < samplesToScore.size(); ++sampleIndex) {
            NavAreaDensitySample sample = samplesToScore.get(sampleIndex);
            float density = resourceSpawn->getDensityAt(zoneName, sample.x, sample.y);
            float dx = sample.x - minerPosition.getX();
            float dy = sample.y - minerPosition.getY();
            float dz = sample.z - minerPosition.getZ();
            float distance = Math::sqrt(dx * dx + dy * dy + dz * dz);

            {
                Locker locker(&navAreaDensitySelectionMutex);
                navAreaDensityDiagnostics.densityCandidatesConsidered++;
            }

            if (density < minAcceptableDensity) {
                Locker locker(&navAreaDensitySelectionMutex);
                navAreaDensityDiagnostics.navAreaSamplesRejected++;
                markNavAreaDensitySampleUseNoLock(sample, false);
                addNavAreaDensityRejectionReasonNoLock("belowMinDensity");
                continue;
            }

            float roleBonus = 0.f;
            if (sample.sourceRole == "city")
                roleBonus = 60.f;
            else if (sample.sourceRole == "poi_region")
                roleBonus = 35.f;

            float score = density * 1000.f -
                distance * distancePenaltyPerMeter +
                sample.confidence * 100.f + roleBonus -
                static_cast<float>(sample.useCount * 5) -
                static_cast<float>(sample.rejectionCount * 20);

            if (!bestSample.isValid() || score > bestScore) {
                bestSample = sample;
                bestScore = score;
                bestDensity = density;
                bestDistance = distance;
            }
        }
    }

    if (!bestSample.isValid()) {
        Locker locker(&navAreaDensitySelectionMutex);
        navAreaDensityDiagnostics.fallbackToLegacySamplingCount++;
        navAreaDensityDiagnostics.densitySelectionMode =
            "legacy_no_density_match";
        addNavAreaDensityRejectionReasonNoLock("noDensityMatch");
        reason = "noDensityMatch";
        selectionMode = "legacy_no_density_match";
        return false;
    }

    navAreaCandidate.x = bestSample.x;
    navAreaCandidate.y = bestSample.y;
    navAreaCandidate.z = bestSample.z;
    navAreaCandidate.density = bestDensity;
    navAreaCandidate.distance = bestDistance;
    navAreaCandidate.adjustedScore = bestScore;
    navAreaCandidate.searchRadius = 1;
    navAreaCandidate.samplesChecked = samplesToScore.size();
    navAreaCandidate.navmeshChecked = true;
    sourceAreaName = bestSample.navAreaName;
    sourceRole = bestSample.sourceRole;
    selectionMode = enableSelection && !shadowMode ?
        String("active_navarea_candidate") : String("shadow_navarea_candidate");
    reason = enableSelection && !shadowMode ?
        String("active NavArea density candidate selected") :
        String("shadow NavArea density candidate retained for diagnostics only");

    {
        Locker locker(&navAreaDensitySelectionMutex);
        navAreaDensityDiagnostics.densitySelectedCandidateScore = bestScore;
        navAreaDensityDiagnostics.densitySelectionMode = selectionMode;
        markNavAreaDensitySampleUseNoLock(bestSample, true);
    }

    return true;
}

static JSONSerializationType buildNavAreaDensitySelectionDiagnosticsJSON() {
    Locker locker(&navAreaDensitySelectionMutex);
    JSONSerializationType result = JSONSerializationType::object();
    result["enabled"] = navAreaDensityDiagnostics.enabled;
    result["shadowMode"] = navAreaDensityDiagnostics.shadowMode;
    result["activeMode"] = navAreaDensityDiagnostics.activeMode;
    result["mode"] = navAreaDensityDiagnostics.activeMode ?
        String("active") :
        (navAreaDensityDiagnostics.shadowMode ?
            String("shadow-read-only") : String("disabled"));
    result["status"] =
        navAreaDensityDiagnostics.updatedAtMs == 0 ? String("no_data") :
        (navAreaDensityDiagnostics.fallbackToLegacySamplingCount > 0 ?
            String("watch") : String("ready"));
    result["readOnly"] = !navAreaDensityDiagnostics.activeMode;
    result["runtimeOnly"] = true;
    result["navAreaCandidatesConsidered"] =
        navAreaDensityDiagnostics.navAreaCandidatesConsidered;
    result["navAreaSamplesGenerated"] =
        navAreaDensityDiagnostics.navAreaSamplesGenerated;
    result["navAreaSampleCacheHits"] =
        navAreaDensityDiagnostics.navAreaSampleCacheHits;
    result["navAreaSampleCacheMisses"] =
        navAreaDensityDiagnostics.navAreaSampleCacheMisses;
    result["navAreaSamplesValidated"] =
        navAreaDensityDiagnostics.navAreaSamplesValidated;
    result["navAreaSamplesRejected"] =
        navAreaDensityDiagnostics.navAreaSamplesRejected;
    result["densityCandidatesConsidered"] =
        navAreaDensityDiagnostics.densityCandidatesConsidered;
    result["densitySelectedCandidateScore"] =
        Math::getPrecision(
            navAreaDensityDiagnostics.densitySelectedCandidateScore, 1);
    result["densitySelectionMode"] =
        navAreaDensityDiagnostics.densitySelectionMode;
    result["pathValidationBudgetUsed"] =
        navAreaDensityDiagnostics.pathValidationBudgetUsed;
    result["pathValidationSkippedBudget"] =
        navAreaDensityDiagnostics.pathValidationSkippedBudget;
    result["fallbackToLegacySamplingCount"] =
        navAreaDensityDiagnostics.fallbackToLegacySamplingCount;
    result["directFallbackPathCount"] =
        navAreaDensityDiagnostics.directFallbackPathCount;
    result["confirmedPathCount"] =
        navAreaDensityDiagnostics.confirmedPathCount;
    result["indoorCandidateRejectedCount"] =
        navAreaDensityDiagnostics.indoorCandidateRejectedCount;
    result["sampleAttemptsUsed"] =
        navAreaDensityDiagnostics.sampleAttemptsUsed;
    result["sampleAttemptBudget"] =
        navAreaDensityDiagnostics.sampleAttemptBudget;
    result["sampleBudgetExhaustedCount"] =
        navAreaDensityDiagnostics.sampleBudgetExhaustedCount;
    result["pathValidationBudget"] =
        navAreaDensityDiagnostics.pathValidationBudget;
    result["sampleCacheSize"] = navAreaDensitySampleCache.size();
    result["behaviorChanged"] = navAreaDensityDiagnostics.activeMode;
    result["persistenceChanged"] = false;
    result["realResourceCreated"] = false;
    result["resourceContainerCreated"] = false;
    result["inventoryMutated"] = false;
    result["economyMutated"] = false;

    JSONSerializationType reasons = JSONSerializationType::array();
    for (int i = 0; i < navAreaDensityDiagnostics.rejectionReasons.size(); ++i) {
        JSONSerializationType row = JSONSerializationType::object();
        row["reason"] = navAreaDensityDiagnostics.rejectionReasons.elementAt(i).getKey();
        row["count"] = navAreaDensityDiagnostics.rejectionReasons.get(i);
        reasons.push_back(row);
    }
    result["navAreaRejectionReasons"] = reasons;

    JSONSerializationType samples = JSONSerializationType::array();
    for (int i = 0; i < navAreaDensitySampleCache.size() && i < 12; ++i) {
        NavAreaDensitySample sample = navAreaDensitySampleCache.get(i);
        JSONSerializationType row = JSONSerializationType::object();
        row["planet"] = sample.planet;
        row["navAreaName"] = sample.navAreaName;
        row["sourceRole"] = sample.sourceRole;
        row["x"] = Math::getPrecision(sample.x, 1);
        row["y"] = Math::getPrecision(sample.y, 1);
        row["z"] = Math::getPrecision(sample.z, 1);
        row["lastValidationResult"] = sample.lastValidationResult;
        row["useCount"] = sample.useCount;
        row["rejectionCount"] = sample.rejectionCount;
        row["confidence"] = Math::getPrecision(sample.confidence, 2);
        row["ageSeconds"] =
            sample.generatedAtMs > 0 &&
            navAreaDensityDiagnostics.updatedAtMs > sample.generatedAtMs ?
            (navAreaDensityDiagnostics.updatedAtMs - sample.generatedAtMs) / 1000 : 0;
        samples.push_back(row);
    }
    result["samples"] = samples;

    return result;
}

static bool densityCandidateIndexUsed(const Vector<int>& usedIndexes, int index) {
    for (int i = 0; i < usedIndexes.size(); ++i) {
        if (usedIndexes.get(i) == index)
            return true;
    }

    return false;
}

static bool isPointInAnyNavmesh(Zone* zone, float x, float y) {
    if (zone == nullptr)
        return false;

    SortedVector<ManagedReference<NavArea*>> areas;
    zone->getInRangeNavMeshes(x, y, &areas, false);

    for (const auto& area : areas) {
        if (area != nullptr && area->containsPoint(x, y))
            return true;
    }

    return false;
}

static bool isDensityCandidateInRequiredNavmesh(
        Zone* zone,
        float x,
        float y,
        bool requireNavmesh,
        bool minerInNavmesh,
        int& navmeshChecks,
        int maxNavmeshChecks) {
    if (!requireNavmesh || !minerInNavmesh)
        return true;

    if (navmeshChecks >= maxNavmeshChecks)
        return false;

    ++navmeshChecks;

    return isPointInAnyNavmesh(zone, x, y);
}

struct ReachabilityMemoryBucket {
    String key;
    String planet;
    String resourceName;
    String resourceType;
    String profileKey;
    String targetSource;
    int bucketX = 0;
    int bucketY = 0;
    int attempts = 0;
    int verifiedPathCount = 0;
    int directFallbackUnverifiedCount = 0;
    int activationCount = 0;
    int sampleCompleteCount = 0;
    int coverageRetainedCount = 0;
    int stationedSampleCount = 0;
    uint64 stationedDurationSeconds = 0;
    uint64 firstSeenAtMs = 0;
    uint64 lastSeenAtMs = 0;
    uint64 lastVerifiedAtMs = 0;
    uint64 lastRejectedAtMs = 0;
    uint64 lastActivatedAtMs = 0;
    uint64 lastSampleCompleteAtMs = 0;
    uint64 lastCoverageRetainedAtMs = 0;
    float pathDistanceTotal = 0.f;
    int pathDistanceSamples = 0;
    float densityTotal = 0.f;
    int densitySamples = 0;

    bool hasUsefulHistory() const {
        return verifiedPathCount > 0 || sampleCompleteCount > 0 ||
            directFallbackUnverifiedCount > 0 || attempts > 0;
    }

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct ReachabilityMemoryDiagnostics {
    int shadowWouldSelectDifferentCount = 0;
    int shadowPreferredVerifiedHistoryCount = 0;
    int activePreferenceUsedCount = 0;
    int activePreferenceFallbackCount = 0;
};

static Mutex reachabilityMemoryMutex;
static VectorMap<String, ReachabilityMemoryBucket> reachabilityMemoryBuckets;
static ReachabilityMemoryDiagnostics reachabilityMemoryDiagnostics;

static int reachabilityCoordinateBucket(float value, int bucketSizeMeters) {
    int bucketSize = bucketSizeMeters <= 0 ? 128 : bucketSizeMeters;
    float divided = value / static_cast<float>(bucketSize);

    if (divided >= 0.f)
        return static_cast<int>(divided);

    return static_cast<int>(divided) - 1;
}

static String buildReachabilityMemoryKey(
        const String& planet,
        const String& resourceName,
        const String& resourceType,
        const String& profileKey,
        const String& targetSource,
        float x,
        float y,
        int bucketSizeMeters) {
    int bucketX = reachabilityCoordinateBucket(x, bucketSizeMeters);
    int bucketY = reachabilityCoordinateBucket(y, bucketSizeMeters);

    return planet + "|" + resourceName + "|" + resourceType + "|" +
        profileKey + "|" + targetSource + "|" + String::valueOf(bucketX) +
        "|" + String::valueOf(bucketY);
}

static float reachabilityMemoryConfidence(
        const ReachabilityMemoryBucket& bucket) {
    if (bucket.attempts <= 0)
        return 0.f;

    float success =
        static_cast<float>(bucket.verifiedPathCount + bucket.sampleCompleteCount);
    float failure =
        static_cast<float>(bucket.directFallbackUnverifiedCount);
    float confidence = (success + 1.f) / (success + failure + 2.f);

    if (bucket.coverageRetainedCount > 0)
        confidence += Math::min(
            0.25f,
            static_cast<float>(bucket.coverageRetainedCount) * 0.05f);

    return Math::getPrecision(Math::min(confidence, 1.f), 3);
}

static float reachabilityMemoryAveragePathDistance(
        const ReachabilityMemoryBucket& bucket) {
    if (bucket.pathDistanceSamples <= 0)
        return 0.f;

    return Math::getPrecision(
        bucket.pathDistanceTotal /
            static_cast<float>(bucket.pathDistanceSamples),
        1);
}

static float reachabilityMemoryAverageDensity(
        const ReachabilityMemoryBucket& bucket) {
    if (bucket.densitySamples <= 0)
        return 0.f;

    return Math::getPrecision(
        bucket.densityTotal / static_cast<float>(bucket.densitySamples),
        3);
}

static void pruneReachabilityMemoryNoLock(
        uint64 nowMs,
        uint64 ttlMs,
        int maxRows) {
    for (int i = reachabilityMemoryBuckets.size() - 1; i >= 0; --i) {
        ReachabilityMemoryBucket bucket =
            reachabilityMemoryBuckets.elementAt(i).getValue();

        if (ttlMs > 0 && bucket.lastSeenAtMs > 0 &&
                nowMs > bucket.lastSeenAtMs + ttlMs) {
            reachabilityMemoryBuckets.remove(i);
        }
    }

    int boundedMaxRows = maxRows <= 0 ? 5000 : maxRows;

    while (reachabilityMemoryBuckets.size() > boundedMaxRows) {
        int oldestIndex = 0;
        uint64 oldestSeenAtMs =
            reachabilityMemoryBuckets.elementAt(0).getValue().lastSeenAtMs;

        for (int i = 1; i < reachabilityMemoryBuckets.size(); ++i) {
            uint64 seenAtMs =
                reachabilityMemoryBuckets.elementAt(i).getValue().lastSeenAtMs;

            if (seenAtMs < oldestSeenAtMs) {
                oldestSeenAtMs = seenAtMs;
                oldestIndex = i;
            }
        }

        reachabilityMemoryBuckets.remove(oldestIndex);
    }
}

static void updateReachabilityMemoryBucket(
        const String& key,
        const String& planet,
        const String& resourceName,
        const String& resourceType,
        const String& profileKey,
        const String& targetSource,
        float x,
        float y,
        float density,
        float pathDistance,
        const String& eventName,
        int bucketSizeMeters,
        int ttlSeconds,
        int maxRows,
        uint64 stationedDurationSeconds = 0) {
    if (key.isEmpty())
        return;

    uint64 nowMs = System::getMiliTime();
    uint64 ttlMs = static_cast<uint64>(ttlSeconds <= 0 ? 1800 : ttlSeconds) * 1000;
    Locker locker(&reachabilityMemoryMutex);
    ReachabilityMemoryBucket bucket;

    if (reachabilityMemoryBuckets.contains(key)) {
        bucket = reachabilityMemoryBuckets.get(key);
    } else {
        bucket.key = key;
        bucket.planet = planet;
        bucket.resourceName = resourceName;
        bucket.resourceType = resourceType;
        bucket.profileKey = profileKey;
        bucket.targetSource = targetSource;
        bucket.bucketX = reachabilityCoordinateBucket(x, bucketSizeMeters);
        bucket.bucketY = reachabilityCoordinateBucket(y, bucketSizeMeters);
        bucket.firstSeenAtMs = nowMs;
    }

    bucket.lastSeenAtMs = nowMs;

    if (density > 0.f) {
        bucket.densityTotal += density;
        bucket.densitySamples++;
    }

    if (pathDistance > 0.f) {
        bucket.pathDistanceTotal += pathDistance;
        bucket.pathDistanceSamples++;
    }

    if (eventName == "validation") {
        bucket.attempts++;
    } else if (eventName == "verifiedPath") {
        bucket.attempts++;
        bucket.verifiedPathCount++;
        bucket.lastVerifiedAtMs = nowMs;
    } else if (eventName == "directFallbackUnverified") {
        bucket.attempts++;
        bucket.directFallbackUnverifiedCount++;
        bucket.lastRejectedAtMs = nowMs;
    } else if (eventName == "activation") {
        bucket.activationCount++;
        bucket.lastActivatedAtMs = nowMs;
    } else if (eventName == "sampleComplete") {
        bucket.sampleCompleteCount++;
        bucket.lastSampleCompleteAtMs = nowMs;
    } else if (eventName == "coverageRetained") {
        bucket.coverageRetainedCount++;
        bucket.stationedSampleCount++;
        bucket.stationedDurationSeconds =
            bucket.stationedDurationSeconds < stationedDurationSeconds ?
            stationedDurationSeconds : bucket.stationedDurationSeconds;
        bucket.lastCoverageRetainedAtMs = nowMs;
    }

    reachabilityMemoryBuckets.put(key, bucket);
    pruneReachabilityMemoryNoLock(nowMs, ttlMs, maxRows);
}

static ReachabilityMemoryBucket getReachabilityMemoryBucket(
        const String& key) {
    Locker locker(&reachabilityMemoryMutex);

    if (reachabilityMemoryBuckets.contains(key))
        return reachabilityMemoryBuckets.get(key);

    return ReachabilityMemoryBucket();
}

static float applyReachabilityMemoryPreferenceScore(
        float baseScore,
        float distance,
        const ReachabilityMemoryBucket& bucket,
        int minAttemptsBeforePenalty,
        float verifiedPathScoreBonus,
        float sampleCompleteScoreBonus,
        float repeatedFailurePenalty,
        float longDistancePenalty512Plus) {
    float multiplier = 1.f;

    if (bucket.verifiedPathCount > 0)
        multiplier += verifiedPathScoreBonus;

    if (bucket.sampleCompleteCount > 0)
        multiplier += sampleCompleteScoreBonus;

    if (bucket.attempts >= minAttemptsBeforePenalty &&
            bucket.directFallbackUnverifiedCount >
                bucket.verifiedPathCount + bucket.sampleCompleteCount)
        multiplier -= repeatedFailurePenalty;

    if (distance >= 512.f)
        multiplier -= longDistancePenalty512Plus;

    if (multiplier < 0.25f)
        multiplier = 0.25f;

    return baseScore * multiplier;
}

static String formatReachabilityBucketStats(
        const ReachabilityMemoryBucket& bucket) {
    if (bucket.key.isEmpty())
        return "none";

    return String("attempts=") + String::valueOf(bucket.attempts) +
        ",verified=" + String::valueOf(bucket.verifiedPathCount) +
        ",fallback=" + String::valueOf(bucket.directFallbackUnverifiedCount) +
        ",activated=" + String::valueOf(bucket.activationCount) +
        ",sampleComplete=" + String::valueOf(bucket.sampleCompleteCount) +
        ",confidence=" +
            String::valueOf(Math::getPrecision(
                reachabilityMemoryConfidence(bucket), 3));
}

static String getReachabilityMemoryDistanceBand(
        const ReachabilityMemoryBucket& bucket,
        int bucketSizeMeters) {
    float x = static_cast<float>(bucket.bucketX * bucketSizeMeters);
    float y = static_cast<float>(bucket.bucketY * bucketSizeMeters);
    float distance = Math::sqrt(x * x + y * y);

    if (distance < 128.f)
        return "0-128m";

    if (distance < 256.f)
        return "128-256m";

    if (distance < 512.f)
        return "256-512m";

    return "512m+";
}

static JSONSerializationType buildReachabilityMemoryBucketJSON(
        const ReachabilityMemoryBucket& bucket,
        uint64 nowMs) {
    JSONSerializationType row = JSONSerializationType::object();
    row["key"] = bucket.key;
    row["planet"] = bucket.planet;
    row["resourceName"] = bucket.resourceName;
    row["resourceType"] = bucket.resourceType;
    row["profile"] = bucket.profileKey;
    row["targetSource"] = bucket.targetSource;
    row["bucketX"] = bucket.bucketX;
    row["bucketY"] = bucket.bucketY;
    row["attempts"] = bucket.attempts;
    row["verifiedPathCount"] = bucket.verifiedPathCount;
    row["directFallbackUnverifiedCount"] =
        bucket.directFallbackUnverifiedCount;
    row["activationCount"] = bucket.activationCount;
    row["sampleCompleteCount"] = bucket.sampleCompleteCount;
    row["coverageRetainedCount"] = bucket.coverageRetainedCount;
    row["stationedSampleCount"] = bucket.stationedSampleCount;
    row["stationedDurationSeconds"] = bucket.stationedDurationSeconds;
    row["sustainedCoverageConfidence"] =
        reachabilityMemoryConfidence(bucket);
    row["confidence"] = reachabilityMemoryConfidence(bucket);
    row["averagePathDistance"] =
        reachabilityMemoryAveragePathDistance(bucket);
    row["averageDensity"] = reachabilityMemoryAverageDensity(bucket);
    row["lastVerifiedAgeSeconds"] =
        bucket.lastVerifiedAtMs > 0 && nowMs > bucket.lastVerifiedAtMs ?
        (nowMs - bucket.lastVerifiedAtMs) / 1000 : 0;
    row["lastRejectedAgeSeconds"] =
        bucket.lastRejectedAtMs > 0 && nowMs > bucket.lastRejectedAtMs ?
        (nowMs - bucket.lastRejectedAtMs) / 1000 : 0;
    row["lastSampleCompleteAgeSeconds"] =
        bucket.lastSampleCompleteAtMs > 0 &&
            nowMs > bucket.lastSampleCompleteAtMs ?
        (nowMs - bucket.lastSampleCompleteAtMs) / 1000 : 0;
    row["lastCoverageRetainedAgeSeconds"] =
        bucket.lastCoverageRetainedAtMs > 0 &&
            nowMs > bucket.lastCoverageRetainedAtMs ?
        (nowMs - bucket.lastCoverageRetainedAtMs) / 1000 : 0;
    row["runtimeOnly"] = true;
    row["yieldMode"] = "conceptual";
    row["realResourceCreated"] = false;
    row["resourceContainerCreated"] = false;
    row["inventoryMutated"] = false;
    row["economyMutated"] = false;

    return row;
}

static void addReachabilityMemoryAggregate(
        VectorMap<String, int>& attempts,
        VectorMap<String, int>& verified,
        VectorMap<String, int>& fallback,
        VectorMap<String, int>& samples,
        const String& key,
        const ReachabilityMemoryBucket& bucket) {
    String aggregateKey = key.isEmpty() ? String("unknown") : key;
    attempts.put(
        aggregateKey,
        (attempts.contains(aggregateKey) ? attempts.get(aggregateKey) : 0) +
            bucket.attempts);
    verified.put(
        aggregateKey,
        (verified.contains(aggregateKey) ? verified.get(aggregateKey) : 0) +
            bucket.verifiedPathCount);
    fallback.put(
        aggregateKey,
        (fallback.contains(aggregateKey) ? fallback.get(aggregateKey) : 0) +
            bucket.directFallbackUnverifiedCount);
    samples.put(
        aggregateKey,
        (samples.contains(aggregateKey) ? samples.get(aggregateKey) : 0) +
            bucket.sampleCompleteCount);
}

static JSONSerializationType buildReachabilityMemoryAggregateRows(
        VectorMap<String, int>& attempts,
        VectorMap<String, int>& verified,
        VectorMap<String, int>& fallback,
        VectorMap<String, int>& samples,
        const String& labelKey) {
    JSONSerializationType rows = JSONSerializationType::array();

    for (int i = 0; i < attempts.size(); ++i) {
        String key = attempts.elementAt(i).getKey();
        int attemptCount = attempts.get(key);
        int verifiedCount = verified.contains(key) ? verified.get(key) : 0;
        int fallbackCount = fallback.contains(key) ? fallback.get(key) : 0;
        int sampleCount = samples.contains(key) ? samples.get(key) : 0;
        JSONSerializationType row = JSONSerializationType::object();
        row[labelKey] = key;
        row["attempts"] = attemptCount;
        row["verifiedPathCount"] = verifiedCount;
        row["directFallbackUnverifiedCount"] = fallbackCount;
        row["sampleCompleteCount"] = sampleCount;
        row["verifiedPercent"] = attemptCount > 0 ?
            Math::getPrecision(
                static_cast<float>(verifiedCount) * 100.f /
                    static_cast<float>(attemptCount),
                1) : 0.f;
        row["sampleCompletePercent"] = attemptCount > 0 ?
            Math::getPrecision(
                static_cast<float>(sampleCount) * 100.f /
                    static_cast<float>(attemptCount),
                1) : 0.f;
        rows.push_back(row);
    }

    return rows;
}

static JSONSerializationType buildReachabilityMemoryJSON(
        bool memoryEnabled,
        bool preferenceEnabled,
        int ttlSeconds,
        int bucketSizeMeters,
        int maxRows,
        int minAttemptsBeforePenalty,
        float verifiedPathScoreBonus,
        float sampleCompleteScoreBonus,
        float repeatedFailurePenalty,
        float longDistancePenalty512Plus,
        bool planetPenaltyEnabled,
        bool resourcePenaltyEnabled) {
    JSONSerializationType result = JSONSerializationType::object();
    result["enabled"] = memoryEnabled;
    result["candidatePreferenceEnabled"] = preferenceEnabled;
    result["mode"] = preferenceEnabled ?
        String("active-preference") : String("shadow-read-only");
    result["status"] = "no_data";
    result["runtimeOnly"] = true;
    result["readOnly"] = !preferenceEnabled;
    result["ttlSeconds"] = ttlSeconds;
    result["bucketSizeMeters"] = bucketSizeMeters;
    result["maxRows"] = maxRows;
    result["minAttemptsBeforePenalty"] = minAttemptsBeforePenalty;
    result["verifiedPathScoreBonus"] =
        Math::getPrecision(verifiedPathScoreBonus, 3);
    result["sampleCompleteScoreBonus"] =
        Math::getPrecision(sampleCompleteScoreBonus, 3);
    result["repeatedFailurePenalty"] =
        Math::getPrecision(repeatedFailurePenalty, 3);
    result["longDistancePenalty512Plus"] =
        Math::getPrecision(longDistancePenalty512Plus, 3);
    result["planetPenaltyEnabled"] = planetPenaltyEnabled;
    result["resourcePenaltyEnabled"] = resourcePenaltyEnabled;
    result["behaviorChanged"] = preferenceEnabled;
    result["persistenceChanged"] = false;
    result["realResourceCreated"] = false;
    result["resourceContainerCreated"] = false;
    result["inventoryMutated"] = false;
    result["economyMutated"] = false;

    JSONSerializationType successfulRows = JSONSerializationType::array();
    JSONSerializationType rejectedRows = JSONSerializationType::array();
    VectorMap<String, int> planetAttempts;
    VectorMap<String, int> planetVerified;
    VectorMap<String, int> planetFallback;
    VectorMap<String, int> planetSamples;
    VectorMap<String, int> resourceAttempts;
    VectorMap<String, int> resourceVerified;
    VectorMap<String, int> resourceFallback;
    VectorMap<String, int> resourceSamples;
    VectorMap<String, int> distanceAttempts;
    VectorMap<String, int> distanceVerified;
    VectorMap<String, int> distanceFallback;
    VectorMap<String, int> distanceSamples;
    int rowCount = 0;
    int verifiedBuckets = 0;
    int rejectedBuckets = 0;
    int sampleCompleteBuckets = 0;
    int totalAttempts = 0;
    int totalVerified = 0;
    int totalFallback = 0;
    int totalActivations = 0;
    int totalSampleComplete = 0;
    int shadowWouldSelectDifferent = 0;
    int shadowPreferredVerifiedHistory = 0;
    int activePreferenceUsed = 0;
    int activePreferenceFallback = 0;
    uint64 nowMs = System::getMiliTime();

    {
        Locker locker(&reachabilityMemoryMutex);
        rowCount = reachabilityMemoryBuckets.size();
        shadowWouldSelectDifferent =
            reachabilityMemoryDiagnostics.shadowWouldSelectDifferentCount;
        shadowPreferredVerifiedHistory =
            reachabilityMemoryDiagnostics.shadowPreferredVerifiedHistoryCount;
        activePreferenceUsed =
            reachabilityMemoryDiagnostics.activePreferenceUsedCount;
        activePreferenceFallback =
            reachabilityMemoryDiagnostics.activePreferenceFallbackCount;

        for (int i = 0; i < reachabilityMemoryBuckets.size(); ++i) {
            ReachabilityMemoryBucket bucket =
                reachabilityMemoryBuckets.elementAt(i).getValue();

            totalAttempts += bucket.attempts;
            totalVerified += bucket.verifiedPathCount;
            totalFallback += bucket.directFallbackUnverifiedCount;
            totalActivations += bucket.activationCount;
            totalSampleComplete += bucket.sampleCompleteCount;

            if (bucket.verifiedPathCount > 0)
                verifiedBuckets++;

            if (bucket.directFallbackUnverifiedCount > 0)
                rejectedBuckets++;

            if (bucket.sampleCompleteCount > 0)
                sampleCompleteBuckets++;

            addReachabilityMemoryAggregate(
                planetAttempts,
                planetVerified,
                planetFallback,
                planetSamples,
                bucket.planet,
                bucket);
            addReachabilityMemoryAggregate(
                resourceAttempts,
                resourceVerified,
                resourceFallback,
                resourceSamples,
                bucket.resourceType,
                bucket);
            addReachabilityMemoryAggregate(
                distanceAttempts,
                distanceVerified,
                distanceFallback,
                distanceSamples,
                getReachabilityMemoryDistanceBand(bucket, bucketSizeMeters),
                bucket);

            if ((bucket.sampleCompleteCount > 0 ||
                    bucket.verifiedPathCount > 0) &&
                    successfulRows.size() < 12) {
                successfulRows.push_back(
                    buildReachabilityMemoryBucketJSON(bucket, nowMs));
            }

            if (bucket.directFallbackUnverifiedCount > 0 &&
                    rejectedRows.size() < 12) {
                rejectedRows.push_back(
                    buildReachabilityMemoryBucketJSON(bucket, nowMs));
            }
        }
    }

    result["status"] = rowCount > 0 ? String("ready") : String("no_data");
    result["memoryRows"] = rowCount;
    result["totalAttempts"] = totalAttempts;
    result["verifiedPathCount"] = totalVerified;
    result["directFallbackUnverifiedCount"] = totalFallback;
    result["activationCount"] = totalActivations;
    result["sampleCompleteCount"] = totalSampleComplete;
    result["verifiedBuckets"] = verifiedBuckets;
    result["rejectedBuckets"] = rejectedBuckets;
    result["sampleCompleteBuckets"] = sampleCompleteBuckets;
    result["shadowWouldSelectDifferentCount"] = shadowWouldSelectDifferent;
    result["shadowPreferredVerifiedHistoryCount"] =
        shadowPreferredVerifiedHistory;
    result["activePreferenceUsedCount"] = activePreferenceUsed;
    result["activePreferenceFallbackCount"] = activePreferenceFallback;
    result["topSuccessfulBuckets"] = successfulRows;
    result["topRejectedBuckets"] = rejectedRows;
    JSONSerializationType byPlanet =
        buildReachabilityMemoryAggregateRows(
            planetAttempts, planetVerified, planetFallback, planetSamples,
            "planet");
    JSONSerializationType byResourceType =
        buildReachabilityMemoryAggregateRows(
            resourceAttempts, resourceVerified, resourceFallback,
            resourceSamples, "resourceType");
    JSONSerializationType byDistanceBand =
        buildReachabilityMemoryAggregateRows(
            distanceAttempts, distanceVerified, distanceFallback,
            distanceSamples, "distanceBand");
    result["byPlanet"] = byPlanet;
    result["byResourceType"] = byResourceType;
    result["byDistanceBand"] = byDistanceBand;
    result["byPlanetReachabilityMemory"] = byPlanet;
    result["byResourceReachabilityMemory"] = byResourceType;
    result["byDistanceBandReachabilityMemory"] = byDistanceBand;

    return result;
}

static void updateReachabilityMemoryFromAssignment(
        const MinerIntelligentTargetAssignment& assignment,
        const String& eventName,
        int bucketSizeMeters,
        int ttlSeconds,
        int maxRows) {
    if (assignment.minerID == 0 || assignment.targetZoneName.isEmpty())
        return;

    String key = buildReachabilityMemoryKey(
        assignment.targetZoneName,
        assignment.targetResourceName,
        assignment.targetResourceType,
        assignment.selectedProfileKey,
        assignment.targetSource,
        assignment.targetX,
        assignment.targetY,
        bucketSizeMeters);
    float pathDistance = assignment.targetDirectDistance;

    if (pathDistance <= 0.f)
        pathDistance = assignment.activationPathDistance;

    if (pathDistance <= 0.f)
        pathDistance = assignment.latestPathDistance;

    updateReachabilityMemoryBucket(
        key,
        assignment.targetZoneName,
        assignment.targetResourceName,
        assignment.targetResourceType,
        assignment.selectedProfileKey,
        assignment.targetSource,
        assignment.targetX,
        assignment.targetY,
        assignment.targetDensity,
        pathDistance,
        eventName,
        bucketSizeMeters,
        ttlSeconds,
        maxRows,
        eventName == "coverageRetained" ?
            assignment.stationDurationSeconds : 0);
}

static bool findMinerDensityTarget(
        uint64 minerID,
        const String& profileKey,
        const String& targetSource,
        const ResourceIntelligenceEntry& resource,
        Zone* zone,
        const Vector3& minerPosition,
        const Vector<int>& searchRadii,
        int samplesPerRadius,
        float minAcceptableDensity,
        bool requireNavmesh,
        bool minerInNavmesh,
        int maxNavmeshChecks,
        float distancePenaltyPerMeter,
        bool reachabilityMemoryEnabled,
        bool reachabilityCandidatePreferenceEnabled,
        int reachabilityBucketSizeMeters,
        int reachabilityMinAttemptsBeforePenalty,
        float reachabilityVerifiedPathScoreBonus,
        float reachabilitySampleCompleteScoreBonus,
        float reachabilityRepeatedFailurePenalty,
        float reachabilityLongDistancePenalty512Plus,
        int reachabilityMemoryTtlSeconds,
        int reachabilityMaxMemoryRows,
        MinerDensityTargetCandidate& selectedCandidate,
        MinerDensityTargetDiagnostics& diagnostics) {
    if (zone == nullptr || resource.name.isEmpty()) {
        diagnostics.rejectReason = "noValidCandidate";
        return false;
    }

    ZoneServer* zoneServer = ServerCore::getZoneServer();

    if (zoneServer == nullptr) {
        diagnostics.rejectReason = "noDensityMap";
        return false;
    }

    ManagedReference<ResourceManager*> resourceManager = zoneServer->getResourceManager();

    if (resourceManager == nullptr) {
        diagnostics.rejectReason = "noDensityMap";
        return false;
    }

    ManagedReference<ResourceSpawn*> resourceSpawn;

    {
        ReadLocker managerLocker(resourceManager);
        ResourceSpawner* spawner = resourceManager->getResourceSpawner();
        ResourceMap* resourceMap = spawner == nullptr ? nullptr : spawner->getResourceMap();
        String resourceKey = resource.name.toLowerCase();

        if (resourceMap != nullptr && resourceMap->contains(resourceKey))
            resourceSpawn = resourceMap->get(resourceKey);
    }

    if (resourceSpawn == nullptr) {
        diagnostics.rejectReason = "noDensityMap";
        return false;
    }

    String zoneName = zone->getZoneName();
    const float goldenAngle = 2.39996323f;
    uint32 deterministicSeed = static_cast<uint32>((minerID ^ resource.objectID) & 0xFFFFFFFF);
    float angleOffset = static_cast<float>(deterministicSeed % 6283) / 1000.f;
    int totalSamplesChecked = 0;
    int navmeshChecks = 0;
    MinerDensityTargetCandidate reachabilityPreferredCandidate;
    ReachabilityMemoryBucket reachabilityPreferredBucket;
    float reachabilityPreferredScore = -1000000.f;
    bool hasReachabilityPreferredCandidate = false;

    for (int radiusIndex = 0; radiusIndex < searchRadii.size(); ++radiusIndex) {
        int searchRadius = searchRadii.get(radiusIndex);

        if (!diagnostics.searchedRadii.isEmpty())
            diagnostics.searchedRadii += ",";

        diagnostics.searchedRadii += String::valueOf(searchRadius);

        Vector<MinerDensityTargetCandidate> candidates;

        for (int sampleIndex = 0; sampleIndex < samplesPerRadius; ++sampleIndex) {
            float radialFraction = Math::sqrt((static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(samplesPerRadius));
            float distance = radialFraction * static_cast<float>(searchRadius);
            float angle = angleOffset + goldenAngle * static_cast<float>(sampleIndex + radiusIndex * samplesPerRadius);
            float x = minerPosition.getX() + Math::cos(angle) * distance;
            float y = minerPosition.getY() + Math::sin(angle) * distance;

            if (!zone->isWithinBoundaries(Vector3(x, y, 0.f)))
                continue;

            MinerDensityTargetCandidate candidate;
            candidate.x = x;
            candidate.y = y;
            candidate.distance = distance;
            candidate.searchRadius = searchRadius;
            candidates.add(candidate);
        }

        {
            Locker spawnLocker(resourceSpawn);

            if (!resourceSpawn->inShift()) {
                diagnostics.rejectReason = "noDensityMap";
                return false;
            }

            for (int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
                MinerDensityTargetCandidate candidate = candidates.get(candidateIndex);
                candidate.density = resourceSpawn->getDensityAt(zoneName, candidate.x, candidate.y);
                ++totalSamplesChecked;
                diagnostics.candidateCount = totalSamplesChecked;

                if (candidate.density > diagnostics.bestObservedCandidate.density)
                    diagnostics.bestObservedCandidate = candidate;

                candidates.set(candidateIndex, candidate);
            }
        }

        MinerDensityTargetCandidate bestInRadius;

        for (int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
            MinerDensityTargetCandidate candidate = candidates.get(candidateIndex);

            if (candidate.density < minAcceptableDensity)
                continue;

            ++diagnostics.acceptableCandidateCount;
            candidate.legacyAdjustedScore = candidate.density * 1000.f -
                candidate.distance * distancePenaltyPerMeter;

            candidate.adjustedScore = candidate.legacyAdjustedScore;
            candidate.reachabilityAdjustedScore = candidate.legacyAdjustedScore;

            if (reachabilityMemoryEnabled) {
                candidate.reachabilityMemoryKey = buildReachabilityMemoryKey(
                    zoneName,
                    resource.name,
                    resource.type,
                    profileKey,
                    targetSource,
                    candidate.x,
                    candidate.y,
                    reachabilityBucketSizeMeters);
                ReachabilityMemoryBucket memoryBucket =
                    getReachabilityMemoryBucket(candidate.reachabilityMemoryKey);
                candidate.reachabilityConfidence =
                    reachabilityMemoryConfidence(memoryBucket);
                candidate.reachabilityAdjustedScore =
                    applyReachabilityMemoryPreferenceScore(
                        candidate.legacyAdjustedScore,
                        candidate.distance,
                        memoryBucket,
                        reachabilityMinAttemptsBeforePenalty,
                        reachabilityVerifiedPathScoreBonus,
                        reachabilitySampleCompleteScoreBonus,
                        reachabilityRepeatedFailurePenalty,
                        reachabilityLongDistancePenalty512Plus);

                if (memoryBucket.hasUsefulHistory() &&
                        candidate.reachabilityAdjustedScore >
                            reachabilityPreferredScore) {
                    reachabilityPreferredScore =
                        candidate.reachabilityAdjustedScore;
                    reachabilityPreferredCandidate = candidate;
                    reachabilityPreferredBucket = memoryBucket;
                    hasReachabilityPreferredCandidate = true;
                }

                if (reachabilityCandidatePreferenceEnabled &&
                        memoryBucket.hasUsefulHistory())
                    candidate.adjustedScore =
                        candidate.reachabilityAdjustedScore;
            }

            candidates.set(candidateIndex, candidate);
        }

        if (requireNavmesh && minerInNavmesh) {
            Vector<int> checkedIndexes;

            while (navmeshChecks < maxNavmeshChecks) {
                int bestUncheckedIndex = -1;
                float bestUncheckedScore = -1000000.f;

                for (int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
                    if (densityCandidateIndexUsed(checkedIndexes, candidateIndex))
                        continue;

                    MinerDensityTargetCandidate candidate = candidates.get(candidateIndex);

                    if (candidate.density < minAcceptableDensity || candidate.adjustedScore <= bestUncheckedScore)
                        continue;

                    bestUncheckedIndex = candidateIndex;
                    bestUncheckedScore = candidate.adjustedScore;
                }

                if (bestUncheckedIndex < 0)
                    break;

                checkedIndexes.add(bestUncheckedIndex);
                MinerDensityTargetCandidate candidate = candidates.get(bestUncheckedIndex);
                int candidateRank = checkedIndexes.size();

                if (!isDensityCandidateInRequiredNavmesh(
                        zone,
                        candidate.x,
                        candidate.y,
                        true,
                        true,
                        navmeshChecks,
                        maxNavmeshChecks)) {
                    if (!diagnostics.hasBestRejectedCandidate() ||
                            candidate.adjustedScore > diagnostics.bestRejectedCandidate.adjustedScore) {
                        diagnostics.bestRejectedCandidate = candidate;
                        diagnostics.bestRejectedReason = "noNavmesh";
                    }
                    continue;
                }

                candidate.navmeshChecked = true;
                diagnostics.acceptedCandidateRank = candidateRank;
                bestInRadius = candidate;
                break;
            }
        } else {
            for (int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
                MinerDensityTargetCandidate candidate = candidates.get(candidateIndex);

                if (candidate.density < minAcceptableDensity)
                    continue;

                if (!bestInRadius.isValid() || candidate.adjustedScore > bestInRadius.adjustedScore)
                    bestInRadius = candidate;
            }

            if (bestInRadius.isValid())
                diagnostics.acceptedCandidateRank = 1;
        }

        if (bestInRadius.isValid()) {
            bestInRadius.z = zone->getHeight(bestInRadius.x, bestInRadius.y);
            bestInRadius.samplesChecked = totalSamplesChecked;
            selectedCandidate = bestInRadius;

            if (reachabilityMemoryEnabled) {
                ReachabilityMemoryBucket selectedBucket =
                    getReachabilityMemoryBucket(
                        selectedCandidate.reachabilityMemoryKey);
                bool hasSelectedMemory = selectedBucket.hasUsefulHistory();
                bool wouldSelectDifferent =
                    hasReachabilityPreferredCandidate &&
                    Vector3(
                        reachabilityPreferredCandidate.x,
                        reachabilityPreferredCandidate.y,
                        0.f).distanceTo(
                            Vector3(
                                selectedCandidate.x,
                                selectedCandidate.y,
                                0.f)) > 1.f;

                {
                    Locker memoryLocker(&reachabilityMemoryMutex);

                    if (hasReachabilityPreferredCandidate &&
                            (reachabilityPreferredBucket.verifiedPathCount > 0 ||
                             reachabilityPreferredBucket.sampleCompleteCount > 0))
                        reachabilityMemoryDiagnostics
                            .shadowPreferredVerifiedHistoryCount++;

                    if (reachabilityCandidatePreferenceEnabled &&
                            hasSelectedMemory) {
                        reachabilityMemoryDiagnostics
                            .activePreferenceUsedCount++;
                    } else if (reachabilityCandidatePreferenceEnabled &&
                            !hasSelectedMemory) {
                        reachabilityMemoryDiagnostics
                            .activePreferenceFallbackCount++;
                    } else if (wouldSelectDifferent) {
                        reachabilityMemoryDiagnostics
                            .shadowWouldSelectDifferentCount++;
                    }
                }

                SimPlayerManager* manager = SimPlayerManager::instance();

                if (manager != nullptr) {
                    manager->info(
                        String("ReachabilityCandidatePreference miner=") +
                        String::valueOf(minerID) +
                        " profile=" + profileKey +
                        " resource=" + resource.name +
                        " type=" + resource.type +
                        " planet=" + zoneName +
                        " mode=" +
                            (reachabilityCandidatePreferenceEnabled ?
                                String("active-preference") :
                                String("shadow-only")) +
                        " wouldSelectDifferent=" +
                            (wouldSelectDifferent ? String("true") :
                                String("false")) +
                        " selectedX=" +
                            String::valueOf(Math::getPrecision(
                                selectedCandidate.x, 1)) +
                        " selectedY=" +
                            String::valueOf(Math::getPrecision(
                                selectedCandidate.y, 1)) +
                        " selectedDensity=" +
                            String::valueOf(Math::getPrecision(
                                selectedCandidate.density, 3)) +
                        " selectedLegacyScore=" +
                            String::valueOf(Math::getPrecision(
                                selectedCandidate.legacyAdjustedScore, 2)) +
                        " selectedReachabilityScore=" +
                            String::valueOf(Math::getPrecision(
                                selectedCandidate.reachabilityAdjustedScore, 2)) +
                        " selectedBucketStats=\"" +
                            formatReachabilityBucketStats(selectedBucket) +
                            "\"" +
                        " preferredX=" +
                            String::valueOf(Math::getPrecision(
                                reachabilityPreferredCandidate.x, 1)) +
                        " preferredY=" +
                            String::valueOf(Math::getPrecision(
                                reachabilityPreferredCandidate.y, 1)) +
                        " preferredDensity=" +
                            String::valueOf(Math::getPrecision(
                                reachabilityPreferredCandidate.density, 3)) +
                        " preferredReachabilityScore=" +
                            String::valueOf(Math::getPrecision(
                                reachabilityPreferredCandidate
                                    .reachabilityAdjustedScore, 2)) +
                        " preferredBucketStats=\"" +
                            formatReachabilityBucketStats(
                                reachabilityPreferredBucket) +
                            "\"" +
                        " behaviorChanged=" +
                            (reachabilityCandidatePreferenceEnabled ?
                                String("true") : String("false")),
                        true);
                }
            }

            return true;
        }
    }

    if (diagnostics.acceptableCandidateCount > 0 && requireNavmesh && minerInNavmesh) {
        diagnostics.rejectReason = "noNavmesh";
    } else if (diagnostics.bestObservedCandidate.density <= 0.f) {
        diagnostics.rejectReason = "noValidCandidate";
    } else if (diagnostics.bestObservedCandidate.density < minAcceptableDensity) {
        diagnostics.rejectReason = "belowMinDensity";
        diagnostics.bestRejectedCandidate = diagnostics.bestObservedCandidate;
        diagnostics.bestRejectedReason = "belowMinDensity";
    } else if (diagnostics.candidateCount > 0) {
        diagnostics.rejectReason = "allCandidatesRejected";
    } else {
        diagnostics.rejectReason = "noValidCandidate";
    }

    return false;
}

static float calculateWorldPathDistance(const Vector<WorldCoordinates>* path) {
    if (path == nullptr || path->size() < 2)
        return 0.f;

    float distance = 0.f;

    for (int i = 1; i < path->size(); ++i) {
        Vector3 previous = path->get(i - 1).getWorldPosition();
        Vector3 current = path->get(i).getWorldPosition();
        distance += previous.distanceTo(current);
    }

    return distance;
}

void MinerPathValidationTask::run() {
    SimPlayerManager* manager = SimPlayerManager::instance();

    if (manager == nullptr || !manager->enabled || zone == nullptr)
        return;

    Vector<WorldCoordinates>* path = nullptr;
    bool pathException = false;

    try {
        WorldCoordinates start(startPosition, nullptr);
        WorldCoordinates target(targetPosition, nullptr);
        path = PathFinderManager::instance()->findPath(start, target, zone);
    } catch (...) {
        pathException = true;
    }

    int pathNodes = path == nullptr ? 0 : path->size();
    float pathDistance = calculateWorldPathDistance(path);
    bool directFallback = pathNodes == 2;
    bool pathFound = path != nullptr && pathNodes >= 2;
    bool targetNavmeshChecked = zone != nullptr;
    bool targetInNavmesh = targetNavmeshChecked ?
        isPointInAnyNavmesh(zone, targetPosition.getX(), targetPosition.getY()) :
        false;
    bool targetTerrainHeightKnown = zone != nullptr;
    float targetTerrainHeight = targetTerrainHeightKnown ?
        zone->getHeight(targetPosition.getX(), targetPosition.getY()) : 0.f;
    float targetZDelta = targetTerrainHeightKnown ?
        targetPosition.getZ() - targetTerrainHeight : 0.f;
    String rejectReason;

    if (pathException) {
        pathFound = false;
        rejectReason = "pathException";
    } else if (!pathFound) {
        rejectReason = "noPath";
    } else if (directFallback) {
        // Core3 returns start/end when a world path could not be evaluated.
        pathFound = false;
        rejectReason = "directFallbackUnverified";
    } else if (pathNodes > maxPathNodes) {
        pathFound = false;
        rejectReason = "tooManyPathNodes";
    } else if (pathDistance > static_cast<float>(maxPathDistance)) {
        pathFound = false;
        rejectReason = "pathTooLong";
    }

    String pathTrustStatus = pathFound ? String("verifiedPath") :
        (rejectReason.isEmpty() ? String("untrusted") : rejectReason);

    String line = String("MinerPathValidationSimulation miner=") + String::valueOf(minerID) +
        " zone=" + zoneName +
        " profile=" + profileKey +
        " resource=" + resourceName +
        " type=" + resourceType +
        " targetSource=" + targetSource +
        " assignmentGenerationId=" + String::valueOf(assignmentGenerationId) +
        " targetHash=" +
            (targetHash.isEmpty() ?
                buildMinerAssignmentTargetHash(
                    targetSource,
                    profileKey,
                    resourceName,
                    resourceType,
                    zoneName,
                    targetPosition.getX(),
                    targetPosition.getY(),
                    targetPosition.getZ()) :
                targetHash) +
        " target=(x:" + String::valueOf(Math::getPrecision(targetPosition.getX(), 1)) +
        ",y:" + String::valueOf(Math::getPrecision(targetPosition.getY(), 1)) +
        ",z:" + String::valueOf(Math::getPrecision(targetPosition.getZ(), 1)) + ")" +
        " density=" + String::valueOf(Math::getPrecision(density, 3)) +
        " distance=" + String::valueOf(Math::getPrecision(directDistance, 1)) +
        " densityTarget=" + (acceptedDensityTarget ? String("accepted") : String("rejected")) +
        " pathFound=" + (pathFound ? String("true") : String("false")) +
        " pathNodes=" + String::valueOf(pathNodes) +
        " pathDistance=" + String::valueOf(Math::getPrecision(pathDistance, 1)) +
        " directFallback=" + (directFallback ? String("true") : String("false")) +
        " pathTrustStatus=" + pathTrustStatus;

    if (!rejectReason.isEmpty())
        line += " rejectReason=" + rejectReason;

    line += " mode=simulation-only";
    MinerPathValidationSnapshot snapshot;
    snapshot.assignmentGenerationId = assignmentGenerationId;
    snapshot.targetHash = targetHash.isEmpty() ?
        buildMinerAssignmentTargetHash(
            targetSource,
            profileKey,
            resourceName,
            resourceType,
            zoneName,
            targetPosition.getX(),
            targetPosition.getY(),
            targetPosition.getZ()) :
        targetHash;
    snapshot.zoneName = zoneName;
    snapshot.profileKey = profileKey;
    snapshot.resourceName = resourceName;
    snapshot.resourceType = resourceType;
    snapshot.targetSource = targetSource;
    snapshot.acceptedDensityTarget = acceptedDensityTarget;
    snapshot.pathFound = pathFound;
    snapshot.rejectReason = rejectReason.isEmpty() ? String("none") : rejectReason;
    snapshot.pathTrustStatus = pathTrustStatus;
    snapshot.pathNodes = pathNodes;
    snapshot.pathDistance = pathDistance;
    snapshot.density = density;
    snapshot.directDistance = directDistance;
    snapshot.targetX = targetPosition.getX();
    snapshot.targetY = targetPosition.getY();
    snapshot.targetZ = targetPosition.getZ();
    snapshot.minerX = startPosition.getX();
    snapshot.minerY = startPosition.getY();
    snapshot.minerZ = startPosition.getZ();
    snapshot.directFallback = directFallback;
    snapshot.minerInNavmeshKnown = true;
    snapshot.minerInNavmesh = minerInNavmesh;
    snapshot.targetNavmeshChecked = targetNavmeshChecked;
    snapshot.targetInNavmesh = targetInNavmesh;
    snapshot.targetTerrainHeightKnown = targetTerrainHeightKnown;
    snapshot.targetTerrainHeight = targetTerrainHeight;
    snapshot.targetZDelta = targetZDelta;
    snapshot.maxPathDistance = maxPathDistance;
    snapshot.maxPathNodes = maxPathNodes;
    snapshot.recordedAtMs = System::getMiliTime();
    uint64 validationSnapshotId =
        manager->recordMinerPathValidationSnapshot(minerID, snapshot);
    line += " validationSnapshotId=" + String::valueOf(validationSnapshotId);
    manager->recordReachabilityValidationSnapshot(snapshot);

    MinerIntelligentTargetAssignment assignment;
    bool assignmentMatches = false;

    if (manager->getMinerIntelligentTargetAssignment(minerID, assignment)) {
        assignmentMatches =
            minerValidationSnapshotMatchesAssignment(assignment, snapshot);

        if (assignmentMatches) {
            bool firstVerifiedValidation =
                pathFound && !assignment.reachabilityValidatedRecorded;
            bool firstRejectedValidation =
                !pathFound && !assignment.reachabilityRejectedRecorded;
            assignment.updatedAtMs = snapshot.recordedAtMs;
            assignment.latestValidationSnapshotId = validationSnapshotId;
            assignment.latestValidationTargetHash = snapshot.targetHash;
            assignment.latestValidationMismatchReason = "none";
            assignment.pathValidationStatus = pathFound ? "valid" : "failed";
            assignment.pathValidationTrustStatus = pathTrustStatus;
            assignment.currentPathValidationStatus = assignment.pathValidationStatus;
            assignment.currentPathTrustStatus = assignment.pathValidationTrustStatus;
            assignment.pathValidationMatched = pathFound;
            String validationStatus = pathFound ? String("validated") : String("candidate");

            if (isMinerAssignmentLifecycleActiveStatus(assignment.status)) {
                if (assignment.status != validationStatus)
                    assignment.lifecycleDowngradePrevented = true;
            } else {
                assignment.status = validationStatus;
            }

            if (pathFound && assignment.validatedAtMs == 0)
                assignment.validatedAtMs = snapshot.recordedAtMs;
            if (pathFound) {
                assignment.validatedSnapshotId = validationSnapshotId;
                assignment.validatedTargetHash = snapshot.targetHash;
                assignment.validatedPathValidationStatus = "valid";
                assignment.validatedPathTrustStatus = pathTrustStatus;
            }
            if (firstVerifiedValidation)
                assignment.reachabilityValidatedRecorded = true;
            else if (firstRejectedValidation)
                assignment.reachabilityRejectedRecorded = true;

            manager->putMinerIntelligentTargetAssignment(assignment);

            if (firstVerifiedValidation)
                manager->recordReachabilityAssignmentValidated(assignment);
            else if (firstRejectedValidation)
                manager->recordReachabilityCandidateRejected(
                    assignment,
                    manager->getReachabilityFailureReason(snapshot));
        }
    }

    manager->info(line, true);

    if (targetSource == "demand_weighted_plan") {
        String validationLine = String("DemandWeightedMinerTargetValidation miner=") +
            String::valueOf(minerID) +
            " zone=" + zoneName +
            " selectedProfile=" + profileKey +
            " targetResource=" + resourceName +
            " targetType=" + resourceType +
            " targetSource=demand_weighted_plan" +
            " densityTargetStatus=" +
                (acceptedDensityTarget ? String("accepted") : String("rejected")) +
            " pathValidationStatus=" +
                (pathFound ? String("valid") : String("failed")) +
            " pathTrustStatus=" + pathTrustStatus +
            " assignmentGenerationId=" +
                String::valueOf(assignmentGenerationId) +
            " targetHash=" + snapshot.targetHash +
            " validationSnapshotId=" +
                String::valueOf(validationSnapshotId) +
            " matchesSwitchDecision=" +
                (assignmentMatches ? String("true") : String("false"));

        if (!rejectReason.isEmpty())
            validationLine += " fallbackReason=" + rejectReason;

        validationLine += " mode=diagnostic-only";
        manager->info(validationLine, true);
    }

    if (path != nullptr)
        delete path;
}

static int calculateGenericScore(const ResourceIntelligenceEntry& entry) {
    int weightedTotal = 0;
    int totalWeight = 0;

    addScorePart(entry.oq, 2, weightedTotal, totalWeight);
    addScorePart(entry.cd, 1, weightedTotal, totalWeight);
    addScorePart(entry.dr, 1, weightedTotal, totalWeight);
    addScorePart(entry.hr, 1, weightedTotal, totalWeight);
    addScorePart(entry.fl, 1, weightedTotal, totalWeight);
    addScorePart(entry.ma, 1, weightedTotal, totalWeight);
    addScorePart(entry.pe, 1, weightedTotal, totalWeight);
    addScorePart(entry.sr, 1, weightedTotal, totalWeight);
    addScorePart(entry.ut, 1, weightedTotal, totalWeight);
    addScorePart(entry.cr, 1, weightedTotal, totalWeight);

    return finishScore(weightedTotal, totalWeight);
}

static int calculateWeaponsmithScore(const ResourceIntelligenceEntry& entry) {
    int weightedTotal = 0;
    int totalWeight = 0;

    addScorePart(entry.cd, 3, weightedTotal, totalWeight);
    addScorePart(entry.oq, 3, weightedTotal, totalWeight);
    addScorePart(entry.sr, 2, weightedTotal, totalWeight);
    addScorePart(entry.ut, 2, weightedTotal, totalWeight);

    return finishScore(weightedTotal, totalWeight);
}

static int calculateArmorsmithScore(const ResourceIntelligenceEntry& entry) {
    int weightedTotal = 0;
    int totalWeight = 0;

    addScorePart(entry.oq, 3, weightedTotal, totalWeight);
    addScorePart(entry.ut, 2, weightedTotal, totalWeight);
    addScorePart(entry.sr, 2, weightedTotal, totalWeight);
    addScorePart(entry.dr, 2, weightedTotal, totalWeight);
    addScorePart(entry.ma, 1, weightedTotal, totalWeight);

    return finishScore(weightedTotal, totalWeight);
}

static int calculateChefScore(const ResourceIntelligenceEntry& entry) {
    int weightedTotal = 0;
    int totalWeight = 0;

    addScorePart(entry.oq, 3, weightedTotal, totalWeight);
    addScorePart(entry.pe, 2, weightedTotal, totalWeight);
    addScorePart(entry.fl, 2, weightedTotal, totalWeight);
    addScorePart(entry.dr, 1, weightedTotal, totalWeight);

    return finishScore(weightedTotal, totalWeight);
}

static int calculateArchitectScore(const ResourceIntelligenceEntry& entry) {
    int weightedTotal = 0;
    int totalWeight = 0;

    addScorePart(entry.oq, 3, weightedTotal, totalWeight);
    addScorePart(entry.dr, 2, weightedTotal, totalWeight);
    addScorePart(entry.ut, 2, weightedTotal, totalWeight);
    addScorePart(entry.ma, 2, weightedTotal, totalWeight);

    return finishScore(weightedTotal, totalWeight);
}

static void calculateResourceIntelligenceScores(Vector<ResourceIntelligenceEntry>& entries) {
    for (int i = 0; i < entries.size(); ++i) {
        ResourceIntelligenceEntry entry = entries.get(i);

        entry.genericScore = calculateGenericScore(entry);
        entry.weaponsmithScore = calculateWeaponsmithScore(entry);
        entry.armorsmithScore = calculateArmorsmithScore(entry);
        entry.chefScore = calculateChefScore(entry);
        entry.architectScore = calculateArchitectScore(entry);

        entries.set(i, entry);
    }
}

static int getResourceIntelligenceScore(const ResourceIntelligenceEntry& entry, int scoreFamily) {
    switch (scoreFamily) {
    case 0:
        return entry.genericScore;
    case 1:
        return entry.weaponsmithScore;
    case 2:
        return entry.armorsmithScore;
    case 3:
        return entry.chefScore;
    case 4:
        return entry.architectScore;
    default:
        return 0;
    }
}

static String getResourceScoutCategory(int scoreFamily) {
    switch (scoreFamily) {
    case 1:
        return "weaponsmith";
    case 2:
        return "armorsmith";
    case 3:
        return "chef";
    case 4:
        return "architect";
    default:
        return "generic";
    }
}

static String getResourceScoutBestUse(int scoreFamily) {
    switch (scoreFamily) {
    case 1:
        return "weaponsmith_material_quality";
    case 2:
        return "armorsmith_material_quality";
    case 3:
        return "chef_high_value_consumables";
    case 4:
        return "architect_infrastructure_material";
    default:
        return "best_available_resource";
    }
}

static String getResourceScoutPlanet(const ResourceIntelligenceEntry& entry) {
    if (entry.zones.isEmpty())
        return "unknown";

    return entry.zones.indexOf(",") >= 0 ? String("multi") : entry.zones;
}

static JSONSerializationType buildResourceScoutOpportunityJSON(
        const ResourceIntelligenceEntry& entry,
        const String& bestUse,
        const String& source,
        int displayScore) {
    JSONSerializationType opportunity = JSONSerializationType::object();

    opportunity["resourceName"] = entry.name;
    opportunity["resourceType"] = entry.type;
    opportunity["objectId"] = entry.objectID;
    opportunity["planet"] = getResourceScoutPlanet(entry);
    opportunity["zones"] = entry.zones;
    opportunity["active"] = entry.inShift;
    opportunity["despawned"] = static_cast<uint64>(entry.despawned);
    opportunity["bestUse"] = bestUse;
    opportunity["score"] = displayScore;
    opportunity["source"] = source;
    opportunity["mode"] = "read-only";

    JSONSerializationType stats = JSONSerializationType::object();
    stats["OQ"] = entry.oq;
    stats["CD"] = entry.cd;
    stats["DR"] = entry.dr;
    stats["HR"] = entry.hr;
    stats["FL"] = entry.fl;
    stats["MA"] = entry.ma;
    stats["PE"] = entry.pe;
    stats["SR"] = entry.sr;
    stats["UT"] = entry.ut;
    stats["CR"] = entry.cr;
    opportunity["stats"] = stats;

    JSONSerializationType scores = JSONSerializationType::object();
    scores["generic"] = entry.genericScore;
    scores["weaponsmith"] = entry.weaponsmithScore;
    scores["armorsmith"] = entry.armorsmithScore;
    scores["chef"] = entry.chefScore;
    scores["architect"] = entry.architectScore;
    opportunity["scores"] = scores;

    JSONSerializationType gatherability = JSONSerializationType::object();
    gatherability["knownDensityAvailable"] = false;
    gatherability["knownDensity"] = 0;
    gatherability["confidence"] = "not_observed";
    opportunity["gatherability"] = gatherability;

    return opportunity;
}

static bool assignmentTargetsResource(
        const MinerIntelligentTargetAssignment& assignment,
        const ResourceIntelligenceEntry& entry) {
    return assignment.targetResourceName == entry.name &&
        assignment.targetResourceType == entry.type;
}

static String classifyResourceCoverageBlocker(
        const MinerIntelligentTargetAssignment& assignment) {
    String details = assignment.lastActivationResult.toLowerCase() + "," +
        assignment.lastFailureReason.toLowerCase() + "," +
        assignment.clearReason.toLowerCase();

    if (assignment.densityTargetStatus == "wrongPlanet")
        return "wrong_planet";

    if (!assignment.densityTargetStatus.isEmpty() &&
            assignment.densityTargetStatus != "accepted")
        return "blocked_by_density";

    if (assignment.pathValidationStatus == "failed" ||
            assignment.pathValidationStatus == "not_available" ||
            assignment.pathValidationStatus == "target_mismatch" ||
            assignment.pathValidationStatus == "stale" ||
            assignment.lastFailureReason == "pathFailed")
        return "blocked_by_path";

    if (details.indexOf("cooldown") >= 0)
        return "cooldown";

    if (details.indexOf("activecap") >= 0 ||
            details.indexOf("active cap") >= 0 ||
            details.indexOf("capped") >= 0)
        return "capped";

    return "uncovered";
}

static String getResourceCoverageReason(const String& status) {
    if (status == "covered")
        return "active miner assigned";
    if (status == "wrong_planet")
        return "target not available from miner planet";
    if (status == "blocked_by_density")
        return "no accepted density target";
    if (status == "blocked_by_path")
        return "no validated path";
    if (status == "cooldown")
        return "miner activation cooldown";
    if (status == "capped")
        return "active miner cap reached";

    return "no active miner assigned";
}

static String normalizeCoverageKey(const String& value) {
    String normalized = value.toLowerCase();
    normalized = normalized.replaceAll(" ", "");
    return normalized;
}

static bool coverageKeysMatchNormalized(
        const String& left,
        const String& right) {
    if (left.isEmpty() || right.isEmpty())
        return false;

    return normalizeCoverageKey(left) == normalizeCoverageKey(right);
}

static bool assignmentTargetsResourceNormalized(
        const MinerIntelligentTargetAssignment& assignment,
        const ResourceIntelligenceEntry& entry) {
    return coverageKeysMatchNormalized(assignment.targetResourceName, entry.name) &&
        coverageKeysMatchNormalized(assignment.targetResourceType, entry.type);
}

static bool resourceCoverageZoneContains(
        const String& assignmentZone,
        const ResourceIntelligenceEntry& entry) {
    if (assignmentZone.isEmpty() || entry.zones.isEmpty())
        return false;

    String zone = normalizeCoverageKey(assignmentZone);
    String zones = normalizeCoverageKey(entry.zones);

    if (zone.isEmpty() || zones.isEmpty())
        return false;

    if (zones == zone)
        return true;

    String haystack = "," + zones + ",";
    String needle = "," + zone + ",";

    return haystack.indexOf(needle) >= 0;
}

static bool resourceCoverageZonesContainAny(
        const Vector<String>& zones,
        const ResourceIntelligenceEntry& entry) {
    for (int i = 0; i < zones.size(); ++i) {
        if (resourceCoverageZoneContains(zones.get(i), entry))
            return true;
    }

    return false;
}

static String joinCoverageZones(const Vector<String>& zones) {
    String result;

    for (int i = 0; i < zones.size(); ++i) {
        if (!result.isEmpty())
            result += ",";

        result += zones.get(i);
    }

    return result.isEmpty() ? String("none") : result;
}

static bool isCoverageAlignmentUntrustedAssignment(
        const MinerIntelligentTargetAssignment& assignment) {
    if (!assignment.densityTargetStatus.isEmpty() &&
            assignment.densityTargetStatus != "accepted")
        return true;

    if (assignment.pathValidationStatus == "failed" ||
            assignment.pathValidationStatus == "not_available" ||
            assignment.pathValidationStatus == "target_mismatch" ||
            assignment.pathValidationStatus == "stale")
        return true;

    return assignment.pathValidationTrustStatus == "directFallbackUnverified";
}

static String getCoverageAlignmentMatchReason(
        bool resourceMatch,
        bool normalizedResourceMatch,
        bool profileMatch,
        bool zoneMatch,
        bool expired,
        bool active,
        bool untrusted,
        const String& assignmentStatus) {
    if (resourceMatch && profileMatch && zoneMatch) {
        if (expired)
            return "exact_resource_profile_zone_match_but_assignment_stale";
        if (active)
            return "exact_resource_profile_zone_match_active";
        if (untrusted)
            return "exact_resource_profile_zone_match_but_path_untrusted";
        if (assignmentStatus == "validated")
            return "exact_resource_profile_zone_match_validated_waiting_for_activation";
        if (assignmentStatus == "candidate")
            return "exact_resource_profile_zone_match_candidate_not_validated";

        return "exact_resource_profile_zone_match_not_active";
    }

    if (resourceMatch && profileMatch)
        return "exact_resource_and_profile_match_but_zone_mismatch";

    if (resourceMatch)
        return "exact_resource_match_but_profile_mismatch";

    if (normalizedResourceMatch)
        return "normalized_resource_match_but_exact_key_differs";

    if (profileMatch)
        return "profile_match_but_resource_mismatch";

    return "not_top_opportunity";
}

static String getPathValidationDiagnosticKey(
        const MinerIntelligentTargetAssignment& assignment,
        bool snapshotAvailable,
        const MinerPathValidationSnapshot& snapshot,
        float coordinateMismatchDistance,
        bool validationStale) {
    if (!assignment.densityTargetStatus.isEmpty() &&
            assignment.densityTargetStatus != "accepted")
        return "density_target_not_accepted";

    if (!snapshotAvailable)
        return "path_validation_unavailable";

    if (validationStale || assignment.pathValidationStatus == "stale")
        return "stale";

    if (assignment.pathValidationStatus == "target_mismatch")
        return "target_mismatch";

    if (coordinateMismatchDistance > 2.f)
        return "density_target_coordinate_mismatch";

    if (snapshot.minerInNavmeshKnown && !snapshot.minerInNavmesh)
        return "miner_not_in_navmesh";

    if (snapshot.directFallback ||
            snapshot.rejectReason == "directFallbackUnverified" ||
            snapshot.pathTrustStatus == "directFallbackUnverified" ||
            assignment.pathValidationTrustStatus == "directFallbackUnverified")
        return "direct_fallback_unverified";

    if (snapshot.targetNavmeshChecked && !snapshot.targetInNavmesh)
        return "target_outside_navmesh";

    if (snapshot.targetTerrainHeightKnown &&
            snapshot.targetZDelta * snapshot.targetZDelta > 9.f)
        return "bad_terrain_or_height";

    if (snapshot.rejectReason == "exceedsMaxPathDistance" ||
            snapshot.pathTrustStatus == "exceedsMaxPathDistance")
        return "exceeds_max_path_distance";

    if (snapshot.rejectReason == "pathTooLong" ||
            snapshot.pathTrustStatus == "pathTooLong")
        return "path_too_long";

    if (snapshot.rejectReason == "tooManyPathNodes" ||
            snapshot.pathTrustStatus == "tooManyPathNodes")
        return "too_many_path_nodes";

    if (snapshot.rejectReason == "noPath" ||
            snapshot.pathTrustStatus == "noPath")
        return "no_path";

    if (snapshot.rejectReason == "pathException" ||
            snapshot.pathTrustStatus == "pathException")
        return "path_exception";

    if (assignment.pathValidationStatus == "valid" &&
            assignment.pathValidationTrustStatus == "verifiedPath" &&
            snapshot.pathFound)
        return "verified_path";

    if (assignment.pathValidationStatus == "failed")
        return "unknown_path_failure";

    return "not_checked";
}

static String getPathValidationHumanReason(const String& key) {
    if (key == "direct_fallback_unverified")
        return "Pathfinder returned an unverified direct start/end fallback; activation remains blocked.";
    if (key == "path_too_long")
        return "Computed path distance exceeds the configured path validation limit.";
    if (key == "exceeds_max_path_distance")
        return "Straight-line target distance exceeds the configured maximum validation distance.";
    if (key == "too_many_path_nodes")
        return "Path was found, but it returned more nodes than the validation cap allows.";
    if (key == "no_path")
        return "Pathfinder did not return a usable path for the candidate target.";
    if (key == "path_exception")
        return "Path validation hit a pathfinder exception; assignment remains blocked.";
    if (key == "target_mismatch")
        return "The stored assignment target no longer matches the latest path validation target.";
    if (key == "density_target_coordinate_mismatch")
        return "Assignment coordinate drifted from the coordinate used by the path validation snapshot.";
    if (key == "stale")
        return "Path validation is stale and should be refreshed before activation.";
    if (key == "miner_not_in_navmesh")
        return "Miner is not currently reported inside navmesh.";
    if (key == "target_outside_navmesh")
        return "Target coordinate is outside the known navmesh areas checked by the validator.";
    if (key == "bad_terrain_or_height")
        return "Target Z differs from sampled terrain height enough to suspect bad terrain or height.";
    if (key == "density_target_not_accepted")
        return "Assignment does not have an accepted density target.";
    if (key == "path_validation_unavailable")
        return "No matching path validation snapshot is available for this assignment yet.";
    if (key == "verified_path")
        return "Assignment has a verified path snapshot.";

    return "Path validation has not produced a more specific diagnostic yet.";
}

static String getPathValidationRecommendedAction(const String& key) {
    if (key == "direct_fallback_unverified" ||
            key == "no_path" ||
            key == "target_outside_navmesh")
        return "inspect_navmesh_or_target_coordinate";
    if (key == "miner_not_in_navmesh")
        return "inspect_miner_spawn_navmesh";
    if (key == "bad_terrain_or_height")
        return "inspect_target_navmesh_or_terrain";
    if (key == "path_too_long" ||
            key == "exceeds_max_path_distance" ||
            key == "too_many_path_nodes")
        return "choose_closer_density_target";
    if (key == "path_exception")
        return "inspect_navmesh_or_pathfinder";
    if (key == "target_mismatch" ||
            key == "density_target_coordinate_mismatch")
        return "wait_for_fresh_validation_or_reassign";
    if (key == "stale" ||
            key == "path_validation_unavailable")
        return "wait_for_fresh_path_validation";
    if (key == "density_target_not_accepted")
        return "inspect_density_target";
    if (key == "verified_path")
        return "path_clear";

    return "inspect_path_validation";
}

static bool resourceIntelligenceIndexUsed(const Vector<int>& usedIndexes, int index) {
    for (int i = 0; i < usedIndexes.size(); ++i) {
        if (usedIndexes.get(i) == index)
            return true;
    }

    return false;
}

static String formatResourceIntelligenceLine(const String& label, const ResourceIntelligenceEntry& entry, int score, int rank) {
    String line = "ResourceIntelligence top " + label + " #" + String::valueOf(rank) +
        ": " + entry.name + " type=" + entry.type +
        " score=" + String::valueOf(score) +
        " OQ=" + String::valueOf(entry.oq) +
        " CD=" + String::valueOf(entry.cd) +
        " DR=" + String::valueOf(entry.dr) +
        " FL=" + String::valueOf(entry.fl) +
        " PE=" + String::valueOf(entry.pe) +
        " SR=" + String::valueOf(entry.sr) +
        " UT=" + String::valueOf(entry.ut) +
        " MA=" + String::valueOf(entry.ma) +
        " zones=" + (entry.zones.isEmpty() ? String("unknown") : entry.zones) +
        " despawns=" + String::valueOf(entry.despawned);

    return line;
}

static void loadMinerConfig(LuaObject& group, SimMinerConfig& minerConfig) {
    LuaObject config = group.getObjectField("minerConfig");

    if (!config.isValidTable()) {
        config.pop();
        return;
    }

    LuaObject resources = config.getObjectField("resources");
    if (resources.isValidTable()) {
        Vector<String> parsedResources;
        int resourceCount = resources.getTableSize();

        for (int i = 1; i <= resourceCount; ++i) {
            String resourceName = resources.getStringAt(i);

            if (!resourceName.isEmpty())
                parsedResources.add(resourceName);
        }

        if (parsedResources.size() > 0) {
            minerConfig.resources.removeAll();

            for (int i = 0; i < parsedResources.size(); ++i) {
                minerConfig.resources.add(parsedResources.get(i));
            }
        }
    }
    resources.pop();

    minerConfig.surveyDurationMs = clampMinerInt(config.getIntField("surveyDurationMs"), minerConfig.surveyDurationMs, 250, 300000);
    minerConfig.sampleDurationMs = clampMinerInt(config.getIntField("sampleDurationMs"), minerConfig.sampleDurationMs, 250, 300000);
    minerConfig.minSearchRadius = clampMinerInt(config.getIntField("minSearchRadius"), minerConfig.minSearchRadius, 1, 1000);
    minerConfig.maxSearchRadius = clampMinerInt(config.getIntField("maxSearchRadius"), minerConfig.maxSearchRadius, 1, 1000);
    minerConfig.fallbackRadius = clampMinerInt(config.getIntField("fallbackRadius"), minerConfig.fallbackRadius, 1, 1000);
    minerConfig.logStateTransitions = config.getBooleanField("logStateTransitions", minerConfig.logStateTransitions);

    LuaObject yieldConfig = config.getObjectField("yieldConfig");
    if (yieldConfig.isValidTable()) {
        minerConfig.yieldEnabled = yieldConfig.getBooleanField("enabled", minerConfig.yieldEnabled);
        minerConfig.minYieldAmount = clampMinerInt(yieldConfig.getIntField("minAmount"), minerConfig.minYieldAmount, 1, 1000000);
        minerConfig.maxYieldAmount = clampMinerInt(yieldConfig.getIntField("maxAmount"), minerConfig.maxYieldAmount, 1, 1000000);
        minerConfig.logYield = yieldConfig.getBooleanField("logYield", minerConfig.logYield);

        if (minerConfig.maxYieldAmount < minerConfig.minYieldAmount)
            minerConfig.maxYieldAmount = minerConfig.minYieldAmount;
    }
    yieldConfig.pop();

    LuaObject summaryConfig = config.getObjectField("summaryConfig");
    if (summaryConfig.isValidTable()) {
        minerConfig.summaryEnabled = summaryConfig.getBooleanField("enabled", minerConfig.summaryEnabled);
        minerConfig.summaryIntervalSeconds = clampMinerInt(summaryConfig.getIntField("intervalSeconds"), minerConfig.summaryIntervalSeconds, 30, 3600);
    }
    summaryConfig.pop();

    if (minerConfig.maxSearchRadius < minerConfig.minSearchRadius)
        minerConfig.maxSearchRadius = minerConfig.minSearchRadius;

    if (minerConfig.resources.size() == 0) {
        minerConfig.resources.add("iron");
        minerConfig.resources.add("gas");
        minerConfig.resources.add("water");
        minerConfig.resources.add("copper");
    }

    config.pop();
}

void SimPlayerManager::initialize() {
#ifdef DEBUG_SIMPLAYER
    info("Initializing SimPlayer Manager...", true);
    info("SimPlayerManager::initialize this=" + String::valueOf((uint64)this), true);
#endif

    loadLuaConfig();

    if (!enabled) {
#ifdef DEBUG_SIMPLAYER
        info("SimPlayerManager disabled; skipping spawns and periodic tasks.", true);
#endif
        return;
    }

    spawnConfiguredGroups();
    scheduleMinerSummaryTask();
    scheduleResourceIntelligenceTask();
    scheduleMinerTargetRecommendationTask();
    scheduleMinerTargetSimulationTask();
    scheduleMinerDensityTargetSimulationTask();
    scheduleMinerPathValidationSimulationTask();
    scheduleDemandProfileSimulationTask();
    scheduleMarketSupplyObservationTask();
    scheduleStockpileSnapshotSimulationTask();
    scheduleAiEconomyPersistenceTask();
    scheduleDemandStateSimulationTask();
    scheduleDemandWeightedMinerPlanSimulationTask();
    scheduleMinerIntelligentTargetingTask();
}

void SimPlayerManager::loadLuaConfig() {
#ifdef DEBUG_SIMPLAYER
    info("DEBUG: Attempting to run Lua file: scripts/managers/sim_player_manager.lua", true);
#endif

    // Fail closed if Lua loading or validation fails.
    enabled = false;

    struct LocationEntry {
        String planet;
        String name;
        float x = 0, y = 0, z = 0;
        float hx = 0, hy = 0, hz = 0;
    };

    try {
        lua->runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        error("DEBUG: CRITICAL LUA ERROR: " + e.getMessage());
        return;
    }

    LuaObject config = lua->getGlobalObject("SimPlayerManagerConfig");
    if (!config.isValidTable()) {
        error("DEBUG: 'SimPlayerManagerConfig' table NOT found!");
        return;
    }

    this->enabled = config.getBooleanField("enabled");
#ifdef DEBUG_SIMPLAYER
    info("DEBUG: Enabled == " + String::valueOf(this->enabled) + " For Load", true);
#endif
    if (!this->enabled) {
        config.pop();
        return;
    }

    allShuttleports.removeAll();
    spawnGroups.removeAll();
    minerSummaryLoggingEnabled = false;
    minerSummaryTaskScheduled = false;
    minerSummaryIntervalSeconds = 300;
    resourceIntelligenceEnabled = false;
    resourceIntelligenceLogTopResources = false;
    resourceIntelligenceTaskScheduled = false;
    resourceIntelligenceIntervalSeconds = 600;
    resourceIntelligenceTopN = 10;
    resourceScoringProfilesEnabled = false;
    resourceScoringProfileKeys.removeAll();
    minerTargetRecommendationsEnabled = false;
    minerTargetRecommendationTaskScheduled = false;
    minerTargetRecommendationIntervalSeconds = 300;
    minerTargetRecommendationTopN = 1;
    minerTargetRecommendationIncludeAllActiveMiners = true;
    minerTargetRecommendationProfileKeys.removeAll();
    minerTargetSimulationEnabled = false;
    minerTargetSimulationTaskScheduled = false;
    minerTargetSimulationIntervalSeconds = 300;
    minerTargetSimulationPreferSamePlanet = true;
    minerTargetSimulationSamePlanetBonus = 150;
    minerTargetSimulationTravelPenalty = 100;
    minerTargetSimulationAssignmentMode = "round_robin";
    minerTargetSimulationProfileWeights.removeAll();
    minerDensityTargetSimulationEnabled = false;
    minerDensityTargetSimulationTaskScheduled = false;
    minerDensityTargetSimulationIntervalSeconds = 300;
    minerDensityTargetSimulationSearchRadii.removeAll();
    minerDensityTargetSimulationSamplesPerRadius = 48;
    minerDensityTargetSimulationMinAcceptableDensity = 0.65f;
    minerDensityTargetSimulationPreferredDensity = 0.80f;
    minerDensityTargetSimulationRequireNavmesh = true;
    minerDensityTargetSimulationMaxPathCheckAttempts = 8;
    minerDensityTargetSimulationDistancePenaltyPerMeter = 0.02f;
    navAreaDensitySelectionEnabled = false;
    navAreaDensitySelectionShadowMode = true;
    navAreaSampleCacheTtlSeconds = 900;
    navAreaMaxSamplesPerArea = 8;
    navAreaMaxSampleAttemptsPerCycle = 16;
    navAreaMaxPathValidationsPerCycle = 0;
    navAreaAvoidGenericInteriors = true;
    navAreaPreferCityAndPoiRegions = true;
    reachabilityMemoryEnabled = true;
    reachabilityCandidatePreferenceEnabled = false;
    reachabilityMemoryTtlSeconds = 1800;
    reachabilityBucketSizeMeters = 128;
    reachabilityMinAttemptsBeforePenalty = 3;
    reachabilityVerifiedPathScoreBonus = 0.15f;
    reachabilitySampleCompleteScoreBonus = 0.25f;
    reachabilityRepeatedFailurePenalty = 0.25f;
    reachabilityLongDistancePenalty512Plus = 0.15f;
    reachabilityPlanetPenaltyEnabled = true;
    reachabilityResourcePenaltyEnabled = true;
    reachabilityMaxMemoryRows = 5000;
    minerPathValidationSimulationEnabled = false;
    minerPathValidationSimulationTaskScheduled = false;
    minerPathValidationSimulationIntervalSeconds = 300;
    minerPathValidationOnlyAcceptedDensityTargets = true;
    minerPathValidationMaxPathDistance = 2500;
    minerPathValidationMaxPathNodes = 256;
    demandProfileSimulationEnabled = false;
    demandProfileSimulationTaskScheduled = false;
    demandProfileSimulationIntervalSeconds = 300;
    demandProfileSimulationServerPhase = "mature_server";
    demandProfileSimulationLogTopN = 3;
    demandProfileSimulationProfileEnabled.removeAll();
    demandProfileSimulationProfileWeights.removeAll();
    demandProfileSimulationProfilePriorities.removeAll();
    demandStateSimulationEnabled = false;
    demandStateSimulationTaskScheduled = false;
    demandStateSimulationIntervalSeconds = 300;
    demandStateSimulationLogTopN = 3;
    demandStateSimulationSupplyMode = "conceptual_totals";
    demandStateSimulationActiveOpportunityWeight = 1.f;
    demandStateSimulationShortageWeight = 1.f;
    demandStateSimulationSurplusDampening = 0.5f;
    demandStateSimulationProfileEnabled.removeAll();
    demandStateSimulationDesiredReserve.removeAll();
    demandStateSimulationLowStockThreshold.removeAll();
    demandStateSimulationCriticalStockThreshold.removeAll();
    marketSupplyObservationEnabled = false;
    marketSupplyObservationTaskScheduled = false;
    marketSupplyObservationIntervalSeconds = 300;
    marketSupplyObservationMaxListingsScanned = 5000;
    marketSupplyObservationIncludeBazaar = true;
    marketSupplyObservationIncludePlayerVendors = true;
    marketSupplyObservationIncludeVendorStockrooms = false;
    marketSupplyObservationIncludePlayerInventory = false;
    marketSupplyObservationIncludePrivateContainers = false;
    marketSupplyObservationMinQuantity = 1;
    marketSupplyObservationLogTopN = 5;
    clearMarketSupplyObservationSnapshot();
    stockpileSnapshotSimulationEnabled = false;
    stockpileSnapshotSimulationTaskScheduled = false;
    stockpileSnapshotSimulationIntervalSeconds = 300;
    stockpileSnapshotSimulationLogTopN = 10;
    stockpileSnapshotSimulationIncludeConceptualMinerTotals = true;
    stockpileSnapshotSimulationIncludeMarketObservation = false;
    aiEconomyPersistConceptualMinerTotals = false;
    aiEconomyPersistenceTaskScheduled = false;
    aiEconomyPersistenceLogSummary = true;
    aiEconomyPersistenceFailureLogged = false;
    aiEconomyPersistenceIntervalSeconds = 300;
    persistentStockpileDemandEnabled = false;
    persistentStockpileDemandIncludeConceptualMinerLots = true;
    persistentStockpileDemandLogSummary = true;
    demandWeightedMinerPlanSimulationEnabled = false;
    demandWeightedMinerPlanSimulationTaskScheduled = false;
    demandWeightedMinerPlanSimulationIntervalSeconds = 300;
    demandWeightedMinerPlanSimulationLogTopN = 20;
    demandWeightedMinerPlanSimulationSamePlanetBonus = 150;
    demandWeightedMinerPlanSimulationTravelPenalty = 100;
    demandWeightedMinerPlanSimulationMaxMinersPerProfile = 2;
    demandWeightedMinerPlanSimulationMinimumPressureThreshold = 1.f;
    demandWeightedMinerPlanSimulationStrongPressureRatio = 1.5f;
    demandWeightedMinerPlanSimulationServerPhase = "mature_server";
    demandWeightedMinerPlanSimulationActiveOpportunityWeight = 1.f;
    demandWeightedMinerPlanSimulationShortageWeight = 1.f;
    demandWeightedMinerPlanSimulationSurplusDampening = 0.5f;
    demandWeightedMinerPlanSimulationIncludeMarketSupply = false;
    demandWeightedMinerPlanSimulationProfileEnabled.removeAll();
    demandWeightedMinerPlanSimulationDesiredReserve.removeAll();
    demandWeightedMinerPlanSimulationLowStockThreshold.removeAll();
    demandWeightedMinerPlanSimulationCriticalStockThreshold.removeAll();
    aiTravelSimulationEnabled = true;
    aiTravelSimulationMaxPlans = 20;
    aiTravelSimulationIncludeResourceRushPlans = true;
    aiTravelSimulationIncludeHubReturnPlans = true;
    aiTravelSimulationHomeHubEnabled = true;
    aiTravelSimulationHomeHubKey = "coronet_resource_hub";
    aiTravelSimulationHomeHubZone = "corellia";
    aiTravelSimulationHomeHubCity = "coronet";
    aiTravelSimulationHomeHubX = -155.f;
    aiTravelSimulationHomeHubY = -4722.f;
    aiTravelSimulationHomeHubPurpose = "sell_resources";
    minerIntelligentTargetingEnabled = false;
    minerIntelligentTargetingTaskScheduled = false;
    minerIntelligentTargetingMode = "off";
    minerIntelligentTargetingIntervalSeconds = 300;
    minerIntelligentTargetingMaxActiveMiners = 1;
    minerIntelligentTargetingRequireDemandWeightedPlan = true;
    minerIntelligentTargetingRequireAcceptedDensityTarget = true;
    minerIntelligentTargetingRequireValidPath = true;
    minerIntelligentTargetingFallbackToConceptualLoop = true;
    minerIntelligentTargetingRollbackOnFailureCount = 3;
    minerIntelligentTargetingLogDecisionSummary = true;
    minerIntelligentTargetingAssignmentEnabled = true;
    minerIntelligentTargetingAssignmentTtlSeconds = 30;
    minerIntelligentTargetingCandidateAssignmentTtlSeconds = 180;
    minerIntelligentTargetingValidatedAssignmentTtlSeconds = 180;
    minerIntelligentTargetingQueuedActivationTtlSeconds = 120;
    minerIntelligentTargetingMovementArrivalTimeoutSeconds = 600;
    minerIntelligentTargetingMovementArrivalTimeoutMinSeconds = 240;
    minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds = 1200;
    minerIntelligentTargetingMovementArrivalSecondsPerMeter = 0.75f;
    minerIntelligentTargetingSampleStartedTimeoutSeconds = 180;
    minerIntelligentTargetingPreventNormalTtlForActiveMovement = true;
    minerIntelligentTargetingAssignmentReplaceOnlyWhenExpiredOrInvalid = true;
    minerIntelligentTargetingAssignmentClearOnSampleComplete = true;
    minerIntelligentTargetingAssignmentClearOnCombat = true;
    minerIntelligentTargetingAssignmentClearOnIncapOrDeath = true;
    minerIntelligentTargetingAssignmentClearOnZoneChange = true;
    minerIntelligentTargetingAssignmentLogLifecycle = true;
    minerIntelligentTargetingAssignmentLogRetained = false;
    minerMovementReadinessDiagnosticsEnabled = true;
    minerIntelligentTargetingLimitedActivationEnabled = false;
    minerIntelligentTargetingLimitedMaxActivationsPerInterval = 1;
    minerIntelligentTargetingLimitedRequireSamePlanet = true;
    minerIntelligentTargetingLimitedDisableOnFirstFailure = true;
    minerIntelligentTargetingLimitedLogActivationLifecycle = true;

    {
        Locker failureLocker(&minerIntelligentTargetingFailureMutex);
        minerIntelligentTargetingFailureCounts.removeAll();
    }

    {
        Locker assignmentLocker(&minerIntelligentTargetingAssignmentMutex);
        minerIntelligentTargetAssignments.removeAll();
        nextMinerAssignmentGenerationId = 1;
    }

    {
        Locker pathSnapshotLocker(&minerPathValidationSnapshotMutex);
        minerPathValidationSnapshots.removeAll();
        nextMinerPathValidationSnapshotId = 1;
    }

    {
        Locker historyLocker(&recentMinerAssignmentHistoryMutex);
        recentMinerAssignmentHistory.removeAll();
    }

    const char* demandProfileKeys[] = {
        "composite_armor_supply",
        "master_weaponsmith_staples",
        "high_damage_weapon_components",
        "chef_buff_foods",
        "chef_high_value_consumables",
        "production_infrastructure"
    };
    const int demandProfileDefaultPriorities[] = {100, 80, 90, 70, 70, 85};
    const int demandStateDefaultReserves[] = {5000, 3000, 3000, 5000, 3000, 10000};

    for (int i = 0; i < 6; ++i) {
        String profileKey = demandProfileKeys[i];
        demandProfileSimulationProfileEnabled.put(profileKey, 1);
        demandProfileSimulationProfileWeights.put(profileKey, 1.f);
        demandProfileSimulationProfilePriorities.put(profileKey, demandProfileDefaultPriorities[i]);
        demandStateSimulationProfileEnabled.put(profileKey, 1);
        demandStateSimulationDesiredReserve.put(profileKey, demandStateDefaultReserves[i]);
        demandStateSimulationLowStockThreshold.put(profileKey, 0.35f);
        demandStateSimulationCriticalStockThreshold.put(profileKey, 0.10f);
        demandWeightedMinerPlanSimulationProfileEnabled.put(profileKey, 1);
        demandWeightedMinerPlanSimulationDesiredReserve.put(
            profileKey, demandStateDefaultReserves[i]);
        demandWeightedMinerPlanSimulationLowStockThreshold.put(profileKey, 0.35f);
        demandWeightedMinerPlanSimulationCriticalStockThreshold.put(profileKey, 0.10f);
    }

    LuaObject resourceIntelligenceConfig = config.getObjectField("resourceIntelligenceConfig");
    if (resourceIntelligenceConfig.isValidTable()) {
        resourceIntelligenceEnabled = resourceIntelligenceConfig.getBooleanField("enabled", resourceIntelligenceEnabled);
        resourceIntelligenceLogTopResources = resourceIntelligenceConfig.getBooleanField("logTopResources", resourceIntelligenceLogTopResources);
        resourceIntelligenceIntervalSeconds = clampMinerInt(resourceIntelligenceConfig.getIntField("summaryIntervalSeconds"), resourceIntelligenceIntervalSeconds, 30, 3600);
        resourceIntelligenceTopN = clampMinerInt(resourceIntelligenceConfig.getIntField("topN"), resourceIntelligenceTopN, 1, 50);
    }
    resourceIntelligenceConfig.pop();

    LuaObject resourceScoringProfilesConfig = config.getObjectField("resourceScoringProfiles");
    if (resourceScoringProfilesConfig.isValidTable()) {
        resourceScoringProfilesEnabled = resourceScoringProfilesConfig.getBooleanField("enabled", resourceScoringProfilesEnabled);

        LuaObject profiles = resourceScoringProfilesConfig.getObjectField("profiles");
        if (profiles.isValidTable()) {
            int profileCount = profiles.getTableSize();

            for (int i = 1; i <= profileCount; ++i) {
                LuaObject profile = profiles.getObjectAt(i);

                if (profile.isValidTable()) {
                    String key = profile.getStringField("key");

                    if (!key.isEmpty())
                        resourceScoringProfileKeys.add(key);
                }

                profile.pop();
            }
        }
        profiles.pop();
    }
    resourceScoringProfilesConfig.pop();

    LuaObject minerTargetRecommendationConfig = config.getObjectField("minerTargetRecommendationConfig");
    if (minerTargetRecommendationConfig.isValidTable()) {
        minerTargetRecommendationsEnabled = minerTargetRecommendationConfig.getBooleanField("enabled", minerTargetRecommendationsEnabled);
        minerTargetRecommendationIntervalSeconds = clampMinerInt(minerTargetRecommendationConfig.getIntField("intervalSeconds"), minerTargetRecommendationIntervalSeconds, 30, 3600);
        minerTargetRecommendationTopN = clampMinerInt(minerTargetRecommendationConfig.getIntField("topN"), minerTargetRecommendationTopN, 1, 10);
        minerTargetRecommendationIncludeAllActiveMiners = minerTargetRecommendationConfig.getBooleanField("includeAllActiveMiners", minerTargetRecommendationIncludeAllActiveMiners);

        LuaObject profiles = minerTargetRecommendationConfig.getObjectField("profiles");
        if (profiles.isValidTable()) {
            int profileCount = profiles.getTableSize();

            for (int i = 1; i <= profileCount; ++i) {
                String key = profiles.getStringAt(i);

                if (!key.isEmpty())
                    minerTargetRecommendationProfileKeys.add(key);
            }
        }
        profiles.pop();
    }
    minerTargetRecommendationConfig.pop();

    LuaObject minerTargetSimulationConfig = config.getObjectField("minerTargetSimulationConfig");
    if (minerTargetSimulationConfig.isValidTable()) {
        minerTargetSimulationEnabled = minerTargetSimulationConfig.getBooleanField("enabled", minerTargetSimulationEnabled);
        minerTargetSimulationIntervalSeconds = clampMinerInt(minerTargetSimulationConfig.getIntField("intervalSeconds"), minerTargetSimulationIntervalSeconds, 30, 3600);
        minerTargetSimulationPreferSamePlanet = minerTargetSimulationConfig.getBooleanField("preferSamePlanet", minerTargetSimulationPreferSamePlanet);
        minerTargetSimulationSamePlanetBonus = clampIntRange(
            static_cast<int>(minerTargetSimulationConfig.getFloatField("samePlanetBonus", static_cast<float>(minerTargetSimulationSamePlanetBonus))), 0, 1000);
        minerTargetSimulationTravelPenalty = clampIntRange(
            static_cast<int>(minerTargetSimulationConfig.getFloatField("travelPenalty", static_cast<float>(minerTargetSimulationTravelPenalty))), 0, 1000);

        String assignmentMode = minerTargetSimulationConfig.getStringField("assignmentMode");

        if (assignmentMode == "round_robin")
            minerTargetSimulationAssignmentMode = assignmentMode;

        LuaObject profileWeights = minerTargetSimulationConfig.getObjectField("profileWeights");
        if (profileWeights.isValidTable()) {
            const char* profileKeys[] = {"weaponsmith_dl44", "chef_ahrisa", "architect_mining_unit"};

            for (const char* profileKey : profileKeys) {
                float weight = profileWeights.getFloatField(profileKey, 1.0f);

                if (weight < 0.f)
                    weight = 0.f;
                else if (weight > 10.f)
                    weight = 10.f;

                minerTargetSimulationProfileWeights.put(String(profileKey), weight);
            }
        }
        profileWeights.pop();
    }
    minerTargetSimulationConfig.pop();

    LuaObject minerDensityTargetSimulationConfig = config.getObjectField("minerDensityTargetSimulationConfig");
    if (minerDensityTargetSimulationConfig.isValidTable()) {
        minerDensityTargetSimulationEnabled = minerDensityTargetSimulationConfig.getBooleanField("enabled", minerDensityTargetSimulationEnabled);
        minerDensityTargetSimulationIntervalSeconds = clampMinerInt(
            minerDensityTargetSimulationConfig.getIntField("intervalSeconds"), minerDensityTargetSimulationIntervalSeconds, 30, 3600);
        minerDensityTargetSimulationSamplesPerRadius = clampMinerInt(
            minerDensityTargetSimulationConfig.getIntField("samplesPerRadius"), minerDensityTargetSimulationSamplesPerRadius, 8, 256);
        minerDensityTargetSimulationMinAcceptableDensity = minerDensityTargetSimulationConfig.getFloatField(
            "minAcceptableDensity", minerDensityTargetSimulationMinAcceptableDensity);
        minerDensityTargetSimulationPreferredDensity = minerDensityTargetSimulationConfig.getFloatField(
            "preferredDensity", minerDensityTargetSimulationPreferredDensity);
        minerDensityTargetSimulationRequireNavmesh = minerDensityTargetSimulationConfig.getBooleanField(
            "requireNavmesh", minerDensityTargetSimulationRequireNavmesh);
        minerDensityTargetSimulationMaxPathCheckAttempts = clampMinerInt(
            minerDensityTargetSimulationConfig.getIntField("maxPathCheckAttempts"), minerDensityTargetSimulationMaxPathCheckAttempts, 1, 64);
        minerDensityTargetSimulationDistancePenaltyPerMeter = minerDensityTargetSimulationConfig.getFloatField(
            "distancePenaltyPerMeter", minerDensityTargetSimulationDistancePenaltyPerMeter);

        if (minerDensityTargetSimulationMinAcceptableDensity < 0.f)
            minerDensityTargetSimulationMinAcceptableDensity = 0.f;
        else if (minerDensityTargetSimulationMinAcceptableDensity > 1.f)
            minerDensityTargetSimulationMinAcceptableDensity = 1.f;

        if (minerDensityTargetSimulationPreferredDensity < minerDensityTargetSimulationMinAcceptableDensity)
            minerDensityTargetSimulationPreferredDensity = minerDensityTargetSimulationMinAcceptableDensity;
        else if (minerDensityTargetSimulationPreferredDensity > 1.f)
            minerDensityTargetSimulationPreferredDensity = 1.f;

        if (minerDensityTargetSimulationDistancePenaltyPerMeter < 0.f)
            minerDensityTargetSimulationDistancePenaltyPerMeter = 0.f;
        else if (minerDensityTargetSimulationDistancePenaltyPerMeter > 1.f)
            minerDensityTargetSimulationDistancePenaltyPerMeter = 1.f;

        LuaObject searchRadii = minerDensityTargetSimulationConfig.getObjectField("searchRadii");
        if (searchRadii.isValidTable()) {
            int radiusCount = searchRadii.getTableSize();

            for (int i = 1; i <= radiusCount; ++i) {
                int radius = searchRadii.getIntAt(i);

                if (radius >= 25 && radius <= 10000)
                    minerDensityTargetSimulationSearchRadii.add(radius);
            }
        }
        searchRadii.pop();
    }
    minerDensityTargetSimulationConfig.pop();

    LuaObject navAreaDensitySelectionConfig =
        config.getObjectField("navAreaDensitySelectionConfig");
    if (navAreaDensitySelectionConfig.isValidTable()) {
        navAreaDensitySelectionEnabled =
            navAreaDensitySelectionConfig.getBooleanField(
                "enableNavAreaDensitySelection",
                navAreaDensitySelectionEnabled);
        navAreaDensitySelectionShadowMode =
            navAreaDensitySelectionConfig.getBooleanField(
                "enableNavAreaDensityShadowMode",
                navAreaDensitySelectionShadowMode);
        navAreaSampleCacheTtlSeconds = clampMinerInt(
            navAreaDensitySelectionConfig.getIntField(
                "navAreaSampleCacheTtlSeconds"),
            navAreaSampleCacheTtlSeconds,
            30,
            7200);
        navAreaMaxSamplesPerArea = clampMinerInt(
            navAreaDensitySelectionConfig.getIntField(
                "navAreaMaxSamplesPerArea"),
            navAreaMaxSamplesPerArea,
            1,
            64);
        navAreaMaxSampleAttemptsPerCycle = clampMinerInt(
            navAreaDensitySelectionConfig.getIntField(
                "navAreaMaxSampleAttemptsPerCycle"),
            navAreaMaxSampleAttemptsPerCycle,
            0,
            512);
        navAreaMaxPathValidationsPerCycle = clampMinerInt(
            navAreaDensitySelectionConfig.getIntField(
                "navAreaMaxPathValidationsPerCycle"),
            navAreaMaxPathValidationsPerCycle,
            0,
            128);
        navAreaAvoidGenericInteriors =
            navAreaDensitySelectionConfig.getBooleanField(
                "navAreaAvoidGenericInteriors",
                navAreaAvoidGenericInteriors);
        navAreaPreferCityAndPoiRegions =
            navAreaDensitySelectionConfig.getBooleanField(
                "navAreaPreferCityAndPoiRegions",
                navAreaPreferCityAndPoiRegions);
    }
    navAreaDensitySelectionConfig.pop();

    LuaObject reachabilityMemoryConfig =
        config.getObjectField("reachabilityMemoryConfig");
    if (reachabilityMemoryConfig.isValidTable()) {
        reachabilityMemoryEnabled =
            reachabilityMemoryConfig.getBooleanField(
                "enableReachabilityMemory",
                reachabilityMemoryEnabled);
        reachabilityCandidatePreferenceEnabled =
            reachabilityMemoryConfig.getBooleanField(
                "enableReachabilityCandidatePreference",
                reachabilityCandidatePreferenceEnabled);
        reachabilityMemoryTtlSeconds = clampMinerInt(
            reachabilityMemoryConfig.getIntField(
                "reachabilityMemoryTtlSeconds"),
            reachabilityMemoryTtlSeconds,
            60,
            86400);
        reachabilityBucketSizeMeters = clampMinerInt(
            reachabilityMemoryConfig.getIntField(
                "reachabilityBucketSizeMeters"),
            reachabilityBucketSizeMeters,
            16,
            1024);
        reachabilityMinAttemptsBeforePenalty = clampMinerInt(
            reachabilityMemoryConfig.getIntField(
                "minAttemptsBeforePenalty"),
            reachabilityMinAttemptsBeforePenalty,
            1,
            100);
        reachabilityVerifiedPathScoreBonus =
            clampFloatRange(
                reachabilityMemoryConfig.getFloatField(
                    "verifiedPathScoreBonus",
                    reachabilityVerifiedPathScoreBonus),
                0.f,
                2.f);
        reachabilitySampleCompleteScoreBonus =
            clampFloatRange(
                reachabilityMemoryConfig.getFloatField(
                    "sampleCompleteScoreBonus",
                    reachabilitySampleCompleteScoreBonus),
                0.f,
                2.f);
        reachabilityRepeatedFailurePenalty =
            clampFloatRange(
                reachabilityMemoryConfig.getFloatField(
                    "repeatedFailurePenalty",
                    reachabilityRepeatedFailurePenalty),
                0.f,
                2.f);
        reachabilityLongDistancePenalty512Plus =
            clampFloatRange(
                reachabilityMemoryConfig.getFloatField(
                    "longDistancePenalty512Plus",
                    reachabilityLongDistancePenalty512Plus),
                0.f,
                2.f);
        reachabilityPlanetPenaltyEnabled =
            reachabilityMemoryConfig.getBooleanField(
                "planetPenaltyEnabled",
                reachabilityPlanetPenaltyEnabled);
        reachabilityResourcePenaltyEnabled =
            reachabilityMemoryConfig.getBooleanField(
                "resourcePenaltyEnabled",
                reachabilityResourcePenaltyEnabled);
        reachabilityMaxMemoryRows = clampMinerInt(
            reachabilityMemoryConfig.getIntField(
                "maxReachabilityMemoryRows"),
            reachabilityMaxMemoryRows,
            100,
            50000);
    }
    reachabilityMemoryConfig.pop();

    if (minerDensityTargetSimulationSearchRadii.size() == 0) {
        minerDensityTargetSimulationSearchRadii.add(250);
        minerDensityTargetSimulationSearchRadii.add(500);
        minerDensityTargetSimulationSearchRadii.add(1000);
        minerDensityTargetSimulationSearchRadii.add(2000);
    }

    for (int i = 0; i < minerDensityTargetSimulationSearchRadii.size(); ++i) {
        for (int j = i + 1; j < minerDensityTargetSimulationSearchRadii.size(); ++j) {
            int left = minerDensityTargetSimulationSearchRadii.get(i);
            int right = minerDensityTargetSimulationSearchRadii.get(j);

            if (right < left) {
                minerDensityTargetSimulationSearchRadii.set(i, right);
                minerDensityTargetSimulationSearchRadii.set(j, left);
            }
        }
    }

    LuaObject minerPathValidationSimulationConfig = config.getObjectField("minerPathValidationSimulationConfig");
    if (minerPathValidationSimulationConfig.isValidTable()) {
        minerPathValidationSimulationEnabled = minerPathValidationSimulationConfig.getBooleanField(
            "enabled", minerPathValidationSimulationEnabled);
        minerPathValidationSimulationIntervalSeconds = clampMinerInt(
            minerPathValidationSimulationConfig.getIntField("intervalSeconds"),
            minerPathValidationSimulationIntervalSeconds, 30, 3600);
        minerPathValidationOnlyAcceptedDensityTargets = minerPathValidationSimulationConfig.getBooleanField(
            "validateOnlyAcceptedDensityTargets", minerPathValidationOnlyAcceptedDensityTargets);
        minerPathValidationMaxPathDistance = clampMinerInt(
            minerPathValidationSimulationConfig.getIntField("maxPathDistance"),
            minerPathValidationMaxPathDistance, 100, 10000);
        minerPathValidationMaxPathNodes = clampMinerInt(
            minerPathValidationSimulationConfig.getIntField("maxPathNodes"),
            minerPathValidationMaxPathNodes, 2, 2048);
    }
    minerPathValidationSimulationConfig.pop();

    LuaObject demandProfileSimulationConfig = config.getObjectField("demandProfileSimulationConfig");
    if (demandProfileSimulationConfig.isValidTable()) {
        demandProfileSimulationEnabled = demandProfileSimulationConfig.getBooleanField(
            "enabled", demandProfileSimulationEnabled);
        demandProfileSimulationIntervalSeconds = clampMinerInt(
            demandProfileSimulationConfig.getIntField("intervalSeconds"),
            demandProfileSimulationIntervalSeconds, 30, 3600);
        demandProfileSimulationLogTopN = clampMinerInt(
            demandProfileSimulationConfig.getIntField("logTopN"),
            demandProfileSimulationLogTopN, 1, 20);

        String serverPhase = demandProfileSimulationConfig.getStringField("serverPhase");

        if (serverPhase == "early_server" || serverPhase == "mature_server" ||
                serverPhase == "resource_rush" || serverPhase == "stockpile_phase") {
            demandProfileSimulationServerPhase = serverPhase;
        }

        LuaObject profiles = demandProfileSimulationConfig.getObjectField("profiles");
        if (profiles.isValidTable()) {
            for (int i = 0; i < 6; ++i) {
                String profileKey = demandProfileKeys[i];
                LuaObject profile = profiles.getObjectField(profileKey);

                if (profile.isValidTable()) {
                    int profileEnabled = profile.getBooleanField("enabled", true) ? 1 : 0;
                    float weight = clampFloatRange(profile.getFloatField("weight", 1.f), 0.f, 10.f);
                    int priority = clampIntRange(
                        static_cast<int>(profile.getFloatField(
                            "priority", static_cast<float>(demandProfileDefaultPriorities[i]))),
                        0, 1000);

                    demandProfileSimulationProfileEnabled.put(profileKey, profileEnabled);
                    demandProfileSimulationProfileWeights.put(profileKey, weight);
                    demandProfileSimulationProfilePriorities.put(profileKey, priority);
                }

                profile.pop();
            }
        }
        profiles.pop();
    }
    demandProfileSimulationConfig.pop();

    LuaObject demandStateSimulationConfig = config.getObjectField("demandStateSimulationConfig");
    if (demandStateSimulationConfig.isValidTable())
        applyDemandStateSimulationConfig(demandStateSimulationConfig);
    demandStateSimulationConfig.pop();

    LuaObject marketSupplyObservationConfig = config.getObjectField("marketSupplyObservationConfig");
    if (marketSupplyObservationConfig.isValidTable())
        applyMarketSupplyObservationConfig(marketSupplyObservationConfig);
    marketSupplyObservationConfig.pop();

    LuaObject stockpileSnapshotSimulationConfig =
        config.getObjectField("stockpileSnapshotSimulationConfig");
    if (stockpileSnapshotSimulationConfig.isValidTable())
        applyStockpileSnapshotSimulationConfig(stockpileSnapshotSimulationConfig);
    stockpileSnapshotSimulationConfig.pop();

    LuaObject aiEconomyPersistenceConfig =
        config.getObjectField("aiEconomyPersistenceConfig");
    if (aiEconomyPersistenceConfig.isValidTable())
        applyAiEconomyPersistenceConfig(aiEconomyPersistenceConfig);
    aiEconomyPersistenceConfig.pop();

    LuaObject persistentStockpileDemandConfig =
        config.getObjectField("persistentStockpileDemandConfig");
    if (persistentStockpileDemandConfig.isValidTable())
        applyPersistentStockpileDemandConfig(
            persistentStockpileDemandConfig);
    persistentStockpileDemandConfig.pop();

    LuaObject demandWeightedMinerPlanSimulationConfig =
        config.getObjectField("demandWeightedMinerPlanSimulationConfig");
    if (demandWeightedMinerPlanSimulationConfig.isValidTable())
        applyDemandWeightedMinerPlanSimulationConfig(
            demandWeightedMinerPlanSimulationConfig);
    demandWeightedMinerPlanSimulationConfig.pop();
    applyDemandWeightedMinerPlanDependencyConfig(config);

    LuaObject aiTravelSimulationConfig =
        config.getObjectField("aiTravelSimulationConfig");
    if (aiTravelSimulationConfig.isValidTable())
        applyAiTravelSimulationConfig(aiTravelSimulationConfig);
    aiTravelSimulationConfig.pop();

    LuaObject stationedMinerConfig =
        config.getObjectField("stationedMinerConfig");
    if (stationedMinerConfig.isValidTable())
        applyStationedMinerConfig(stationedMinerConfig);
    stationedMinerConfig.pop();

    LuaObject minerIntelligentTargetingConfig =
        config.getObjectField("minerIntelligentTargetingConfig");
    if (minerIntelligentTargetingConfig.isValidTable())
        applyMinerIntelligentTargetingConfig(minerIntelligentTargetingConfig);
    minerIntelligentTargetingConfig.pop();

    // --- LOAD SHUTTLEPORTS ---
    LuaObject shuttles = config.getObjectField("shuttleports");
    if (shuttles.isValidTable()) {
        const char* planets[] = {"naboo", "tatooine", "corellia", "dantooine", "talus", "rori", "lok", "yavin4", "endor", "dathomir"};

        for (const char* pName : planets) {
            LuaObject planetTable = shuttles.getObjectField(pName);

            if (planetTable.isValidTable()) {
                int cityCount = planetTable.getTableSize();
#ifdef DEBUG_SIMPLAYER
                info("DEBUG: Found " + String::valueOf(cityCount) + " entries for planet: " + String(pName), true);
#endif
                for (int j = 1; j <= cityCount; ++j) {
                    LuaObject city = planetTable.getObjectAt(j);

                    if (city.isValidTable()) {
                        String cityName = city.getStringField("name");
                        if (cityName.isEmpty()) cityName = "Unknown/Unnamed";

                        LocationEntry entry;
                        entry.planet = pName;
                        entry.name = cityName;
                        bool validSpawn = false;

                        // 1. READ SPAWN (Then POP it immediately to clear stack)
                        LuaObject spawn = city.getObjectField("spawn");
                        if (spawn.isValidTable()) {
                            entry.x = spawn.getFloatAt(1);
                            entry.y = spawn.getFloatAt(2);
                            entry.z = spawn.getFloatAt(3);
                            validSpawn = true;
#ifdef DEBUG_SIMPLAYER
                            info("DEBUG: Loaded spawn for " + cityName + ": " + String::valueOf(entry.x) + "," + String::valueOf(entry.y) + "," + String::valueOf(entry.z), true);

                        } else {
                            error("DEBUG: Entry #" + String::valueOf(j) + " (" + cityName + ") in " + String(pName) + " is invalid! Check 'spawn' table.");
#endif
                        }
                        spawn.pop();

                        // 2. READ HANGOUT
                        if (validSpawn) {
                            LuaObject hangout = city.getObjectField("hangout");
                            if (hangout.isValidTable()) {
                                entry.hx = hangout.getFloatAt(1);
                                entry.hy = hangout.getFloatAt(2);
                                entry.hz = hangout.getFloatAt(3);
#ifdef DEBUG_SIMPLAYER
                                info("DEBUG: Loaded Hangout for " + cityName + ": " + String::valueOf(entry.hx) + "," + String::valueOf(entry.hy), true);
#endif
                            } else {
                                // Fallback: Hangout = Spawn
#ifdef DEBUG_SIMPLAYER
                                info("DEBUG: No 'hangout' table found for " + cityName + ". Defaulting to spawn.", true);
#endif
                                entry.hx = entry.x;
                                entry.hy = entry.y;
                                entry.hz = entry.z;
                            }
                            hangout.pop(); // Pop hangout

                            ShuttleportLocation loc;
                            loc.planet = entry.planet;
                            loc.name = entry.name;
                            loc.spawn = Vector3(entry.x, entry.y, entry.z);
                            loc.hangout = Vector3(entry.hx, entry.hy, entry.hz);
                            allShuttleports.add(loc);
                        }
                    }
                    city.pop();
                }
            }
            planetTable.pop();
        }
    }
    shuttles.pop();

    if (allShuttleports.size() == 0) {
#ifdef DEBUG_SIMPLAYER
        error("DEBUG: ABORTING - No valid spawn locations found.");
#endif
        config.pop();
        return;
    }
#ifdef DEBUG_SIMPLAYER
    info("DEBUG: Successfully loaded " + String::valueOf(allShuttleports.size()) + " spawn locations.", true);
#endif
    // --- PROCESS GROUPS ---
    LuaObject groups = config.getObjectField("spawnGroups");
    if (groups.isValidTable()) {
        int groupCount = groups.getTableSize();

        for (int i = 1; i <= groupCount; ++i) {
            LuaObject group = groups.getObjectAt(i);
            String type = group.getStringField("type");
            SpawnGroup g;
            g.type = group.getStringField("type");
            g.totalCount = group.getIntField("totalCount");
            g.behavior = group.getStringField("behavior");
            g.faction = group.getStringField("faction");

            LuaObject templates = group.getObjectField("templates");
            if (templates.isValidTable()) {
                int tSize = templates.getTableSize();
                for (int t = 1; t <= tSize; ++t) {
                    g.templates.add(templates.getStringAt(t));
                }
            }
            templates.pop();

            loadMinerConfig(group, g.minerConfig);
            if (!g.type.beginsWith("pvp") && g.minerConfig.summaryEnabled) {
                if (!minerSummaryLoggingEnabled || g.minerConfig.summaryIntervalSeconds < minerSummaryIntervalSeconds)
                    minerSummaryIntervalSeconds = g.minerConfig.summaryIntervalSeconds;

                minerSummaryLoggingEnabled = true;
            }
#ifdef DEBUG_SIMPLAYER
            info("DEBUG: Loaded Group " + String::valueOf(i) + " (" + g.type + ") totalCount=" + String::valueOf(g.totalCount), true);
#endif
            spawnGroups.add(g);
            group.pop();
        }
    }
    groups.pop();
    config.pop();
}

void SimPlayerManager::spawnSimPlayer(const String& planet, float x, float y, const String& templateName) {
    if (!enabled)
        return;

    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return;

    Zone* zone = zoneServer->getZone(planet);
    if (zone == nullptr) {
        info("spawnSimPlayer: Could not find zone: " + planet);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr) return;

    float z = zone->getHeight(x, y);

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, x, z, y, 0);
    if (creature == nullptr) {
        info("spawnSimPlayer: Failed to spawn SimPlayer template: " + templateName);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr) return;

    // --- COSMETICS ---
    NameManager* nm = zoneServer->getNameManager();
    if (nm != nullptr) {
        // Use Type 0 to generate a Human Name (First Last) instead of TK-123
        String name = nm->makeCreatureName(0, creature->getSpecies());
        if (!name.isEmpty()) {
            agent->setCustomObjectName(name, true);
        }
    }

    // Reset default flags to clean slate
    agent->setCreatureBitmask(0);
    agent->setDespawnOnNoPlayerInRange(false);

    toggleBot(agent);
}

void SimPlayerManager::toggleBot(AiAgent* agent) {
    if (agent == nullptr) return;

    uint64 oid = agent->getObjectID();

    if (controllers.contains(oid)) {
#ifdef DEBUG_SIMPLAYER
        info("Stopping SimPlayer for agent " + String::valueOf(oid), true);
#endif
        agent->eraseBlackboard("simAlwaysActive");
        controllers.drop(oid);

        agent->clearPatrolPoints();
        agent->clearSavedPatrolPoints();
        agent->setMovementState(AiAgent::OBLIVIOUS);
        agent->activateAiBehavior(true);
        agent->setSimPlayerBot(false);

        agent->destroyObjectFromWorld(true);
        agent->destroyObjectFromDatabase(true);
        return;
    } else {
        if (!enabled)
            return;

#ifdef DEBUG_SIMPLAYER
        info("Starting SimPlayer for agent " + String::valueOf(oid), true);
#endif
        agent->setCustomAiMap(String("patrol").hashCode());
        agent->setAITemplate();

        agent->writeBlackboard("simAlwaysActive", true);
        agent->setSimAlwaysActive(true);
        agent->setSimPlayerBot(true);
        agent->setDespawnOnNoPlayerInRange(false);

        Reference<SimPlayerController*> ctrl = nullptr;
#ifdef DEBUG_SIMPLAYER
        info("toggleBot: creating PvP controller using DEFAULT route (no spawn/hangout)", true);
#endif
        const CreatureTemplate* tmpl = agent->getCreatureTemplate();
        String tName = (tmpl != nullptr) ? tmpl->getTemplateName() : "";

        // NOTE: Keep this logic broad so new PvP templates (e.g. rebel_commando)
        // still get the correct controller when spawned via spawnSimPlayer().
        String lower = tName.toLowerCase();
        bool looksRebel = lower.beginsWith("rebel") || lower.contains("specforce") || lower.contains("light");
        bool looksImperial = lower.beginsWith("imperial") || lower.contains("stormtrooper") || lower.contains("dark");

        if (looksImperial) {
            agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

            SimPvPController* pvp = new SimPvPController(agent, true);

            // Give it enough context to cycle
            String zoneName = (agent->getZone() != nullptr) ? agent->getZone()->getZoneName() : "unknown";
            pvp->setCycleContext(this, tName, "pvp_solo", zoneName, "toggleBot");

            ctrl = pvp;

        } else if (looksRebel) {
            agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

            SimPvPController* pvp = new SimPvPController(agent, false);

            String zoneName = (agent->getZone() != nullptr) ? agent->getZone()->getZoneName() : "unknown";
            pvp->setCycleContext(this, tName, "pvp_solo", zoneName, "toggleBot");

            ctrl = pvp;
        } else {
             agent->setPvpStatusBitmask(0);
             ctrl = new SimMinerController(agent);
        }

        controllers.put(oid, ctrl);

        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }
}

uint64 SimPlayerManager::recordConceptualMinerYield(const String& resourceName, int amount, uint64 sourceObjectID, bool logYield) {
    String resourceKey = resourceName;

    if (!enabled || resourceKey.isEmpty() || amount <= 0)
        return 0;

    uint64 total = 0;
    uint64 lockAttemptTime = 0;
    uint64 lockAcquiredTime = 0;

    if (logYield) {
        info("SimMiner yield: phase=enter resource=" + resourceKey +
             " amount=" + String::valueOf(amount) +
             " source=" + String::valueOf(sourceObjectID), true);
        lockAttemptTime = System::getMiliTime();
    }

    {
        Locker locker(&conceptualMinerTotalsMutex);

        if (logYield)
            lockAcquiredTime = System::getMiliTime();

        if (conceptualMinerTotals.contains(resourceKey))
            total = conceptualMinerTotals.get(resourceKey);

        total += amount;
        conceptualMinerTotals.put(resourceKey, total);
    }

    if (logYield) {
        uint64 completedTime = System::getMiliTime();
        info("SimMiner yield: phase=complete resource=" + resourceKey +
             " amount=" + String::valueOf(amount) +
             " source=" + String::valueOf(sourceObjectID) +
             " total=" + String::valueOf(total) +
             " lockWaitMs=" + String::valueOf(lockAcquiredTime - lockAttemptTime) +
             " totalMs=" + String::valueOf(completedTime - lockAttemptTime), true);
    }

    return total;
}

static String buildResourceAwareStockpileAggregationKey(
        const SimIntelligentYieldSnapshot& snapshot) {
    return snapshot.conceptualLabel + "|" +
        snapshot.sourceResourceName + "|" +
        snapshot.sourceResourceType + "|" +
        snapshot.sourceZone + "|" +
        snapshot.selectedDemandProfile + "|" +
        snapshot.identityConfidence + "|intelligent_miner";
}

void SimPlayerManager::recordResourceAwareConceptualStockpileYield(
        const SimIntelligentYieldSnapshot& snapshot) {
    if (snapshot.conceptualLabel.isEmpty() ||
            snapshot.sourceResourceName.isEmpty() ||
            snapshot.sourceResourceType.isEmpty() ||
            snapshot.sourceZone.isEmpty() ||
            snapshot.amount <= 0 ||
            snapshot.identityConfidence != "observed_resource_spawn")
        return;

    const int maxResourceAwareRows = 64;
    String key = buildResourceAwareStockpileAggregationKey(snapshot);

    Locker locker(&resourceAwareStockpileMutex);

    for (int i = 0; i < resourceAwareStockpileRows.size(); ++i) {
        SimResourceAwareStockpileRow row =
            resourceAwareStockpileRows.get(i);

        if (row.aggregationKey != key)
            continue;

        row.quantity += static_cast<uint64>(snapshot.amount);
        row.eventCount++;
        row.lastObservedMs = snapshot.recordedAtMs;
        row.sourceX = snapshot.sourceX;
        row.sourceY = snapshot.sourceY;
        row.sourceZ = snapshot.sourceZ;
        row.sourceDensity = snapshot.sourceDensity;
        row.demandState = snapshot.demandState;
        row.pressureScore = snapshot.pressureScore;
        resourceAwareStockpileRows.set(i, row);
        return;
    }

    if (resourceAwareStockpileRows.size() >= maxResourceAwareRows) {
        int oldestIndex = 0;
        uint64 oldestTime = resourceAwareStockpileRows.get(0).firstObservedMs;

        for (int i = 1; i < resourceAwareStockpileRows.size(); ++i) {
            uint64 firstObserved =
                resourceAwareStockpileRows.get(i).firstObservedMs;

            if (firstObserved < oldestTime) {
                oldestTime = firstObserved;
                oldestIndex = i;
            }
        }

        resourceAwareStockpileRows.remove(oldestIndex);
    }

    SimResourceAwareStockpileRow row;
    row.aggregationKey = key;
    row.quantity = static_cast<uint64>(snapshot.amount);
    row.eventCount = 1;
    row.firstObservedMs = snapshot.recordedAtMs;
    row.lastObservedMs = snapshot.recordedAtMs;
    row.conceptualLabel = snapshot.conceptualLabel;
    row.sourceResourceName = snapshot.sourceResourceName;
    row.sourceResourceType = snapshot.sourceResourceType;
    row.sourceZone = snapshot.sourceZone;
    row.sourceX = snapshot.sourceX;
    row.sourceY = snapshot.sourceY;
    row.sourceZ = snapshot.sourceZ;
    row.sourceDensity = snapshot.sourceDensity;
    row.selectedDemandProfile = snapshot.selectedDemandProfile;
    row.demandState = snapshot.demandState;
    row.pressureScore = snapshot.pressureScore;
    row.acquisitionSource = "intelligent_miner";
    row.resourceLifecycleState = "conceptual";
    row.identityConfidence = snapshot.identityConfidence;
    row.yieldMode = "conceptual";
    row.realResourceCreated = false;
    row.resourceContainerCreated = false;
    row.inventoryMutated = false;
    row.economyMutated = false;
    resourceAwareStockpileRows.add(row);
}

uint64 SimPlayerManager::recordIntelligentConceptualMinerYield(
        const String& conceptualLabel, int amount, uint64 minerID, bool logYield) {
    uint64 total = recordConceptualMinerYield(
        conceptualLabel, amount, minerID, logYield);

    if (!enabled || conceptualLabel.isEmpty() || amount <= 0 || minerID == 0)
        return total;

    uint64 nowMs = System::getMiliTime();
    MinerIntelligentTargetAssignment assignment;
    bool assignmentAvailable = false;

    {
        Locker locker(&minerIntelligentTargetingAssignmentMutex);

        if (minerIntelligentTargetAssignments.contains(minerID)) {
            assignment = minerIntelligentTargetAssignments.get(minerID);
            assignmentAvailable = assignment.isValid();
        }
    }

    SimIntelligentYieldSnapshot snapshot;
    snapshot.minerID = minerID;
    snapshot.recordedAtMs = nowMs;
    snapshot.amount = amount;
    snapshot.conceptualLabel = conceptualLabel;
    snapshot.yieldMode = "conceptual";
    snapshot.identityConfidence = assignmentAvailable ?
        String("observed_resource_spawn") : String("assignment_unavailable");
    snapshot.realResourceCreated = false;
    snapshot.resourceContainerCreated = false;
    snapshot.inventoryMutated = false;
    snapshot.economyMutated = false;

    if (assignmentAvailable) {
        snapshot.assignmentGenerationId = assignment.assignmentGenerationId;
        snapshot.targetHash = assignment.targetHash;
        snapshot.activationSnapshotId = assignment.activationSnapshotId;
        snapshot.activationPathValidationStatus =
            assignment.activationPathValidationStatus;
        snapshot.activationPathTrustStatus =
            assignment.activationPathTrustStatus;
        snapshot.assignmentCreatedAtMs = assignment.createdAtMs;
        snapshot.assignmentAgeSeconds =
            assignment.createdAtMs > 0 && nowMs > assignment.createdAtMs ?
            (nowMs - assignment.createdAtMs) / 1000 : 0;
        snapshot.sourceResourceName = assignment.targetResourceName;
        snapshot.sourceResourceType = assignment.targetResourceType;
        snapshot.sourceZone = assignment.targetZoneName;
        snapshot.sourceX = assignment.targetX;
        snapshot.sourceY = assignment.targetY;
        snapshot.sourceZ = assignment.targetZ;
        snapshot.sourceDensity = assignment.targetDensity;
        snapshot.selectedDemandProfile = assignment.selectedProfileKey;
        snapshot.demandState = assignment.demandState;
        snapshot.pressureScore = assignment.pressureScore;
    }

    {
        Locker locker(&recentIntelligentYieldsMutex);

        recentIntelligentYields.add(snapshot);

        while (recentIntelligentYields.size() > 24)
            recentIntelligentYields.remove(0);
    }

    recordResourceAwareConceptualStockpileYield(snapshot);

    if (logYield) {
        info(String("SimMiner intelligent conceptual yield provenance miner=") +
             String::valueOf(minerID) +
             " assignmentGenerationId=" +
                String::valueOf(snapshot.assignmentGenerationId) +
             " targetHash=" +
                (snapshot.targetHash.isEmpty() ?
                    String("none") : snapshot.targetHash) +
             " activationSnapshotId=" +
                String::valueOf(snapshot.activationSnapshotId) +
             " amount=" + String::valueOf(amount) +
             " conceptualLabel=" + conceptualLabel +
             " sourceResourceName=" +
                (snapshot.sourceResourceName.isEmpty() ?
                    String("none") : snapshot.sourceResourceName) +
             " sourceResourceType=" +
                (snapshot.sourceResourceType.isEmpty() ?
                    String("none") : snapshot.sourceResourceType) +
             " sourceZone=" +
                (snapshot.sourceZone.isEmpty() ?
                    String("unknown") : snapshot.sourceZone) +
             " selectedDemandProfile=" +
                (snapshot.selectedDemandProfile.isEmpty() ?
                    String("none") : snapshot.selectedDemandProfile) +
             " yieldMode=conceptual identityConfidence=" +
                snapshot.identityConfidence +
             " activationValidationStatus=" +
                (snapshot.activationPathValidationStatus.isEmpty() ?
                    String("none") : snapshot.activationPathValidationStatus) +
             " activationPathTrustStatus=" +
                (snapshot.activationPathTrustStatus.isEmpty() ?
                    String("none") : snapshot.activationPathTrustStatus) +
             " realResourceCreated=false resourceContainerCreated=false" +
             " inventoryMutated=false economyMutated=false", true);
    }

    return total;
}

void SimPlayerManager::clearMinerIntelligentTargetAssignmentFromController(
        uint64 minerID, const String& reason) {
    if (!enabled)
        return;

    clearMinerIntelligentTargetAssignment(minerID, reason, minerIntelligentTargetingMode);
}

void SimPlayerManager::clearMinerIntelligentTargetAssignmentOnSampleComplete(uint64 minerID) {
    if (!enabled || !minerIntelligentTargetingAssignmentClearOnSampleComplete)
        return;

    if (stationedMinerLifecycleEnabled)
        return;

    clearMinerIntelligentTargetAssignmentFromController(minerID, "sampleComplete");
}

bool SimPlayerManager::transitionMinerIntelligentAssignmentToStationed(
        uint64 minerID, int yieldAmount, bool& scheduleRepeatedSample,
        int& delayMs, String& reason) {
    scheduleRepeatedSample = false;
    delayMs = 0;
    reason = "sampleComplete";

    if (minerID == 0)
        return false;

    if (!enabled || !minerIntelligentTargetingAssignmentEnabled) {
        reason = "emergencyDisabled";
        return false;
    }

    if (!stationedMinerLifecycleEnabled) {
        reason = "sampleComplete";
        return false;
    }

    if (minerIntelligentTargetingLimitedEmergencyDisabled) {
        reason = "emergencyDisabled";
        return false;
    }

    uint64 now = System::getMiliTime();
    MinerIntelligentTargetAssignment assignment;

    {
        Locker locker(&minerIntelligentTargetingAssignmentMutex);

        if (!minerIntelligentTargetAssignments.contains(minerID)) {
            reason = "minerInvalid";
            return false;
        }

        assignment = minerIntelligentTargetAssignments.get(minerID);
    }

    if (!assignment.isValid()) {
        reason = "minerInvalid";
        return false;
    }

    if (stationedMinerRequireDemandStillValid &&
            assignment.selectedProfileKey.isEmpty()) {
        reason = "demandNoLongerValid";
        return false;
    }

    if (stationedMinerRequireResourceStillActive &&
            (assignment.targetResourceName.isEmpty() ||
             assignment.targetResourceType.isEmpty())) {
        reason = "resourceDespawned";
        return false;
    }

    if (stationedMinerRequireSamePlanet && assignment.targetZoneName.isEmpty()) {
        reason = "zoneMismatch";
        return false;
    }

    if (stationedMinerClearWhenReserveSatisfied &&
            !assignment.selectedProfileKey.isEmpty()) {
        uint64 desiredReserve =
            demandWeightedMinerPlanSimulationDesiredReserve.contains(
                assignment.selectedProfileKey) ?
            static_cast<uint64>(
                demandWeightedMinerPlanSimulationDesiredReserve.get(
                    assignment.selectedProfileKey)) : 0;

        if (desiredReserve > 0) {
            Vector<String> resourceNames;
            Vector<uint64> amounts;
            collectConceptualMinerTotals(resourceNames, amounts);

            for (int i = 0; i < resourceNames.size() && i < amounts.size(); ++i) {
                String label = resourceNames.get(i);

                if ((label == assignment.targetResourceType ||
                        label == assignment.targetResourceName) &&
                        amounts.get(i) >= desiredReserve) {
                    reason = "reserveSatisfied";
                    return false;
                }
            }
        }
    }

    uint64 durationSeconds = assignment.stationedAtMs > 0 &&
        now > assignment.stationedAtMs ?
        (now - assignment.stationedAtMs) / 1000 : 0;

    if (stationedMinerMaxDurationSeconds > 0 &&
            assignment.stationedAtMs > 0 &&
            durationSeconds >= static_cast<uint64>(stationedMinerMaxDurationSeconds)) {
        reason = "maxStationDurationReached";
        return false;
    }

    if (stationedMinerMaxSamplesPerAssignment > 0 &&
            assignment.stationSampleCount >= stationedMinerMaxSamplesPerAssignment) {
        reason = "maxStationSamplesReached";
        return false;
    }

    {
        Locker locker(&minerIntelligentTargetingAssignmentMutex);

        if (!minerIntelligentTargetAssignments.contains(minerID)) {
            reason = "minerInvalid";
            return false;
        }

        assignment = minerIntelligentTargetAssignments.get(minerID);
        assignment.status = "stationed";
        assignment.rebalanceReason = "coverageRetained";
        assignment.updatedAtMs = now;

        if (assignment.stationedAtMs == 0)
            assignment.stationedAtMs = now;

        assignment.lastStationSampleAtMs = now;
        assignment.sampleFinishedAtMs = now;
        assignment.stationSampleCount++;
        assignment.stationYieldQuantity +=
            static_cast<uint64>(yieldAmount > 0 ? yieldAmount : 0);
        assignment.stationDurationSeconds =
            now > assignment.stationedAtMs ?
            (now - assignment.stationedAtMs) / 1000 : 0;
        assignment.stationCoverageRetainedCount++;
        assignment.reachabilityStationedCoverageRecorded = true;

        minerIntelligentTargetAssignments.put(minerID, assignment);
    }

    recordReachabilityStationedCoverage(assignment);

    reason = "stationed";

    if (stationedMinerRepeatedSamplingEnabled &&
            assignment.stationSampleCount < stationedMinerMaxSamplesPerAssignment &&
            (stationedMinerMaxDurationSeconds <= 0 ||
                assignment.stationDurationSeconds <
                    static_cast<uint64>(stationedMinerMaxDurationSeconds))) {
        int jitter = stationedMinerSampleJitterSeconds > 0 ?
            System::random(stationedMinerSampleJitterSeconds) : 0;
        delayMs = (stationedMinerSampleIntervalSeconds + jitter) * 1000;
        scheduleRepeatedSample = delayMs > 0;
    }

    if (minerIntelligentTargetingAssignmentLogLifecycle) {
        info(String("MinerIntelligentTargetAssignment miner=") +
             String::valueOf(minerID) +
             " action=stationed" +
             " clearReason=none" +
             " rebalanceReason=" + assignment.rebalanceReason +
             " assignmentGenerationId=" +
                String::valueOf(assignment.assignmentGenerationId) +
             " targetHash=" +
                (assignment.targetHash.isEmpty() ?
                    String("none") : assignment.targetHash) +
             " stationSampleCount=" +
                String::valueOf(assignment.stationSampleCount) +
             " stationDurationSeconds=" +
                String::valueOf(assignment.stationDurationSeconds) +
             " repeatedSamplingScheduled=" +
                (scheduleRepeatedSample ? String("true") : String("false")) +
             " mode=" + minerIntelligentTargetingMode, true);
    }

    return true;
}

void SimPlayerManager::recordMinerIntelligentTargetAssignmentLifecycleFromController(
        uint64 minerID, const String& eventName, const String& detail) {
    if (!enabled)
        return;

    recordMinerIntelligentTargetAssignmentLifecycle(minerID, eventName, detail);
}

void SimPlayerManager::scheduleMinerSummaryTask() {
    if (!enabled || !minerSummaryLoggingEnabled || minerSummaryTaskScheduled)
        return;

    minerSummaryTaskScheduled = true;

    Reference<SimMinerSummaryTask*> task = new SimMinerSummaryTask();
    task->schedule(minerSummaryIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerSummaryTask() {
    minerSummaryTaskScheduled = false;

    if (!enabled || !minerSummaryLoggingEnabled)
        return;

    logConceptualMinerSummary();
    scheduleMinerSummaryTask();
}

int SimPlayerManager::countActiveMiners() {
    int activeMiners = 0;
    int controllerCount = controllers.size();

    for (int i = 0; i < controllerCount; ++i) {
        uint64 key = controllers.getKey(i);
        Reference<SimPlayerController*> ctrl = controllers.get(key);

        if (ctrl != nullptr && dynamic_cast<SimMinerController*>(ctrl.get()) != nullptr)
            ++activeMiners;
    }

    return activeMiners;
}

void SimPlayerManager::collectConceptualMinerTotals(Vector<String>& resourceNames, Vector<uint64>& amounts) {
    Locker locker(&conceptualMinerTotalsMutex);

    for (int i = 0; i < conceptualMinerTotals.size(); ++i) {
        resourceNames.add(conceptualMinerTotals.elementAt(i).getKey());
        amounts.add(conceptualMinerTotals.get(i));
    }
}

JSONSerializationType SimPlayerManager::getAiEconomyDashboardSnapshot() {
    uint64 nowMs = System::getMiliTime();
    Time now;

    JSONSerializationType result = JSONSerializationType::object();
    JSONSerializationType metadata = JSONSerializationType::object();
    metadata["asOfTime"] = now.getFormattedTimeFull();
    metadata["epochMs"] = nowMs;
    metadata["enabled"] = enabled;
    result["metadata"] = metadata;

    int activeMiners = 0;
    int activePvpBots = 0;
    int controllerCount = controllers.size();
    Vector<String> activeMinerZones;
    Vector<String> configuredMinerSpawnZones;
    Vector<uint64> activeMinerIds;
    VectorMap<uint64, String> activeMinerZoneById;
    VectorMap<uint64, float> activeMinerXById;
    VectorMap<uint64, float> activeMinerYById;
    VectorMap<uint64, float> activeMinerZById;
    VectorMap<uint64, int> activeMinerNavmeshById;
    VectorMap<String, int> populationTotalByZone;
    VectorMap<String, int> populationMinersByZone;
    VectorMap<String, int> populationPvpByZone;
    VectorMap<String, int> populationAssignedMinersByZone;
    VectorMap<String, int> populationCandidateByZone;
    VectorMap<String, int> populationValidatedByZone;
    VectorMap<String, int> populationSamplingByZone;
    VectorMap<String, int> populationStationedByZone;
    VectorMap<String, int> populationMovingByZone;
    VectorMap<String, int> populationBlockedByZone;
    VectorMap<String, int> populationRemotePlansFromZone;
    VectorMap<String, int> populationHubPlansToZone;
    JSONSerializationType controllerRows = JSONSerializationType::array();

    bool minerSpawnsConfigured = false;

    for (int i = 0; i < spawnGroups.size(); ++i) {
        const SpawnGroup& group = spawnGroups.get(i);

        if (!group.type.beginsWith("pvp") && group.totalCount > 0) {
            minerSpawnsConfigured = true;
            break;
        }
    }

    if (minerSpawnsConfigured) {
        for (int i = 0; i < allShuttleports.size(); ++i) {
            ShuttleportLocation location = allShuttleports.get(i);
            addUniqueLabel(configuredMinerSpawnZones, location.planet);
        }
    }

    for (int i = 0; i < controllerCount; ++i) {
        uint64 controllerKey = controllers.getKey(i);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr)
            continue;

        String role = "unknown";

        if (dynamic_cast<SimMinerController*>(ctrl.get()) != nullptr) {
            role = "miner";
            activeMiners++;
        } else if (dynamic_cast<SimPvPController*>(ctrl.get()) != nullptr) {
            role = "pvp_bot";
            activePvpBots++;
        }

        ManagedReference<AiAgent*> agent = ctrl->getAgent();
        String zoneName = "unknown";
        uint64 objectID = controllerKey;
        Vector3 objectPosition;
        bool objectInNavmesh = false;
        bool objectPositionKnown = false;

        if (agent != nullptr) {
            Locker agentLocker(agent);
            objectID = agent->getObjectID();
            objectPosition = agent->getWorldPosition();
            objectInNavmesh = agent->isInNavMesh();
            objectPositionKnown = true;

            Zone* zone = agent->getZone();
            if (zone != nullptr)
                zoneName = zone->getZoneName();
        }

        addIntCounter(populationTotalByZone, zoneName);

        if (role == "miner" && zoneName != "unknown") {
            addUniqueLabel(activeMinerZones, zoneName);
            activeMinerIds.add(objectID);
            activeMinerZoneById.put(objectID, zoneName);
            if (objectPositionKnown) {
                activeMinerXById.put(objectID, objectPosition.getX());
                activeMinerYById.put(objectID, objectPosition.getY());
                activeMinerZById.put(objectID, objectPosition.getZ());
                activeMinerNavmeshById.put(
                    objectID, objectInNavmesh ? 1 : 0);
            }
            addIntCounter(populationMinersByZone, zoneName);
        } else if (role == "pvp_bot") {
            addIntCounter(populationPvpByZone, zoneName);
        }

        JSONSerializationType row = JSONSerializationType::object();
        row["objectId"] = objectID;
        row["role"] = role;
        row["zone"] = zoneName;
        controllerRows.push_back(row);
    }

    JSONSerializationType population = JSONSerializationType::object();
    population["totalControllers"] = controllerCount;
    population["activeMiners"] = activeMiners;
    population["activePvpBots"] = activePvpBots;
    population["pvpStatus"] = "experimental";
    population["controllers"] = controllerRows;
    population["activeMinerZones"] = joinCoverageZones(activeMinerZones);
    population["configuredMinerSpawnZones"] =
        joinCoverageZones(configuredMinerSpawnZones);
    population["travelImplemented"] = false;

    JSONSerializationType futureRoles = JSONSerializationType::array();
    JSONSerializationType crafterRole = JSONSerializationType::object();
    crafterRole["role"] = "crafters";
    crafterRole["status"] = "not_implemented";
    crafterRole["active"] = 0;
    futureRoles.push_back(crafterRole);

    JSONSerializationType pveRole = JSONSerializationType::object();
    pveRole["role"] = "pve_roles";
    pveRole["status"] = "not_implemented";
    pveRole["active"] = 0;
    futureRoles.push_back(pveRole);
    population["futureRoles"] = futureRoles;
    result["population"] = population;

    int assignmentActive = 0;
    int assignmentQueued = 0;
    int assignmentMoving = 0;
    int assignmentSampling = 0;
    int assignmentStationed = 0;
	int assignmentCandidate = 0;
	int assignmentValidated = 0;
	int assignmentFailed = 0;
	int assignmentExpired = 0;
	int candidateExpiredCount = 0;
	int validatedExpiredCount = 0;
	int queuedActivationTimeoutCount = 0;
	int movementArrivalTimeoutCount = 0;
	int sampleTimeoutCount = 0;
	int normalTtlSkippedForActiveMovementCount = 0;
	int expiredWhileActivePreventedCount = 0;
	int forceMovementReadinessPassedCount = 0;
    int forceMovementBlockedCount = 0;
    String movementReadinessStatus = "no_data";
    String movementReadinessReason = "no_live_assignments";
    VectorMap<uint64, int> activeMinerAssigned;
    Vector<MinerIntelligentTargetAssignment> dashboardAssignmentSnapshots;
    JSONSerializationType assignments = JSONSerializationType::array();

    {
        Locker assignmentLocker(&minerIntelligentTargetingAssignmentMutex);

        for (int i = 0; i < minerIntelligentTargetAssignments.size(); ++i) {
	            MinerIntelligentTargetAssignment assignment =
	                minerIntelligentTargetAssignments.elementAt(i).getValue();
	            dashboardAssignmentSnapshots.add(assignment);
	            uint64 timeoutAgeSeconds = 0;
	            uint64 timeoutSeconds = 0;
	            String timeoutReason =
	                getMinerIntelligentAssignmentTimeoutReason(
	                    assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, false);
	            bool expired = !timeoutReason.isEmpty();
	            String status = expired ? timeoutReason : assignment.status;
	            bool activeLifecycle = isMinerIntelligentAssignmentActive(assignment);
	            bool normalTtlSkipped = assignment.normalTtlSkippedForActiveMovement ||
	                (activeLifecycle &&
	                    isMinerIntelligentAssignmentNormalTtlElapsed(
	                        assignment, nowMs) &&
	                    minerIntelligentTargetingPreventNormalTtlForActiveMovement);

	            if (expired) {
	                assignmentExpired++;
	                if (timeoutReason == "expired" &&
	                        assignment.status == "validated")
	                    validatedExpiredCount++;
	                else if (timeoutReason == "expired")
	                    candidateExpiredCount++;
	                else if (timeoutReason == "queuedActivationTimeout")
	                    queuedActivationTimeoutCount++;
	                else if (timeoutReason == "movementArrivalTimeout")
	                    movementArrivalTimeoutCount++;
	                else if (timeoutReason == "sampleTimeout")
	                    sampleTimeoutCount++;
	            } else if (status == "queued") {
	                assignmentQueued++;
            } else if (status == "activation_started") {
                assignmentMoving++;
            } else if (status == "sample_started") {
                assignmentSampling++;
            } else if (status == "stationed") {
                assignmentStationed++;
            } else if (status == "candidate") {
                assignmentCandidate++;
            } else if (status == "validated") {
                assignmentValidated++;
	            } else if (status == "failed") {
	                assignmentFailed++;
	            }

	            if (normalTtlSkipped) {
	                normalTtlSkippedForActiveMovementCount++;
	                expiredWhileActivePreventedCount++;
	            }

	            if (!expired && activeLifecycle)
	                assignmentActive++;

            if (!expired && activeMinerZoneById.contains(assignment.minerID)) {
                String assignmentZone = activeMinerZoneById.get(assignment.minerID);
                activeMinerAssigned.put(assignment.minerID, 1);
                addIntCounter(populationAssignedMinersByZone, assignmentZone);

                if (status == "candidate")
                    addIntCounter(populationCandidateByZone, assignmentZone);
                else if (status == "validated")
                    addIntCounter(populationValidatedByZone, assignmentZone);
                else if (status == "sample_started")
                    addIntCounter(populationSamplingByZone, assignmentZone);
                else if (status == "stationed")
                    addIntCounter(populationStationedByZone, assignmentZone);
                else if (status == "activation_started" || status == "queued")
                    addIntCounter(populationMovingByZone, assignmentZone);

                String assignmentBlocker = classifyResourceCoverageBlocker(assignment);
                if (status == "failed" || assignmentBlocker == "blocked_by_path" ||
                        assignmentBlocker == "blocked_by_density" ||
                        assignmentBlocker == "wrong_planet")
                    addIntCounter(populationBlockedByZone, assignmentZone);
            }

            JSONSerializationType assignmentJSON = JSONSerializationType::object();
            assignmentJSON["minerId"] = assignment.minerID;
            assignmentJSON["status"] = status;
            assignmentJSON["lifecycleStatus"] = status;
            assignmentJSON["assignmentGenerationId"] =
                assignment.assignmentGenerationId;
            assignmentJSON["targetHash"] = assignment.targetHash;
            assignmentJSON["profile"] = assignment.selectedProfileKey;
            assignmentJSON["demandState"] = assignment.demandState;
            assignmentJSON["targetResource"] = assignment.targetResourceName;
            assignmentJSON["targetType"] = assignment.targetResourceType;
            assignmentJSON["targetZone"] = assignment.targetZoneName;
            assignmentJSON["density"] = Math::getPrecision(assignment.targetDensity, 3);
            assignmentJSON["pathValidationStatus"] = assignment.pathValidationStatus;
            assignmentJSON["pathTrustStatus"] = assignment.pathValidationTrustStatus;
            assignmentJSON["currentPathValidationStatus"] =
                assignment.currentPathValidationStatus.isEmpty() ?
                assignment.pathValidationStatus :
                assignment.currentPathValidationStatus;
            assignmentJSON["currentPathTrustStatus"] =
                assignment.currentPathTrustStatus.isEmpty() ?
                assignment.pathValidationTrustStatus :
                assignment.currentPathTrustStatus;
            assignmentJSON["latestValidationStatus"] =
                assignment.pathValidationStatus;
            assignmentJSON["latestPathTrustStatus"] =
                assignment.pathValidationTrustStatus;
            assignmentJSON["latestValidationSnapshotId"] =
                assignment.latestValidationSnapshotId;
            assignmentJSON["latestValidationTargetHash"] =
                assignment.latestValidationTargetHash;
            assignmentJSON["validatedSnapshotId"] =
                assignment.validatedSnapshotId;
            assignmentJSON["validatedTargetHash"] =
                assignment.validatedTargetHash;
            assignmentJSON["activationSnapshotId"] =
                assignment.activationSnapshotId;
            assignmentJSON["activationTargetHash"] =
                assignment.activationTargetHash;
            assignmentJSON["activationValidationStatus"] =
                assignment.activationPathValidationStatus;
            assignmentJSON["activationPathTrustStatus"] =
                assignment.activationPathTrustStatus;
            assignmentJSON["validationMismatchReason"] =
                assignment.latestValidationMismatchReason;
            assignmentJSON["lifecycleDowngradePrevented"] =
                assignment.lifecycleDowngradePrevented;
            assignmentJSON["lastActivationResult"] = assignment.lastActivationResult;
            assignmentJSON["lastFailureReason"] = assignment.lastFailureReason;
            assignmentJSON["rebalanceReason"] = assignment.rebalanceReason;
            assignmentJSON["stationedAtMs"] = assignment.stationedAtMs;
            assignmentJSON["lastSampleAtMs"] = assignment.lastStationSampleAtMs;
            assignmentJSON["lastStationSampleAtMs"] =
                assignment.lastStationSampleAtMs;
            assignmentJSON["stationSampleCount"] = assignment.stationSampleCount;
            assignmentJSON["stationYieldQuantity"] =
                assignment.stationYieldQuantity;
            assignmentJSON["stationDurationSeconds"] =
                assignment.status == "stationed" &&
                    assignment.stationedAtMs > 0 && nowMs > assignment.stationedAtMs ?
                (nowMs - assignment.stationedAtMs) / 1000 :
                assignment.stationDurationSeconds;
            assignmentJSON["timeoutReason"] =
                timeoutReason.isEmpty() ? String("none") : timeoutReason;
	            assignmentJSON["lifecycleTimeoutAgeSeconds"] = timeoutAgeSeconds;
	            assignmentJSON["lifecycleTimeoutSeconds"] = timeoutSeconds;
	            assignmentJSON["normalTtlSkippedForActiveMovement"] =
	                normalTtlSkipped;
	            assignmentJSON["activeMovementAgeSeconds"] =
	                assignment.status == "activation_started" ? timeoutAgeSeconds : 0;
	            assignmentJSON["activeMovementTimeoutSeconds"] =
	                assignment.status == "activation_started" ? timeoutSeconds : 0;
	            assignmentJSON["movementTimeoutRemainingSeconds"] =
	                assignment.status == "activation_started" &&
	                    timeoutSeconds > timeoutAgeSeconds ?
	                    timeoutSeconds - timeoutAgeSeconds : 0;
	            assignmentJSON["sampleTimeoutRemainingSeconds"] =
	                assignment.status == "sample_started" &&
	                    timeoutSeconds > timeoutAgeSeconds ?
	                    timeoutSeconds - timeoutAgeSeconds : 0;
	            assignmentJSON["activationPathDistance"] =
	                Math::getPrecision(assignment.activationPathDistance, 1);
	            assignmentJSON["latestPathDistance"] =
	                Math::getPrecision(assignment.latestPathDistance, 1);
	            assignmentJSON["ageSeconds"] =
                assignment.createdAtMs > 0 && nowMs > assignment.createdAtMs ?
                (nowMs - assignment.createdAtMs) / 1000 : 0;
	            assignmentJSON["remainingSeconds"] =
	                timeoutSeconds > timeoutAgeSeconds ?
	                timeoutSeconds - timeoutAgeSeconds : 0;
            assignments.push_back(assignmentJSON);
        }
    }

	    for (int i = 0; i < dashboardAssignmentSnapshots.size(); ++i) {
	        MinerIntelligentTargetAssignment assignment =
	            dashboardAssignmentSnapshots.get(i);
	        uint64 timeoutAgeSeconds = 0;
	        uint64 timeoutSeconds = 0;
	        String timeoutReason =
	            getMinerIntelligentAssignmentTimeoutReason(
	                assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, false);
	        bool expired = !timeoutReason.isEmpty();
	        String status = expired ? timeoutReason : assignment.status;
	        bool alreadyActive = !expired && isMinerIntelligentAssignmentActive(assignment);
        bool activeCapPermits = alreadyActive ||
            assignmentActive < minerIntelligentTargetingLimitedMaxActiveIntelligentMiners;
        bool validationStatusReady =
            (assignment.activationSnapshotId > 0 &&
                assignment.activationPathValidationStatus == "valid" &&
                assignment.activationPathTrustStatus == "verifiedPath" &&
                assignment.activationTargetHash == assignment.targetHash) ||
            (assignment.activationSnapshotId == 0 &&
                assignment.validatedSnapshotId > 0 &&
                assignment.validatedPathValidationStatus == "valid" &&
                assignment.validatedPathTrustStatus == "verifiedPath" &&
                assignment.validatedTargetHash == assignment.targetHash);
        bool lifecycleReady = !expired &&
            (status == "candidate" || status == "validated" ||
             status == "queued" || status == "activation_started" ||
             status == "sample_started" || status == "sample_complete");
        bool mismatchFree =
            assignment.latestValidationMismatchReason.isEmpty() ||
            assignment.latestValidationMismatchReason == "none";
        bool ready = lifecycleReady &&
            validationStatusReady &&
            !assignment.lifecycleDowngradePrevented &&
            mismatchFree &&
            activeCapPermits;

	        if (ready) {
	            forceMovementReadinessPassedCount++;
	        } else {
	            forceMovementBlockedCount++;

	            if (movementReadinessReason == "no_live_assignments") {
	                if (timeoutReason == "queuedActivationTimeout")
	                    movementReadinessReason = "queuedActivationTimeout";
	                else if (timeoutReason == "movementArrivalTimeout")
	                    movementReadinessReason = "movementTimedOut";
	                else if (timeoutReason == "sampleTimeout")
	                    movementReadinessReason = "sampleTimedOut";
	                else if (status == "candidate")
	                    movementReadinessReason = "candidateAwaitingValidation";
	                else if (status == "validated")
	                    movementReadinessReason = "validatedAwaitingActivation";
	                else if (status == "queued")
	                    movementReadinessReason = "queuedAwaitingStart";
	                else if (status == "activation_started")
	                    movementReadinessReason = "movingAwaitingArrival";
	                else if (status == "sample_started")
	                    movementReadinessReason = "samplingInProgress";
	                else if (status == "sample_complete")
	                    movementReadinessReason = "completed";
	                else if (!lifecycleReady)
	                    movementReadinessReason = "lifecycle_not_ready";
	                else if (!validationStatusReady)
	                    movementReadinessReason =
	                        assignment.pathValidationStatus == "failed" ?
	                        String("blockedPathValidation") :
	                        String("activation_validation_not_verified");
                else if (assignment.lifecycleDowngradePrevented)
                    movementReadinessReason = "lifecycle_downgrade_prevented";
                else if (!mismatchFree)
                    movementReadinessReason = assignment.latestValidationMismatchReason;
                else if (!activeCapPermits)
                    movementReadinessReason = "active_cap_full";
                else
                    movementReadinessReason = "readiness_blocked";
            }
        }
    }

    if (dashboardAssignmentSnapshots.size() > 0)
        movementReadinessStatus =
            forceMovementReadinessPassedCount > 0 ? String("ready") : String("blocked");
    if (forceMovementReadinessPassedCount > 0)
        movementReadinessReason = "identity_matched_verified_validation";
    if (!minerMovementReadinessDiagnosticsEnabled) {
        movementReadinessStatus = "disabled";
        movementReadinessReason = "diagnostic_disabled";
        forceMovementReadinessPassedCount = 0;
        forceMovementBlockedCount = 0;
    }

    int healthAttempts = 0;
    int healthStarted = 0;
    int healthArrivals = 0;
	int healthSamplesCompleted = 0;
	int healthPathFailures = 0;
	int healthExpired = 0;
	int healthCandidateExpired = 0;
	int healthValidatedExpired = 0;
	int healthQueuedActivationTimeout = 0;
	int healthMovementArrivalTimeout = 0;
	int healthSampleTimeout = 0;
	int healthExpiredWhileActivePrevented = 0;
	int healthNormalTtlSkippedForActiveMovement = 0;
	int healthCooldownSkips = 0;
    int healthActiveCapSkips = 0;
    int healthZoneSkips = 0;

    {
        Locker healthLocker(&minerIntelligentTargetingHealthMutex);
        healthAttempts = minerIntelligentActivationHealthAttempts;
        healthStarted = minerIntelligentActivationHealthStarted;
        healthArrivals = minerIntelligentActivationHealthArrivals;
	        healthSamplesCompleted = minerIntelligentActivationHealthSamplesCompleted;
	        healthPathFailures = minerIntelligentActivationHealthPathFailures;
	        healthExpired = minerIntelligentActivationHealthExpired;
	        healthCandidateExpired =
	            minerIntelligentActivationHealthCandidateExpired;
	        healthValidatedExpired =
	            minerIntelligentActivationHealthValidatedExpired;
	        healthQueuedActivationTimeout =
	            minerIntelligentActivationHealthQueuedActivationTimeout;
	        healthMovementArrivalTimeout =
	            minerIntelligentActivationHealthMovementArrivalTimeout;
	        healthSampleTimeout =
	            minerIntelligentActivationHealthSampleTimeout;
	        healthExpiredWhileActivePrevented =
	            minerIntelligentActivationHealthExpiredWhileActivePrevented;
	        healthNormalTtlSkippedForActiveMovement =
	            minerIntelligentActivationHealthNormalTtlSkippedForActiveMovement;
	        healthCooldownSkips = minerIntelligentActivationHealthCooldownSkips;
        healthActiveCapSkips = minerIntelligentActivationHealthActiveCapSkips;
        healthZoneSkips = minerIntelligentActivationHealthZoneSkips;
    }

    int activationFailures = healthAttempts > healthStarted ?
        healthAttempts - healthStarted : 0;

    JSONSerializationType minerActivity = JSONSerializationType::object();
    minerActivity["intelligentTargetingEnabled"] = minerIntelligentTargetingEnabled;
    minerActivity["mode"] = minerIntelligentTargetingMode;
    minerActivity["currentIntelligentActiveCount"] = assignmentActive;
    minerActivity["coverageActiveCount"] = assignmentActive;
    minerActivity["queued"] = assignmentQueued;
    minerActivity["moving"] = assignmentMoving;
    minerActivity["sampling"] = assignmentSampling;
    minerActivity["stationed"] = assignmentStationed;
    minerActivity["candidate"] = assignmentCandidate;
    minerActivity["validated"] = assignmentValidated;
    minerActivity["failed"] = assignmentFailed;
	    minerActivity["expired"] = assignmentExpired;
	    minerActivity["candidateExpiredCount"] = candidateExpiredCount;
	    minerActivity["validatedExpiredCount"] = validatedExpiredCount;
	    minerActivity["queuedActivationTimeoutCount"] =
	        queuedActivationTimeoutCount;
	    minerActivity["movementArrivalTimeoutCount"] =
	        movementArrivalTimeoutCount;
	    minerActivity["sampleTimeoutCount"] = sampleTimeoutCount;
	    minerActivity["expiredWhileActivePreventedCount"] =
	        expiredWhileActivePreventedCount;
	    minerActivity["normalTtlSkippedForActiveMovementCount"] =
	        normalTtlSkippedForActiveMovementCount;
    minerActivity["activationFailures"] = activationFailures;
    minerActivity["pathFailures"] = healthPathFailures;
    minerActivity["emergencyDisabled"] =
        minerIntelligentTargetingLimitedEmergencyDisabled;
    minerActivity["maxActiveIntelligentMiners"] =
        minerIntelligentTargetingLimitedMaxActiveIntelligentMiners;
    minerActivity["maxActivationsPerInterval"] =
        minerIntelligentTargetingLimitedMaxActivationsPerInterval;
    minerActivity["cooldownSeconds"] =
        minerIntelligentTargetingLimitedCooldownSecondsPerMiner;
    minerActivity["movementReadinessStatus"] = movementReadinessStatus;
    minerActivity["movementReadinessReason"] = movementReadinessReason;
    minerActivity["forceMovementReadinessPassedCount"] =
        forceMovementReadinessPassedCount;
    minerActivity["forceMovementBlockedCount"] = forceMovementBlockedCount;
    minerActivity["assignments"] = assignments;

    JSONSerializationType health = JSONSerializationType::object();
    health["attempts"] = healthAttempts;
    health["started"] = healthStarted;
    health["arrivals"] = healthArrivals;
	health["samplesCompleted"] = healthSamplesCompleted;
	health["expired"] = healthExpired;
	health["candidateExpiredCount"] = healthCandidateExpired;
	health["validatedExpiredCount"] = healthValidatedExpired;
	health["queuedActivationTimeoutCount"] =
	    healthQueuedActivationTimeout;
	health["movementArrivalTimeoutCount"] =
	    healthMovementArrivalTimeout;
	health["sampleTimeoutCount"] = healthSampleTimeout;
	health["expiredWhileActivePreventedCount"] =
	    healthExpiredWhileActivePrevented;
	health["normalTtlSkippedForActiveMovementCount"] =
	    healthNormalTtlSkippedForActiveMovement;
	health["cooldownSkips"] = healthCooldownSkips;
    health["activeCapSkips"] = healthActiveCapSkips;
    health["zoneSkips"] = healthZoneSkips;
    minerActivity["healthWindow"] = health;
    result["minerActivity"] = minerActivity;

    JSONSerializationType movementReadiness = JSONSerializationType::object();
    movementReadiness["enabled"] = minerMovementReadinessDiagnosticsEnabled;
    movementReadiness["mode"] = "read-only";
    movementReadiness["forceMovementEnabled"] = false;
    movementReadiness["movementReadinessStatus"] = movementReadinessStatus;
    movementReadiness["movementReadinessReason"] = movementReadinessReason;
	movementReadiness["forceMovementReadinessPassedCount"] =
	    forceMovementReadinessPassedCount;
	movementReadiness["forceMovementBlockedCount"] = forceMovementBlockedCount;
	movementReadiness["queuedActivationTimeoutCount"] =
	    queuedActivationTimeoutCount;
	movementReadiness["movementArrivalTimeoutCount"] =
	    movementArrivalTimeoutCount;
	movementReadiness["sampleTimeoutCount"] = sampleTimeoutCount;
	movementReadiness["expiredWhileActivePreventedCount"] =
	    expiredWhileActivePreventedCount;
	movementReadiness["normalTtlSkippedForActiveMovementCount"] =
	    normalTtlSkippedForActiveMovementCount;
    movementReadiness["requiresLifecycleStable"] = true;
    movementReadiness["requiresActivationValidationVerifiedPath"] = true;
    movementReadiness["requiresGenerationAndTargetHashMatch"] = true;
    movementReadiness["requiresActiveCapPermit"] = true;
    movementReadiness["behaviorChanged"] = false;
    movementReadiness["realResourceCreated"] = false;
    movementReadiness["resourceContainerCreated"] = false;
    movementReadiness["inventoryMutated"] = false;
    movementReadiness["economyMutated"] = false;
    result["movementReadiness"] = movementReadiness;

    JSONSerializationType assignmentHistoryRows = JSONSerializationType::array();

    {
        Locker historyLocker(&recentMinerAssignmentHistoryMutex);

        for (int i = recentMinerAssignmentHistory.size(); i > 0; --i) {
            MinerAssignmentHistorySnapshot history =
                recentMinerAssignmentHistory.get(i - 1);

            JSONSerializationType row = JSONSerializationType::object();
            row["minerId"] = history.minerID;
            row["assignmentGenerationId"] = history.assignmentGenerationId;
            row["recordedAtMs"] = history.recordedAtMs;
            row["ageSeconds"] =
                history.recordedAtMs > 0 && nowMs > history.recordedAtMs ?
                (nowMs - history.recordedAtMs) / 1000 : 0;
            row["targetHash"] = history.targetHash;
            row["latestValidationTargetHash"] =
                history.latestValidationTargetHash;
            row["validatedTargetHash"] = history.validatedTargetHash;
            row["activationTargetHash"] = history.activationTargetHash;
            row["latestValidationSnapshotId"] =
                history.latestValidationSnapshotId;
            row["validatedSnapshotId"] = history.validatedSnapshotId;
            row["activationSnapshotId"] = history.activationSnapshotId;
            row["selectedProfile"] = history.selectedProfileKey;
            row["targetResource"] = history.targetResourceName;
            row["targetResourceType"] = history.targetResourceType;
            row["targetZone"] = history.targetZoneName;
	        row["lifecycleStatus"] = history.status;
	        row["clearReason"] = history.clearReason;
            row["movementAgeSeconds"] = history.movementAgeSeconds;
            row["movementTimeoutSeconds"] = history.movementTimeoutSeconds;
            row["sampleAgeSeconds"] = history.sampleAgeSeconds;
            row["sampleTimeoutSeconds"] = history.sampleTimeoutSeconds;
            row["stationedAtMs"] = history.stationedAtMs;
            row["lastSampleAtMs"] = history.lastStationSampleAtMs;
            row["lastStationSampleAtMs"] = history.lastStationSampleAtMs;
            row["rebalanceReason"] = history.rebalanceReason;
            row["stationSampleCount"] = history.stationSampleCount;
            row["stationYieldQuantity"] = history.stationYieldQuantity;
            row["stationDurationSeconds"] = history.stationDurationSeconds;
            row["normalTtlSkippedForActiveMovement"] =
                history.normalTtlSkippedForActiveMovement;
	        row["latestValidationStatus"] = history.latestValidationStatus;
            row["latestPathTrustStatus"] = history.latestPathTrustStatus;
            row["activationValidationStatus"] =
                history.activationValidationStatus;
	        row["activationPathTrustStatus"] =
	            history.activationPathTrustStatus;
	        row["latestPathDistance"] =
	            Math::getPrecision(history.latestPathDistance, 1);
	        row["activationPathDistance"] =
	            Math::getPrecision(history.activationPathDistance, 1);
            row["validationMismatchReason"] =
                history.validationMismatchReason;
            row["lifecycleDowngradePrevented"] =
                history.lifecycleDowngradePrevented;
            row["yielded"] = history.yielded;
            row["mode"] = "memory-only";
            assignmentHistoryRows.push_back(row);
        }
    }

    JSONSerializationType assignmentHistory = JSONSerializationType::object();
    assignmentHistory["enabled"] = true;
    assignmentHistory["readOnly"] = true;
    assignmentHistory["runtimeOnly"] = true;
    assignmentHistory["persisted"] = false;
    assignmentHistory["mode"] = "memory-only";
    assignmentHistory["rowCount"] = assignmentHistoryRows.size();
    assignmentHistory["maxRows"] = 32;
    assignmentHistory["rows"] = assignmentHistoryRows;
    result["recentAssignmentHistory"] = assignmentHistory;

    JSONSerializationType recentYieldRows = JSONSerializationType::array();
    VectorMap<String, uint64> recentYieldQuantityByProfile;
    VectorMap<String, int> recentYieldCountByProfile;

    {
        Locker yieldLocker(&recentIntelligentYieldsMutex);

        for (int i = recentIntelligentYields.size(); i > 0; --i) {
            SimIntelligentYieldSnapshot snapshot = recentIntelligentYields.get(i - 1);

            JSONSerializationType row = JSONSerializationType::object();
            row["minerId"] = snapshot.minerID;
            row["recordedAtMs"] = snapshot.recordedAtMs;
            row["assignmentGenerationId"] = snapshot.assignmentGenerationId;
            row["targetHash"] = snapshot.targetHash;
            row["activationSnapshotId"] = snapshot.activationSnapshotId;
            row["activationValidationStatus"] =
                snapshot.activationPathValidationStatus;
            row["activationPathTrustStatus"] =
                snapshot.activationPathTrustStatus;
            row["ageSeconds"] =
                snapshot.recordedAtMs > 0 && nowMs > snapshot.recordedAtMs ?
                (nowMs - snapshot.recordedAtMs) / 1000 : 0;
            row["assignmentCreatedAtMs"] = snapshot.assignmentCreatedAtMs;
            row["assignmentAgeSeconds"] = snapshot.assignmentAgeSeconds;
            row["amount"] = snapshot.amount;
            row["conceptualLabel"] = snapshot.conceptualLabel;
            row["sourceResourceName"] = snapshot.sourceResourceName;
            row["sourceResourceType"] = snapshot.sourceResourceType;
            row["sourceZone"] = snapshot.sourceZone;
            row["sourceX"] = Math::getPrecision(snapshot.sourceX, 1);
            row["sourceY"] = Math::getPrecision(snapshot.sourceY, 1);
            row["sourceZ"] = Math::getPrecision(snapshot.sourceZ, 1);
            row["sourceDensity"] = Math::getPrecision(snapshot.sourceDensity, 3);
            row["selectedDemandProfile"] = snapshot.selectedDemandProfile;
            row["demandState"] = snapshot.demandState;
            row["pressureScore"] = Math::getPrecision(snapshot.pressureScore, 1);
            row["yieldMode"] = snapshot.yieldMode;
            row["identityConfidence"] = snapshot.identityConfidence;
            row["realResourceCreated"] = snapshot.realResourceCreated;
            row["resourceContainerCreated"] = snapshot.resourceContainerCreated;
            row["inventoryMutated"] = snapshot.inventoryMutated;
            row["economyMutated"] = snapshot.economyMutated;
            recentYieldRows.push_back(row);

            addUint64Counter(
                recentYieldQuantityByProfile,
                snapshot.selectedDemandProfile,
                static_cast<uint64>(snapshot.amount));
            addIntCounter(
                recentYieldCountByProfile,
                snapshot.selectedDemandProfile);
        }
    }

    result["recentIntelligentYields"] = recentYieldRows;
    result["recentIntelligentYieldCount"] = recentYieldRows.size();

    JSONSerializationType resourceAwareRows = JSONSerializationType::array();
    uint64 resourceAwareTotalQuantity = 0;
    int resourceAwareEventCount = 0;
    int resourceAwareRowCount = 0;
    VectorMap<String, uint64> resourceAwareQuantityByProfile;

    {
        Locker stockpileLocker(&resourceAwareStockpileMutex);
        resourceAwareRowCount = resourceAwareStockpileRows.size();

        for (int i = 0; i < resourceAwareStockpileRows.size(); ++i) {
            SimResourceAwareStockpileRow snapshot =
                resourceAwareStockpileRows.get(i);
            resourceAwareTotalQuantity += snapshot.quantity;
            resourceAwareEventCount += snapshot.eventCount;

            JSONSerializationType row = JSONSerializationType::object();
            row["conceptualLabel"] = snapshot.conceptualLabel;
            row["quantity"] = snapshot.quantity;
            row["eventCount"] = snapshot.eventCount;
            row["firstObservedMs"] = snapshot.firstObservedMs;
            row["lastObservedMs"] = snapshot.lastObservedMs;
            row["firstObservedAgeSeconds"] =
                snapshot.firstObservedMs > 0 && nowMs > snapshot.firstObservedMs ?
                (nowMs - snapshot.firstObservedMs) / 1000 : 0;
            row["lastObservedAgeSeconds"] =
                snapshot.lastObservedMs > 0 && nowMs > snapshot.lastObservedMs ?
                (nowMs - snapshot.lastObservedMs) / 1000 : 0;
            row["sourceResourceName"] = snapshot.sourceResourceName;
            row["sourceResourceType"] = snapshot.sourceResourceType;
            row["sourcePlanet"] = snapshot.sourceZone;
            row["sourceZone"] = snapshot.sourceZone;
            row["sourceX"] = Math::getPrecision(snapshot.sourceX, 1);
            row["sourceY"] = Math::getPrecision(snapshot.sourceY, 1);
            row["sourceZ"] = Math::getPrecision(snapshot.sourceZ, 1);
            row["density"] = Math::getPrecision(snapshot.sourceDensity, 3);
            row["sourceDensity"] = Math::getPrecision(snapshot.sourceDensity, 3);
            row["selectedProfile"] = snapshot.selectedDemandProfile;
            row["selectedDemandProfile"] = snapshot.selectedDemandProfile;
            row["demandState"] = snapshot.demandState;
            row["pressureScore"] = Math::getPrecision(snapshot.pressureScore, 1);
            row["identityConfidence"] = snapshot.identityConfidence;
            row["acquisitionSource"] = snapshot.acquisitionSource;
            row["resourceLifecycleState"] = snapshot.resourceLifecycleState;
            row["yieldMode"] = snapshot.yieldMode;
            row["realResourceCreated"] = snapshot.realResourceCreated;
            row["resourceContainerCreated"] = snapshot.resourceContainerCreated;
            row["inventoryMutated"] = snapshot.inventoryMutated;
            row["economyMutated"] = snapshot.economyMutated;
            resourceAwareRows.push_back(row);

            addUint64Counter(
                resourceAwareQuantityByProfile,
                snapshot.selectedDemandProfile,
                snapshot.quantity);
        }
    }

    JSONSerializationType resourceAwareStockpile = JSONSerializationType::object();
    resourceAwareStockpile["enabled"] = true;
    resourceAwareStockpile["readOnly"] = true;
    resourceAwareStockpile["runtimeOnly"] = true;
    resourceAwareStockpile["persisted"] = false;
    resourceAwareStockpile["mode"] = "runtime-read-only";
    resourceAwareStockpile["status"] = "ready";
    resourceAwareStockpile["totalQuantity"] = resourceAwareTotalQuantity;
    resourceAwareStockpile["rowCount"] = resourceAwareRowCount;
    resourceAwareStockpile["eventCount"] = resourceAwareEventCount;
    resourceAwareStockpile["maxRows"] = 64;
    resourceAwareStockpile["rows"] = resourceAwareRows;
    resourceAwareStockpile["yieldMode"] = "conceptual";
    resourceAwareStockpile["identityConfidence"] = "observed_resource_spawn";
    resourceAwareStockpile["acquisitionSource"] = "intelligent_miner";
    resourceAwareStockpile["resourceLifecycleState"] = "conceptual";
    resourceAwareStockpile["realResourceCreated"] = false;
    resourceAwareStockpile["resourceContainerCreated"] = false;
    resourceAwareStockpile["inventoryMutated"] = false;
    resourceAwareStockpile["economyMutated"] = false;
    result["resourceAwareStockpile"] = resourceAwareStockpile;

    Vector<String> conceptualResourceNames;
    Vector<uint64> conceptualAmounts;
    collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

    JSONSerializationType sessionTotals = JSONSerializationType::array();
    VectorMap<String, uint64> sessionConceptualTotals;
    uint64 sessionTotalQuantity = 0;

    for (int i = 0; i < conceptualResourceNames.size() && i < conceptualAmounts.size(); ++i) {
        String label = conceptualResourceNames.get(i);
        uint64 amount = conceptualAmounts.get(i);
        sessionTotalQuantity += amount;
        sessionConceptualTotals.put(label, amount);

        JSONSerializationType row = JSONSerializationType::object();
        row["label"] = label;
        row["quantity"] = amount;
        sessionTotals.push_back(row);
    }

    VectorMap<String, uint64> persistentConceptualTotals;
    int persistentConceptualLots = 0;
    uint64 persistentConceptualQuantity = 0;
    String persistentStockpileStatus = "disabled";
    bool persistentSnapshotReady =
        AiEconomyManager::instance()->snapshotPersistentConceptualMinerSupplyForDemand(
            persistentConceptualTotals,
            persistentConceptualLots,
            persistentConceptualQuantity,
            persistentStockpileStatus);

    JSONSerializationType persistentTotals = JSONSerializationType::array();

    if (persistentSnapshotReady) {
        for (int i = 0; i < persistentConceptualTotals.size(); ++i) {
            JSONSerializationType row = JSONSerializationType::object();
            row["label"] = persistentConceptualTotals.elementAt(i).getKey();
            row["quantity"] = persistentConceptualTotals.get(i);
            persistentTotals.push_back(row);
        }
    }

    VectorMap<String, uint64> knownConceptualTotals;
    uint64 knownConceptualQuantity = 0;

    for (int i = 0; i < persistentConceptualTotals.size(); ++i) {
        String label = persistentConceptualTotals.elementAt(i).getKey();
        uint64 quantity = persistentConceptualTotals.get(i);
        knownConceptualTotals.put(label, quantity);
        knownConceptualQuantity += quantity;
    }

    for (int i = 0; i < conceptualResourceNames.size() && i < conceptualAmounts.size(); ++i) {
        String label = conceptualResourceNames.get(i);
        uint64 quantity = conceptualAmounts.get(i);
        uint64 aggregate = knownConceptualTotals.contains(label) ?
            knownConceptualTotals.get(label) : 0;
        aggregate += quantity;
        knownConceptualTotals.put(label, aggregate);
        knownConceptualQuantity += quantity;
    }

    JSONSerializationType knownTotals = JSONSerializationType::array();

    for (int i = 0; i < knownConceptualTotals.size(); ++i) {
        JSONSerializationType row = JSONSerializationType::object();
        row["label"] = knownConceptualTotals.elementAt(i).getKey();
        row["quantity"] = knownConceptualTotals.get(i);
        knownTotals.push_back(row);
    }

    JSONSerializationType coverageSlots = JSONSerializationType::array();
    JSONSerializationType coverageByResource = JSONSerializationType::array();
    JSONSerializationType coverageByProfile = JSONSerializationType::array();
    JSONSerializationType uncoveredHighPriorityNeeds =
        JSONSerializationType::array();
    JSONSerializationType rebalanceCandidates = JSONSerializationType::array();
    VectorMap<String, int> profileAssigned;
    VectorMap<String, int> profileStationed;
    VectorMap<String, int> profileMoving;
    VectorMap<String, int> profileSampling;
    VectorMap<String, float> profilePressure;
    VectorMap<String, int> resourceAssigned;
    VectorMap<String, int> resourceStationed;
    VectorMap<String, int> resourceMoving;
    VectorMap<String, int> resourceSampling;
    VectorMap<String, String> resourceNameByKey;
    VectorMap<String, String> resourceTypeByKey;
    VectorMap<String, String> resourceZoneByKey;
    int desiredCoverageSlots = 0;
    int actualCoveredSlots = 0;
    int stationedCoverageMiners = 0;
    int movingCoverageMiners = 0;
    int samplingCoverageMiners = 0;
    uint64 stationDurationTotalSeconds = 0;
    uint64 stationDurationMaxSeconds = 0;
    int stationDurationSamples = 0;
    int stationSampleTotal = 0;
    uint64 stationYieldTotal = 0;

    for (int i = 0; i < dashboardAssignmentSnapshots.size(); ++i) {
        MinerIntelligentTargetAssignment assignment =
            dashboardAssignmentSnapshots.get(i);
        uint64 timeoutAgeSeconds = 0;
        uint64 timeoutSeconds = 0;
        String timeoutReason =
            getMinerIntelligentAssignmentTimeoutReason(
                assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, false);
        bool expired = !timeoutReason.isEmpty();
        String status = expired ? timeoutReason : assignment.status;

        if (expired)
            continue;

        String profileKey = assignment.selectedProfileKey.isEmpty() ?
            String("unknown") : assignment.selectedProfileKey;
        String resourceKey =
            assignment.targetResourceName + "|" +
            assignment.targetResourceType + "|" +
            assignment.targetZoneName;
        addIntCounter(profileAssigned, profileKey);
        addIntCounter(resourceAssigned, resourceKey);
        resourceNameByKey.put(resourceKey, assignment.targetResourceName);
        resourceTypeByKey.put(resourceKey, assignment.targetResourceType);
        resourceZoneByKey.put(resourceKey, assignment.targetZoneName);

        if (!profilePressure.contains(profileKey) ||
                assignment.pressureScore > profilePressure.get(profileKey))
            profilePressure.put(profileKey, assignment.pressureScore);

        if (status == "stationed") {
            addIntCounter(profileStationed, profileKey);
            addIntCounter(resourceStationed, resourceKey);
            stationedCoverageMiners++;
            uint64 durationSeconds = assignment.stationedAtMs > 0 &&
                nowMs > assignment.stationedAtMs ?
                (nowMs - assignment.stationedAtMs) / 1000 :
                assignment.stationDurationSeconds;
            stationDurationTotalSeconds += durationSeconds;
            stationDurationSamples++;
            if (durationSeconds > stationDurationMaxSeconds)
                stationDurationMaxSeconds = durationSeconds;
        } else if (status == "sample_started") {
            addIntCounter(profileSampling, profileKey);
            addIntCounter(resourceSampling, resourceKey);
            samplingCoverageMiners++;
        } else if (status == "activation_started" || status == "queued") {
            addIntCounter(profileMoving, profileKey);
            addIntCounter(resourceMoving, resourceKey);
            movingCoverageMiners++;
        }

        stationSampleTotal += assignment.stationSampleCount;
        stationYieldTotal += assignment.stationYieldQuantity;

        int slotAssigned = 1;
        int slotStationed = status == "stationed" ? 1 : 0;
        int slotMoving =
            (status == "activation_started" || status == "queued") ? 1 : 0;
        int slotSampling = status == "sample_started" ? 1 : 0;
        int desiredMiners = 1;
        int gap = desiredMiners > slotAssigned ?
            desiredMiners - slotAssigned : 0;
        String conceptualLabel = assignment.targetResourceType;
        uint64 knownQuantity =
            knownConceptualTotals.contains(conceptualLabel) ?
            knownConceptualTotals.get(conceptualLabel) : 0;
        uint64 desiredReserve =
            demandWeightedMinerPlanSimulationDesiredReserve.contains(profileKey) ?
            static_cast<uint64>(
                demandWeightedMinerPlanSimulationDesiredReserve.get(profileKey)) : 0;
        float reserveRatio = desiredReserve > 0 ?
            Math::getPrecision(
                static_cast<float>(knownQuantity) /
                    static_cast<float>(desiredReserve),
                3) : 0.f;

        JSONSerializationType slot = JSONSerializationType::object();
        slot["coverageSlotId"] = assignment.targetHash.isEmpty() ?
            String::valueOf(assignment.assignmentGenerationId) :
            assignment.targetHash;
        slot["demandProfile"] = profileKey;
        slot["resourceName"] = assignment.targetResourceName;
        slot["resourceType"] = assignment.targetResourceType;
        slot["conceptualLabel"] = conceptualLabel;
        slot["zone"] = assignment.targetZoneName;
        slot["targetSource"] = assignment.targetSource;
        slot["targetHash"] = assignment.targetHash;
        slot["desiredMiners"] = desiredMiners;
        slot["assignedMinerCount"] = slotAssigned;
        slot["stationedMinerCount"] = slotStationed;
        slot["movingMinerCount"] = slotMoving;
        slot["samplingMinerCount"] = slotSampling;
        slot["coverageGap"] = gap;
        slot["pressureScore"] = Math::getPrecision(assignment.pressureScore, 1);
        slot["stockpileKnownQuantity"] = knownQuantity;
        slot["desiredReserve"] = desiredReserve;
        slot["reserveRatio"] = reserveRatio;
        slot["rebalanceReason"] = assignment.rebalanceReason.isEmpty() ?
            String("none") : assignment.rebalanceReason;
        slot["lastUpdatedMs"] = assignment.updatedAtMs;
        slot["runtimeOnly"] = true;
        slot["realResourceCreated"] = false;
        slot["resourceContainerCreated"] = false;
        slot["inventoryMutated"] = false;
        slot["economyMutated"] = false;
        coverageSlots.push_back(slot);

        if (gap == 0)
            actualCoveredSlots++;

        if (!assignment.rebalanceReason.isEmpty() ||
                status == "failed" || status == "maxStationDurationReached" ||
                status == "maxStationSamplesReached") {
            JSONSerializationType candidate = JSONSerializationType::object();
            candidate["minerId"] = assignment.minerID;
            candidate["demandProfile"] = profileKey;
            candidate["resourceName"] = assignment.targetResourceName;
            candidate["resourceType"] = assignment.targetResourceType;
            candidate["zone"] = assignment.targetZoneName;
            candidate["status"] = status;
            candidate["rebalanceReason"] = assignment.rebalanceReason.isEmpty() ?
                status : assignment.rebalanceReason;
            rebalanceCandidates.push_back(candidate);
        }
    }

    for (int i = 0; i < demandWeightedMinerPlanSimulationProfileEnabled.size(); ++i) {
        String profileKey =
            demandWeightedMinerPlanSimulationProfileEnabled.elementAt(i).getKey();

        if (demandWeightedMinerPlanSimulationProfileEnabled.get(profileKey) <= 0)
            continue;

        int desiredMiners =
            demandWeightedMinerPlanSimulationMaxMinersPerProfile;
        desiredCoverageSlots += desiredMiners;
        int assignedCount = profileAssigned.contains(profileKey) ?
            profileAssigned.get(profileKey) : 0;
        int stationedCount = profileStationed.contains(profileKey) ?
            profileStationed.get(profileKey) : 0;
        int movingCount = profileMoving.contains(profileKey) ?
            profileMoving.get(profileKey) : 0;
        int samplingCount = profileSampling.contains(profileKey) ?
            profileSampling.get(profileKey) : 0;
        int gap = desiredMiners > assignedCount ?
            desiredMiners - assignedCount : 0;
        uint64 desiredReserve =
            demandWeightedMinerPlanSimulationDesiredReserve.contains(profileKey) ?
            static_cast<uint64>(
                demandWeightedMinerPlanSimulationDesiredReserve.get(profileKey)) : 0;

        JSONSerializationType row = JSONSerializationType::object();
        row["demandProfile"] = profileKey;
        row["desiredMiners"] = desiredMiners;
        row["assignedMinerCount"] = assignedCount;
        row["stationedMinerCount"] = stationedCount;
        row["movingMinerCount"] = movingCount;
        row["samplingMinerCount"] = samplingCount;
        row["coverageGap"] = gap;
        float rowPressureScore = profilePressure.contains(profileKey) ?
            Math::getPrecision(profilePressure.get(profileKey), 1) : 0.f;
        row["pressureScore"] = rowPressureScore;
        row["desiredReserve"] = desiredReserve;
        coverageByProfile.push_back(row);

        if (gap > 0) {
            JSONSerializationType need = JSONSerializationType::object();
            need["demandProfile"] = profileKey;
            need["coverageGap"] = gap;
            need["desiredMiners"] = desiredMiners;
            need["assignedMinerCount"] = assignedCount;
            need["pressureScore"] = rowPressureScore;
            need["rebalanceReason"] = "coverageGap";
            uncoveredHighPriorityNeeds.push_back(need);
        }
    }

    for (int i = 0; i < resourceAssigned.size(); ++i) {
        String resourceKey = resourceAssigned.elementAt(i).getKey();
        JSONSerializationType row = JSONSerializationType::object();
        row["resourceKey"] = resourceKey;
        row["resourceName"] = resourceNameByKey.contains(resourceKey) ?
            resourceNameByKey.get(resourceKey) : String("unknown");
        row["resourceType"] = resourceTypeByKey.contains(resourceKey) ?
            resourceTypeByKey.get(resourceKey) : String("unknown");
        row["zone"] = resourceZoneByKey.contains(resourceKey) ?
            resourceZoneByKey.get(resourceKey) : String("unknown");
        row["assignedMinerCount"] = resourceAssigned.get(resourceKey);
        row["stationedMinerCount"] = resourceStationed.contains(resourceKey) ?
            resourceStationed.get(resourceKey) : 0;
        row["movingMinerCount"] = resourceMoving.contains(resourceKey) ?
            resourceMoving.get(resourceKey) : 0;
        row["samplingMinerCount"] = resourceSampling.contains(resourceKey) ?
            resourceSampling.get(resourceKey) : 0;
        coverageByResource.push_back(row);
    }

    JSONSerializationType stationDurationSummary =
        JSONSerializationType::object();
    stationDurationSummary["stationedCount"] = stationedCoverageMiners;
    stationDurationSummary["averageStationDurationSeconds"] =
        stationDurationSamples > 0 ?
        stationDurationTotalSeconds /
            static_cast<uint64>(stationDurationSamples) : 0;
    stationDurationSummary["maxStationDurationSeconds"] =
        stationDurationMaxSeconds;

    JSONSerializationType stationSampleSummary =
        JSONSerializationType::object();
    stationSampleSummary["stationSampleCount"] = stationSampleTotal;
    stationSampleSummary["stationYieldQuantity"] = stationYieldTotal;
    stationSampleSummary["averageSamplesPerStationedMiner"] =
        stationedCoverageMiners > 0 ?
        Math::getPrecision(
            static_cast<float>(stationSampleTotal) /
                static_cast<float>(stationedCoverageMiners),
            2) : 0.f;

    JSONSerializationType coveragePlanner = JSONSerializationType::object();
    coveragePlanner["enabled"] = true;
    coveragePlanner["runtimeOnly"] = true;
    coveragePlanner["readOnly"] = true;
    coveragePlanner["mode"] = "memory-only";
    coveragePlanner["stationedLifecycleEnabled"] =
        stationedMinerLifecycleEnabled;
    coveragePlanner["stationedRepeatedSamplingEnabled"] =
        stationedMinerRepeatedSamplingEnabled;
    coveragePlanner["desiredCoverageSlots"] = desiredCoverageSlots;
    coveragePlanner["actualCoveredSlots"] = actualCoveredSlots;
    coveragePlanner["totalCoverageGap"] =
        desiredCoverageSlots > actualCoveredSlots ?
        desiredCoverageSlots - actualCoveredSlots : 0;
    coveragePlanner["stationedMiners"] = stationedCoverageMiners;
    coveragePlanner["movingMiners"] = movingCoverageMiners;
    coveragePlanner["samplingMiners"] = samplingCoverageMiners;
    coveragePlanner["unassignedMiners"] =
        activeMiners > activeMinerAssigned.size() ?
        activeMiners - activeMinerAssigned.size() : 0;
    coveragePlanner["coverageSlots"] = coverageSlots;
    coveragePlanner["coverageByProfile"] = coverageByProfile;
    coveragePlanner["coverageByResource"] = coverageByResource;
    coveragePlanner["uncoveredHighPriorityNeeds"] =
        uncoveredHighPriorityNeeds;
    coveragePlanner["rebalanceCandidates"] = rebalanceCandidates;
    coveragePlanner["stationDurationSummary"] = stationDurationSummary;
    coveragePlanner["stationSampleSummary"] = stationSampleSummary;
    coveragePlanner["realResourceCreated"] = false;
    coveragePlanner["resourceContainerCreated"] = false;
    coveragePlanner["inventoryMutated"] = false;
    coveragePlanner["economyMutated"] = false;
    result["coveragePlanner"] = coveragePlanner;

    Vector<DemandProfileDefinition> profiles = createDemandProfileDefinitions();
    AiEconomyStockpileInspectionSnapshot stockpileSnapshot;
    String stockpileInspectionStatus;
    bool stockpileInspectionReady =
        AiEconomyManager::instance()->snapshotStockpileInspection(
            stockpileSnapshot, 24, stockpileInspectionStatus);

    JSONSerializationType labelSummaries = JSONSerializationType::array();
    Vector<String> stockpileLabels;

    for (int i = 0; i < sessionConceptualTotals.size(); ++i)
        addUniqueLabel(stockpileLabels,
            sessionConceptualTotals.elementAt(i).getKey());

    for (int i = 0; i < stockpileSnapshot.conceptualMinerQuantities.size(); ++i)
        addUniqueLabel(stockpileLabels,
            stockpileSnapshot.conceptualMinerQuantities.elementAt(i).getKey());

    for (int i = 0; i < stockpileSnapshot.startupBaselineQuantities.size(); ++i)
        addUniqueLabel(stockpileLabels,
            stockpileSnapshot.startupBaselineQuantities.elementAt(i).getKey());

    for (int i = 0; i < knownConceptualTotals.size(); ++i)
        addUniqueLabel(stockpileLabels,
            knownConceptualTotals.elementAt(i).getKey());

    for (int i = 0; i < stockpileLabels.size(); ++i) {
        String label = stockpileLabels.get(i);
        uint64 currentSessionQuantity =
            sessionConceptualTotals.contains(label) ?
            sessionConceptualTotals.get(label) : 0;
        uint64 persistedQuantity =
            stockpileSnapshot.conceptualMinerQuantities.contains(label) ?
            stockpileSnapshot.conceptualMinerQuantities.get(label) : 0;
        uint64 startupBaselineQuantity =
            stockpileSnapshot.startupBaselineQuantities.contains(label) ?
            stockpileSnapshot.startupBaselineQuantities.get(label) : 0;
        uint64 totalKnownQuantity =
            knownConceptualTotals.contains(label) ?
            knownConceptualTotals.get(label) : currentSessionQuantity +
                startupBaselineQuantity;

        JSONSerializationType row = JSONSerializationType::object();
        row["label"] = label;
        row["currentSessionQuantity"] = currentSessionQuantity;
        row["persistedQuantity"] = persistedQuantity;
        row["startupBaselineQuantity"] = startupBaselineQuantity;
        row["totalKnownQuantity"] = totalKnownQuantity;
        row["demandProfiles"] =
            getDemandProfilesForConceptualLabel(profiles, label);
        row["identityConfidence"] = "conceptual_label";
        row["source"] = "conceptual_miner";
        row["resourceLifecycleState"] = "conceptual";
        row["ownerScope"] = "galaxy";
        row["yieldMode"] = "conceptual";
        row["realResourceCreated"] = false;
        row["resourceContainerCreated"] = false;
        row["inventoryMutated"] = false;
        row["economyMutated"] = false;
        labelSummaries.push_back(row);
    }

    JSONSerializationType lotRows = JSONSerializationType::array();

    for (int i = 0; i < stockpileSnapshot.lots.size(); ++i) {
        AiEconomyStockpileInspectionLot lot = stockpileSnapshot.lots.get(i);

        JSONSerializationType row = JSONSerializationType::object();
        row["entryId"] = lot.entryID;
        row["conceptualLabel"] = lot.conceptualLabel;
        row["quantity"] = lot.quantity;
        row["reservedQuantity"] = lot.reservedQuantity;
        row["availableQuantity"] = lot.availableQuantity;
        row["acquisitionSource"] = lot.acquisitionSource;
        row["resourceLifecycleState"] = lot.resourceLifecycleState;
        row["identityConfidence"] = lot.identityConfidence;
        row["ownerScope"] = lot.ownerScope;
        row["matchedDemandProfiles"] = lot.matchedDemandProfiles;
        row["qualityTier"] = lot.qualityTier;
        row["sourceResourceName"] = lot.resourceSpawnName;
        row["sourceResourceType"] = lot.resourceType;
        row["sourcePlanet"] = lot.sourcePlanet;
        row["sourceZone"] = lot.sourceZone;
        row["resourceSpawnObjectId"] = lot.resourceSpawnObjectID;
        row["acquiredTimestampMs"] = lot.acquiredTimestampMs;
        row["lastUpdatedTimestampMs"] = lot.lastUpdatedTimestampMs;
        row["updatedAgeSeconds"] =
            lot.lastUpdatedTimestampMs > 0 && nowMs > lot.lastUpdatedTimestampMs ?
            (nowMs - lot.lastUpdatedTimestampMs) / 1000 : 0;
        row["conceptualMinerLot"] = lot.conceptualMinerLot;
        row["yieldMode"] = "conceptual";
        row["realResourceCreated"] = false;
        row["resourceContainerCreated"] = false;
        row["inventoryMutated"] = false;
        row["economyMutated"] = false;
        lotRows.push_back(row);
    }

    JSONSerializationType stockpileInspection = JSONSerializationType::object();
    stockpileInspection["readOnly"] = true;
    stockpileInspection["mode"] = "read-only";
    stockpileInspection["status"] = stockpileInspectionStatus;
    stockpileInspection["snapshotAvailable"] = stockpileInspectionReady;
    stockpileInspection["persistenceReady"] =
        stockpileSnapshot.persistenceReady;
    stockpileInspection["persistenceEnabled"] =
        stockpileSnapshot.persistenceReady;
    stockpileInspection["checkpointEnabled"] =
        aiEconomyPersistConceptualMinerTotals;
    stockpileInspection["checkpointIntervalSeconds"] =
        aiEconomyPersistenceIntervalSeconds;
    stockpileInspection["persistentStockpileDemandEnabled"] =
        persistentStockpileDemandEnabled;
    stockpileInspection["persistentStockpileDemandIncludeConceptualMinerLots"] =
        persistentStockpileDemandIncludeConceptualMinerLots;
    stockpileInspection["persistentStockpileDemandMode"] =
        persistentStockpileDemandEnabled &&
            persistentStockpileDemandIncludeConceptualMinerLots ?
        String("startup_baseline_only") : String("disabled");
    stockpileInspection["loadedLots"] = stockpileSnapshot.loadedLots;
    stockpileInspection["conceptualMinerLots"] =
        stockpileSnapshot.conceptualMinerLots;
    stockpileInspection["totalQuantity"] =
        stockpileSnapshot.totalQuantity;
    stockpileInspection["conceptualMinerQuantity"] =
        stockpileSnapshot.conceptualMinerQuantity;
    stockpileInspection["startupBaselineQuantity"] =
        stockpileSnapshot.startupBaselineQuantity;
    stockpileInspection["currentSessionQuantity"] =
        sessionTotalQuantity;
    stockpileInspection["availableQuantity"] =
        stockpileSnapshot.availableQuantity;
    stockpileInspection["reservedQuantity"] =
        stockpileSnapshot.reservedQuantity;
    stockpileInspection["dataCreatedTimestampMs"] =
        stockpileSnapshot.dataCreatedTimestampMs;
    stockpileInspection["dataUpdatedTimestampMs"] =
        stockpileSnapshot.dataUpdatedTimestampMs;
    stockpileInspection["updatedAgeSeconds"] =
        stockpileSnapshot.dataUpdatedTimestampMs > 0 &&
            nowMs > stockpileSnapshot.dataUpdatedTimestampMs ?
        (nowMs - stockpileSnapshot.dataUpdatedTimestampMs) / 1000 : 0;
    stockpileInspection["identityConfidence"] = "conceptual_label";
    stockpileInspection["yieldMode"] = "conceptual";
    stockpileInspection["realResourceCreated"] = false;
    stockpileInspection["resourceContainerCreated"] = false;
    stockpileInspection["inventoryMutated"] = false;
    stockpileInspection["economyMutated"] = false;
    stockpileInspection["labelSummaries"] = labelSummaries;
    stockpileInspection["lots"] = lotRows;
    stockpileInspection["lotRowsTruncated"] =
        stockpileSnapshot.loadedLots > stockpileSnapshot.lots.size();
    result["stockpileInspection"] = stockpileInspection;

    JSONSerializationType supply = JSONSerializationType::object();
    supply["currentSessionConceptualTotals"] = sessionTotals;
    supply["currentSessionConceptualTotalQuantity"] = sessionTotalQuantity;
    supply["persistentBaselineStockpile"] = persistentTotals;
    supply["persistentBaselineStockpileLots"] = persistentConceptualLots;
    supply["persistentBaselineStockpileQuantity"] = persistentConceptualQuantity;
    supply["persistentBaselineStatus"] = persistentStockpileStatus;
    supply["persistentBaselineReady"] = persistentSnapshotReady;
    supply["totalKnownConceptualSupply"] = knownTotals;
    supply["totalKnownConceptualQuantity"] = knownConceptualQuantity;
    result["supply"] = supply;

    Vector<ResourceIntelligenceEntry> entries;
    String snapshotError;
    bool activeSnapshotAvailable = collectResourceIntelligenceSnapshot(entries, snapshotError);

    if (activeSnapshotAvailable)
        calculateResourceIntelligenceScores(entries);

    VectorMap<String, uint64> marketQuantities;
    VectorMap<String, int> marketListings;
    VectorMap<String, float> marketCheapestPrices;
    VectorMap<String, float> marketMedianPrices;
    VectorMap<String, String> marketConfidences;
    VectorMap<String, String> marketTopResources;
    VectorMap<String, String> marketTopTypes;

    if (marketSupplyObservationEnabled) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            String profileKey = profiles.get(profileIndex).key;

            if (marketSupplyProfileQuantities.contains(profileKey))
                marketQuantities.put(profileKey, marketSupplyProfileQuantities.get(profileKey));
            if (marketSupplyProfileListings.contains(profileKey))
                marketListings.put(profileKey, marketSupplyProfileListings.get(profileKey));
            if (marketSupplyProfileCheapestPricePerUnit.contains(profileKey))
                marketCheapestPrices.put(profileKey, marketSupplyProfileCheapestPricePerUnit.get(profileKey));
            if (marketSupplyProfileMedianPricePerUnit.contains(profileKey))
                marketMedianPrices.put(profileKey, marketSupplyProfileMedianPricePerUnit.get(profileKey));
            if (marketSupplyProfileConfidence.contains(profileKey))
                marketConfidences.put(profileKey, marketSupplyProfileConfidence.get(profileKey));
            if (marketSupplyProfileTopResource.contains(profileKey))
                marketTopResources.put(profileKey, marketSupplyProfileTopResource.get(profileKey));
            if (marketSupplyProfileTopType.contains(profileKey))
                marketTopTypes.put(profileKey, marketSupplyProfileTopType.get(profileKey));
        }
    }

    Vector<DemandStateSimulationResult> demandResults;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        DemandProfileDefinition profile = profiles.get(profileIndex);
        bool profileEnabled = !demandStateSimulationProfileEnabled.contains(profile.key) ||
            demandStateSimulationProfileEnabled.get(profile.key) != 0;

        if (!profileEnabled)
            continue;

        DemandStateSimulationResult demandResult;
        demandResult.profileKey = profile.key;
        demandResult.desiredReserve = demandStateSimulationDesiredReserve.contains(profile.key) ?
            static_cast<uint64>(demandStateSimulationDesiredReserve.get(profile.key)) : 0;
        demandResult.aiConceptualSupply = estimateConceptualDemandStateSupply(
            profile.key,
            conceptualResourceNames,
            conceptualAmounts,
            demandResult.supplyConfidence,
            demandResult.supplyLabels);
        demandResult.marketObservedSupply = marketQuantities.contains(profile.key) ?
            marketQuantities.get(profile.key) : 0;
        demandResult.marketListingsMatched = marketListings.contains(profile.key) ?
            marketListings.get(profile.key) : 0;
        demandResult.marketCheapestPricePerUnit =
            marketCheapestPrices.contains(profile.key) ?
            marketCheapestPrices.get(profile.key) : -1.f;
        demandResult.marketMedianPricePerUnit =
            marketMedianPrices.contains(profile.key) ?
            marketMedianPrices.get(profile.key) : -1.f;
        demandResult.marketSupplyConfidence = marketConfidences.contains(profile.key) ?
            marketConfidences.get(profile.key) : "none";
        demandResult.marketTopResource = marketTopResources.contains(profile.key) ?
            marketTopResources.get(profile.key) : "";
        demandResult.marketTopType = marketTopTypes.contains(profile.key) ?
            marketTopTypes.get(profile.key) : "";
        demandResult.supplyConfidence = combineSupplyConfidence(
            demandResult.supplyConfidence,
            demandResult.marketSupplyConfidence);

        if (persistentStockpileDemandEnabled) {
            demandResult.persistentStockpileMode =
                persistentStockpileDemandIncludeConceptualMinerLots ?
                String("startup_baseline_only") : String("disabled");
            demandResult.persistentStockpileStatus = persistentStockpileStatus;

            if (persistentStockpileDemandIncludeConceptualMinerLots &&
                    persistentStockpileStatus == "ready") {
                demandResult.persistentStockpileSupply =
                    estimatePersistentConceptualDemandStateSupply(
                        profile.key,
                        persistentConceptualTotals,
                        demandResult.persistentStockpileLotsMatched,
                        demandResult.persistentStockpileConfidence,
                        demandResult.persistentStockpileLabels);
                demandResult.persistentStockpileQuantityMatched =
                    demandResult.persistentStockpileSupply;
                demandResult.supplyConfidence = combineSupplyConfidence(
                    demandResult.supplyConfidence,
                    demandResult.persistentStockpileConfidence);
            }
        }

        float lowThreshold = demandStateSimulationLowStockThreshold.contains(profile.key) ?
            demandStateSimulationLowStockThreshold.get(profile.key) : 0.35f;
        float criticalThreshold = demandStateSimulationCriticalStockThreshold.contains(profile.key) ?
            demandStateSimulationCriticalStockThreshold.get(profile.key) : 0.10f;

        demandResult.activeProfileAvailableForPhase =
            demandProfileActiveForPhase(profile, demandProfileSimulationServerPhase);

        if (activeSnapshotAvailable && demandResult.activeProfileAvailableForPhase) {
            for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                ResourceIntelligenceEntry entry = entries.get(entryIndex);
                DemandProfileMatch match =
                    evaluateDemandProfileResource(entry, profile, 1.f, 100);

                if (!match.eligible ||
                        (demandResult.hasActiveOpportunity &&
                         match.demandScore <= demandResult.activeMatch.demandScore)) {
                    continue;
                }

                demandResult.hasActiveOpportunity = true;
                demandResult.activeResource = entry;
                demandResult.activeMatch = match;
            }
        }

        calculateDemandStatePressure(
            demandResult,
            lowThreshold,
            criticalThreshold,
            demandStateSimulationShortageWeight,
            demandStateSimulationActiveOpportunityWeight,
            demandStateSimulationSurplusDampening);

        demandResults.add(demandResult);
    }

    for (int i = 0; i < demandResults.size(); ++i) {
        for (int j = i + 1; j < demandResults.size(); ++j) {
            if (demandResults.get(j).pressureScore <= demandResults.get(i).pressureScore)
                continue;

            DemandStateSimulationResult swap = demandResults.get(i);
            demandResults.set(i, demandResults.get(j));
            demandResults.set(j, swap);
        }
    }

    JSONSerializationType demandRows = JSONSerializationType::array();

    for (int i = 0; i < demandResults.size(); ++i) {
        DemandStateSimulationResult demandResult = demandResults.get(i);
        String stateGroup = demandResult.state;

        if (demandResult.state == "critical" || demandResult.state == "low")
            stateGroup = "shortage";
        else if (demandResult.state == "disabledReserve")
            stateGroup = "disabled";

        JSONSerializationType row = JSONSerializationType::object();
        row["profile"] = demandResult.profileKey;
        row["state"] = demandResult.state;
        row["stateGroup"] = stateGroup;
        row["desiredReserve"] = demandResult.desiredReserve;
        row["knownSupply"] = demandResult.totalKnownSupply;
        row["aiConceptualSupply"] = demandResult.aiConceptualSupply;
        row["persistentStockpileSupply"] = demandResult.persistentStockpileSupply;
        row["marketObservedSupply"] = demandResult.marketObservedSupply;
        row["shortageUnits"] = demandResult.shortageUnits;
        row["surplusUnits"] = demandResult.surplusUnits;
        row["reserveRatio"] = Math::getPrecision(demandResult.reserveRatio, 3);
        row["pressureScore"] = Math::getPrecision(demandResult.pressureScore, 1);
        row["supplyConfidence"] = demandResult.supplyConfidence;
        row["supplyLabels"] = demandResult.supplyLabels;
        row["activeProfileAvailableForPhase"] =
            demandResult.activeProfileAvailableForPhase;

        JSONSerializationType opportunity = JSONSerializationType::object();
        opportunity["available"] = demandResult.hasActiveOpportunity;

        if (demandResult.hasActiveOpportunity) {
            opportunity["resourceName"] = demandResult.activeResource.name;
            opportunity["resourceType"] = demandResult.activeResource.type;
            opportunity["zones"] = demandResult.activeResource.zones;
            opportunity["demandScore"] = demandResult.activeMatch.demandScore;
            opportunity["matchedType"] = demandResult.activeMatch.matchedType;
            opportunity["oq"] = demandResult.activeResource.oq;
            opportunity["cd"] = demandResult.activeResource.cd;
            opportunity["dr"] = demandResult.activeResource.dr;
            opportunity["ma"] = demandResult.activeResource.ma;
            opportunity["pe"] = demandResult.activeResource.pe;
        } else if (!activeSnapshotAvailable) {
            opportunity["reason"] = snapshotError;
        } else if (!demandResult.activeProfileAvailableForPhase) {
            opportunity["reason"] = "profile_inactive_for_phase";
        } else {
            opportunity["reason"] = "no_eligible_active_resource";
        }

        row["activeOpportunityResource"] = opportunity;
        demandRows.push_back(row);
    }

    JSONSerializationType demand = JSONSerializationType::object();
    demand["enabled"] = demandStateSimulationEnabled;
    demand["supplyMode"] = demandStateSimulationSupplyMode;
    demand["serverPhase"] = demandProfileSimulationServerPhase;
    demand["activeResourceSnapshotAvailable"] = activeSnapshotAvailable;
    demand["activeResourceSnapshotError"] = snapshotError;
    demand["profiles"] = demandRows;
    result["demand"] = demand;

    JSONSerializationType resourceScout = JSONSerializationType::object();
    resourceScout["enabled"] = resourceIntelligenceEnabled;
    resourceScout["readOnly"] = true;
    resourceScout["mode"] = "read-only";
    resourceScout["status"] = activeSnapshotAvailable ?
        String("ready") : String("no_data");
    resourceScout["source"] = "resource_intelligence_snapshot";
    resourceScout["snapshotAvailable"] = activeSnapshotAvailable;
    resourceScout["snapshotError"] = snapshotError;
    resourceScout["activeResourceCount"] = activeSnapshotAvailable ? entries.size() : 0;
    resourceScout["serverPhase"] = demandProfileSimulationServerPhase;

    JSONSerializationType broadOpportunities = JSONSerializationType::array();
    int broadOpportunityCount = 0;

    if (activeSnapshotAvailable) {
        for (int scoreFamily = 0; scoreFamily < 5; ++scoreFamily) {
            int bestIndex = -1;
            int bestScore = 0;

            for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                ResourceIntelligenceEntry entry = entries.get(entryIndex);

                if (!broadScoreFamilyAllowsResource(entry, scoreFamily))
                    continue;

                int score = getResourceIntelligenceScore(entry, scoreFamily);

                if (score <= bestScore)
                    continue;

                bestIndex = entryIndex;
                bestScore = score;
            }

            if (bestIndex < 0 || bestScore <= 0)
                continue;

            JSONSerializationType opportunity = buildResourceScoutOpportunityJSON(
                entries.get(bestIndex),
                getResourceScoutBestUse(scoreFamily),
                "resource_intelligence_snapshot",
                bestScore);
            opportunity["category"] = getResourceScoutCategory(scoreFamily);
            opportunity["rank"] = 1;
            broadOpportunities.push_back(opportunity);
            broadOpportunityCount++;
        }
    }

    resourceScout["topBroadOpportunities"] = broadOpportunities;
    resourceScout["topBroadOpportunityCount"] = broadOpportunityCount;

    JSONSerializationType demandOpportunities = JSONSerializationType::array();
    int demandOpportunityCount = 0;
    int noDensityTargets = 0;
    int highValueUnassigned = 0;
    const int highValueDemandScore = 750;

    for (int i = 0; i < demandResults.size(); ++i) {
        DemandStateSimulationResult demandResult = demandResults.get(i);

        if (!demandResult.hasActiveOpportunity)
            continue;

        DemandProfileDefinition profile;
        bool foundProfile = findDemandProfileDefinition(
            profiles, demandResult.profileKey, profile);
        String category = foundProfile ? profile.category : String("unknown");
        String stateGroup = demandResult.state;

        if (demandResult.state == "critical" || demandResult.state == "low")
            stateGroup = "shortage";
        else if (demandResult.state == "disabledReserve")
            stateGroup = "disabled";

        JSONSerializationType opportunity = buildResourceScoutOpportunityJSON(
            demandResult.activeResource,
            demandResult.profileKey,
            "demand_state_snapshot",
            demandResult.activeMatch.demandScore);
        opportunity["category"] = category;
        opportunity["rank"] = demandOpportunityCount + 1;

        JSONSerializationType demandInfo = JSONSerializationType::object();
        demandInfo["profile"] = demandResult.profileKey;
        demandInfo["category"] = category;
        demandInfo["state"] = demandResult.state;
        demandInfo["stateGroup"] = stateGroup;
        demandInfo["priority"] = Math::getPrecision(demandResult.pressureScore, 1);
        demandInfo["demandScore"] = demandResult.activeMatch.demandScore;
        demandInfo["matchedType"] = demandResult.activeMatch.matchedType;
        demandInfo["desiredReserve"] = demandResult.desiredReserve;
        demandInfo["knownSupply"] = demandResult.totalKnownSupply;
        demandInfo["shortageUnits"] = demandResult.shortageUnits;
        opportunity["demand"] = demandInfo;

        demandOpportunities.push_back(opportunity);
        demandOpportunityCount++;
        noDensityTargets++;
    }

    {
        Locker assignmentLocker(&minerIntelligentTargetingAssignmentMutex);

        for (int resultIndex = 0; resultIndex < demandResults.size(); ++resultIndex) {
            DemandStateSimulationResult demandResult = demandResults.get(resultIndex);

            if (!demandResult.hasActiveOpportunity ||
                    demandResult.activeMatch.demandScore < highValueDemandScore)
                continue;

            bool assigned = false;

            for (int assignmentIndex = 0;
                    assignmentIndex < minerIntelligentTargetAssignments.size();
                    ++assignmentIndex) {
                MinerIntelligentTargetAssignment assignment =
                    minerIntelligentTargetAssignments.elementAt(assignmentIndex).getValue();

                if (!isMinerIntelligentAssignmentActive(assignment))
                    continue;

                if (assignment.targetResourceName == demandResult.activeResource.name &&
                        assignment.targetResourceType == demandResult.activeResource.type) {
                    assigned = true;
                    break;
                }
            }

            if (!assigned)
                highValueUnassigned++;
        }
    }

    resourceScout["demandOpportunities"] = demandOpportunities;
    resourceScout["demandOpportunityCount"] = demandOpportunityCount;

    JSONSerializationType coverageRows = JSONSerializationType::array();
    JSONSerializationType highestUncovered = JSONSerializationType::object();
    bool hasHighestUncovered = false;
    int coverageTopLimit = Math::min(demandResults.size(), 12);
    int coverageCovered = 0;
    int coverageUncovered = 0;
    int coverageAssignedMiners = 0;
    int coverageActiveMiners = 0;
    int coverageBlockedByPath = 0;
    int coverageBlockedByDensity = 0;
    int coverageWrongPlanet = 0;
    int coverageCooldown = 0;
    int coverageCapped = 0;
    VectorMap<String, int> profileCoveredOpportunities;
    VectorMap<String, int> profileUncoveredOpportunities;
    JSONSerializationType coverageAlignmentOpportunityRows =
        JSONSerializationType::array();
    JSONSerializationType coverageAlignmentAssignmentRows =
        JSONSerializationType::array();
    int alignmentOpportunitiesWithExactMatch = 0;
    int alignmentOpportunitiesWithActiveMatch = 0;
    int alignmentOpportunitiesWithCandidateMatch = 0;
    int alignmentOpportunitiesWithValidatedMatch = 0;
    int alignmentOpportunitiesWithUntrustedMatch = 0;
    int alignmentOpportunitiesWithStaleMatch = 0;
    int alignmentOpportunitiesWithProfileMismatch = 0;
    int alignmentOpportunitiesWithResourceMismatch = 0;
    int alignmentOpportunitiesWithZoneMismatch = 0;
    int alignmentOpportunitiesWithNormalizedKeyMismatch = 0;
    int alignmentOpportunitiesWithoutActiveLocalMiner = 0;
    int alignmentOpportunitiesWithoutConfiguredSpawnZone = 0;
    int alignmentOpportunitiesTravelRequiredUnsupported = 0;
    int alignmentAssignmentsExactTopMatch = 0;
    int alignmentAssignmentsCovered = 0;
    int alignmentAssignmentsCandidate = 0;
    int alignmentAssignmentsValidated = 0;
    int alignmentAssignmentsUntrusted = 0;
    int alignmentAssignmentsStale = 0;
    int alignmentAssignmentsNotTopOpportunity = 0;
    int alignmentAssignmentsProfileMismatch = 0;
    int alignmentAssignmentsResourceMismatch = 0;
    int alignmentAssignmentsZoneMismatch = 0;
    int alignmentAssignmentsNormalizedKeyMismatch = 0;

    {
        Locker assignmentLocker(&minerIntelligentTargetingAssignmentMutex);

        for (int resultIndex = 0;
                resultIndex < demandResults.size() && resultIndex < coverageTopLimit;
                ++resultIndex) {
            DemandStateSimulationResult demandResult = demandResults.get(resultIndex);

            if (!demandResult.hasActiveOpportunity)
                continue;

            DemandProfileDefinition profile;
            bool foundProfile = findDemandProfileDefinition(
                profiles, demandResult.profileKey, profile);
            String category = foundProfile ? profile.category : String("unknown");
            bool hasActiveMinerInOpportunityZone =
                resourceCoverageZonesContainAny(
                    activeMinerZones, demandResult.activeResource);
            bool hasConfiguredMinerSpawnInOpportunityZone =
                resourceCoverageZonesContainAny(
                    configuredMinerSpawnZones, demandResult.activeResource);
            bool travelRequired = !hasActiveMinerInOpportunityZone;
            bool travelSupported = false;
            int alignmentAssignmentCount = minerIntelligentTargetAssignments.size();
            int alignmentResourceMatches = 0;
            int alignmentNormalizedResourceMatches = 0;
            int alignmentProfileMatches = 0;
            int alignmentZoneMatches = 0;
            int alignmentExactProfileResourceMatches = 0;
            int alignmentExactProfileResourceZoneMatches = 0;
            int alignmentActiveMatches = 0;
            int alignmentCandidateMatches = 0;
            int alignmentValidatedMatches = 0;
            int alignmentUntrustedMatches = 0;
            int alignmentStaleMatches = 0;
            int closestScore = -1;
            uint64 closestMinerID = 0;
            String closestAssignmentStatus = "none";
            String closestMatchReason = "no_assignment_exists";
            int assignedCount = 0;
            int activeCount = 0;
            int queuedCount = 0;
            int movingCount = 0;
            int samplingCount = 0;
            String coverageStatus = "uncovered";
            JSONSerializationType miners = JSONSerializationType::array();

            for (int assignmentIndex = 0;
                    assignmentIndex < minerIntelligentTargetAssignments.size();
                    ++assignmentIndex) {
                MinerIntelligentTargetAssignment assignment =
                    minerIntelligentTargetAssignments.elementAt(assignmentIndex).getValue();
	                uint64 timeoutAgeSeconds = 0;
	                uint64 timeoutSeconds = 0;
	                String timeoutReason =
	                    getMinerIntelligentAssignmentTimeoutReason(
	                        assignment, nowMs, timeoutAgeSeconds,
	                        timeoutSeconds, false);
	                bool expired = !timeoutReason.isEmpty();
                bool resourceMatch = assignmentTargetsResource(
                    assignment, demandResult.activeResource);
                bool normalizedResourceMatch =
                    !resourceMatch &&
                    assignmentTargetsResourceNormalized(
                        assignment, demandResult.activeResource);
                bool profileMatch =
                    assignment.selectedProfileKey == demandResult.profileKey;
                bool zoneMatch = resourceCoverageZoneContains(
                    assignment.targetZoneName, demandResult.activeResource);
                bool exactProfileResourceMatch =
                    resourceMatch && profileMatch;
                bool exactProfileResourceZoneMatch =
                    exactProfileResourceMatch && zoneMatch;
                bool active =
                    !expired && isMinerIntelligentAssignmentActive(assignment);
                bool untrusted =
                    !expired && isCoverageAlignmentUntrustedAssignment(assignment);

                if (resourceMatch)
                    alignmentResourceMatches++;
                if (normalizedResourceMatch)
                    alignmentNormalizedResourceMatches++;
                if (profileMatch)
                    alignmentProfileMatches++;
                if (zoneMatch)
                    alignmentZoneMatches++;
                if (exactProfileResourceMatch)
                    alignmentExactProfileResourceMatches++;

                if (exactProfileResourceZoneMatch) {
                    alignmentExactProfileResourceZoneMatches++;

                    if (active)
                        alignmentActiveMatches++;
                    if (assignment.status == "candidate")
                        alignmentCandidateMatches++;
                    if (assignment.status == "validated")
                        alignmentValidatedMatches++;
                    if (untrusted)
                        alignmentUntrustedMatches++;
                    if (expired)
                        alignmentStaleMatches++;
                }

                int matchScore = 0;

                if (resourceMatch)
                    matchScore += 40;
                else if (normalizedResourceMatch)
                    matchScore += 30;

                if (profileMatch)
                    matchScore += 20;
                if (zoneMatch)
                    matchScore += 10;
                if (active)
                    matchScore += 3;
                else if (assignment.status == "validated")
                    matchScore += 2;
                else if (assignment.status == "candidate")
                    matchScore += 1;

                if (matchScore > closestScore) {
                    closestScore = matchScore;
                    closestMinerID = assignment.minerID;
                    closestAssignmentStatus =
	                        expired ? timeoutReason : assignment.status;
                    closestMatchReason = getCoverageAlignmentMatchReason(
                        resourceMatch,
                        normalizedResourceMatch,
                        profileMatch,
                        zoneMatch,
                        expired,
                        active,
                        untrusted,
                        assignment.status);
                }
            }

            for (int assignmentIndex = 0;
                    assignmentIndex < minerIntelligentTargetAssignments.size();
                    ++assignmentIndex) {
                MinerIntelligentTargetAssignment assignment =
                    minerIntelligentTargetAssignments.elementAt(assignmentIndex).getValue();

                if (!assignmentTargetsResource(
                        assignment, demandResult.activeResource))
                    continue;

                assignedCount++;

                if (isMinerIntelligentAssignmentActive(assignment)) {
                    activeCount++;

                    if (assignment.status == "queued")
                        queuedCount++;
                    else if (assignment.status == "activation_started")
                        movingCount++;
                    else if (assignment.status == "sample_started")
                        samplingCount++;
                }

                String blocker = classifyResourceCoverageBlocker(assignment);

                if (coverageStatus == "uncovered" && blocker != "uncovered")
                    coverageStatus = blocker;

                JSONSerializationType miner = JSONSerializationType::object();
                miner["minerId"] = assignment.minerID;
                miner["status"] = assignment.status;
                miner["densityTargetStatus"] = assignment.densityTargetStatus;
                miner["pathValidationStatus"] = assignment.pathValidationStatus;
                miner["lastActivationResult"] = assignment.lastActivationResult;
                miner["lastFailureReason"] = assignment.lastFailureReason;
                miners.push_back(miner);
            }

            if (activeCount > 0)
                coverageStatus = "covered";

            if (coverageStatus == "covered") {
                coverageCovered++;
                addIntCounter(
                    profileCoveredOpportunities,
                    demandResult.profileKey);
            } else {
                coverageUncovered++;
                addIntCounter(
                    profileUncoveredOpportunities,
                    demandResult.profileKey);

                if (coverageStatus == "blocked_by_path")
                    coverageBlockedByPath++;
                else if (coverageStatus == "blocked_by_density")
                    coverageBlockedByDensity++;
                else if (coverageStatus == "wrong_planet")
                    coverageWrongPlanet++;
                else if (coverageStatus == "cooldown")
                    coverageCooldown++;
                else if (coverageStatus == "capped")
                    coverageCapped++;
            }

            coverageAssignedMiners += assignedCount;
            coverageActiveMiners += activeCount;

            JSONSerializationType row = buildResourceScoutOpportunityJSON(
                demandResult.activeResource,
                demandResult.profileKey,
                "resource_coverage_snapshot",
                demandResult.activeMatch.demandScore);
            row["rank"] = resultIndex + 1;
            row["category"] = category;
            row["coverageStatus"] = coverageStatus;
            row["coverageReason"] = getResourceCoverageReason(coverageStatus);
            row["assignedMinerCount"] = assignedCount;
            row["activeMinerCount"] = activeCount;
            row["queuedMinerCount"] = queuedCount;
            row["movingMinerCount"] = movingCount;
            row["samplingMinerCount"] = samplingCount;
            row["miners"] = miners;

            JSONSerializationType demandInfo = JSONSerializationType::object();
            demandInfo["profile"] = demandResult.profileKey;
            demandInfo["category"] = category;
            demandInfo["state"] = demandResult.state;
            demandInfo["priority"] = Math::getPrecision(demandResult.pressureScore, 1);
            demandInfo["demandScore"] = demandResult.activeMatch.demandScore;
            demandInfo["matchedType"] = demandResult.activeMatch.matchedType;
            demandInfo["knownSupply"] = demandResult.totalKnownSupply;
            demandInfo["desiredReserve"] = demandResult.desiredReserve;
            row["demand"] = demandInfo;

            coverageRows.push_back(row);

            String alignmentDiagnosis = "no_assignment_exists";

            if (alignmentActiveMatches > 0) {
                alignmentDiagnosis = "covered_by_active_assignment";
                alignmentOpportunitiesWithActiveMatch++;
            } else if (!hasConfiguredMinerSpawnInOpportunityZone) {
                alignmentDiagnosis = "unreachable_no_configured_miner_spawn_zone";
                alignmentOpportunitiesWithoutConfiguredSpawnZone++;
            } else if (travelRequired && !travelSupported) {
                alignmentDiagnosis = "travel_required_unsupported";
                alignmentOpportunitiesWithoutActiveLocalMiner++;
                alignmentOpportunitiesTravelRequiredUnsupported++;
            } else if (alignmentExactProfileResourceZoneMatches > 0) {
                if (alignmentStaleMatches >= alignmentExactProfileResourceZoneMatches) {
                    alignmentDiagnosis = "assignment_exists_but_stale";
                    alignmentOpportunitiesWithStaleMatch++;
                } else if (alignmentUntrustedMatches > 0) {
                    alignmentDiagnosis = "assignment_exists_but_path_untrusted";
                    alignmentOpportunitiesWithUntrustedMatch++;
                } else if (alignmentValidatedMatches > 0) {
                    alignmentDiagnosis = "assignment_validated_waiting_for_activation";
                    alignmentOpportunitiesWithValidatedMatch++;
                } else if (alignmentCandidateMatches > 0) {
                    alignmentDiagnosis = "assignment_exists_but_candidate_not_validated";
                    alignmentOpportunitiesWithCandidateMatch++;
                } else {
                    alignmentDiagnosis = "assignment_exists_but_not_active";
                }
            } else if (alignmentExactProfileResourceMatches > 0) {
                alignmentDiagnosis = "assignment_exists_but_target_zone_mismatch";
                alignmentOpportunitiesWithZoneMismatch++;
            } else if (alignmentResourceMatches > 0) {
                alignmentDiagnosis = "assignment_exists_but_profile_mismatch";
                alignmentOpportunitiesWithProfileMismatch++;
            } else if (alignmentNormalizedResourceMatches > 0) {
                alignmentDiagnosis =
                    "assignment_exists_but_resource_key_normalization_differs";
                alignmentOpportunitiesWithNormalizedKeyMismatch++;
            } else if (alignmentProfileMatches > 0) {
                alignmentDiagnosis = "assignment_exists_but_resource_mismatch";
                alignmentOpportunitiesWithResourceMismatch++;
            }

            if (alignmentExactProfileResourceZoneMatches > 0)
                alignmentOpportunitiesWithExactMatch++;

            JSONSerializationType alignmentRow = JSONSerializationType::object();
            alignmentRow["rank"] = resultIndex + 1;
            alignmentRow["resourceName"] = demandResult.activeResource.name;
            alignmentRow["resourceType"] = demandResult.activeResource.type;
            alignmentRow["zones"] = demandResult.activeResource.zones;
            alignmentRow["profile"] = demandResult.profileKey;
            alignmentRow["category"] = category;
            alignmentRow["demandState"] = demandResult.state;
            alignmentRow["pressureScore"] =
                Math::getPrecision(demandResult.pressureScore, 1);
            alignmentRow["coverageStatus"] = coverageStatus;
            alignmentRow["coverageReason"] =
                getResourceCoverageReason(coverageStatus);
            alignmentRow["diagnosis"] = alignmentDiagnosis;
            alignmentRow["hasActiveMinerInOpportunityZone"] =
                hasActiveMinerInOpportunityZone;
            alignmentRow["hasConfiguredMinerSpawnInOpportunityZone"] =
                hasConfiguredMinerSpawnInOpportunityZone;
            alignmentRow["travelRequired"] = travelRequired;
            alignmentRow["travelSupported"] = travelSupported;
            alignmentRow["samePlanetRequired"] =
                minerIntelligentTargetingLimitedRequireSamePlanet;
            alignmentRow["activeMinerZones"] = joinCoverageZones(activeMinerZones);
            alignmentRow["configuredMinerSpawnZones"] =
                joinCoverageZones(configuredMinerSpawnZones);
            alignmentRow["assignmentCount"] = alignmentAssignmentCount;
            alignmentRow["resourceMatchCount"] = alignmentResourceMatches;
            alignmentRow["normalizedResourceMatchCount"] =
                alignmentNormalizedResourceMatches;
            alignmentRow["profileMatchCount"] = alignmentProfileMatches;
            alignmentRow["zoneMatchCount"] = alignmentZoneMatches;
            alignmentRow["exactProfileResourceMatchCount"] =
                alignmentExactProfileResourceMatches;
            alignmentRow["exactProfileResourceZoneMatchCount"] =
                alignmentExactProfileResourceZoneMatches;
            alignmentRow["activeMatchCount"] = alignmentActiveMatches;
            alignmentRow["candidateMatchCount"] = alignmentCandidateMatches;
            alignmentRow["validatedMatchCount"] = alignmentValidatedMatches;
            alignmentRow["untrustedMatchCount"] = alignmentUntrustedMatches;
            alignmentRow["staleMatchCount"] = alignmentStaleMatches;
            alignmentRow["closestMinerId"] = closestMinerID;
            alignmentRow["closestAssignmentStatus"] = closestAssignmentStatus;
            alignmentRow["closestMatchReason"] = closestMatchReason;
            coverageAlignmentOpportunityRows.push_back(alignmentRow);

            if (coverageStatus != "covered" && !hasHighestUncovered) {
                highestUncovered["resourceName"] = demandResult.activeResource.name;
                highestUncovered["resourceType"] = demandResult.activeResource.type;
                highestUncovered["bestUse"] = demandResult.profileKey;
                highestUncovered["pressureScore"] =
                    Math::getPrecision(demandResult.pressureScore, 1);
                highestUncovered["zone"] =
                    getResourceScoutPlanet(demandResult.activeResource);
                highestUncovered["zones"] = demandResult.activeResource.zones;
                highestUncovered["coverageStatus"] = coverageStatus;
                highestUncovered["reason"] =
                    getResourceCoverageReason(coverageStatus);
                hasHighestUncovered = true;
            }
        }

        for (int assignmentIndex = 0;
                assignmentIndex < minerIntelligentTargetAssignments.size();
                ++assignmentIndex) {
            MinerIntelligentTargetAssignment assignment =
                minerIntelligentTargetAssignments.elementAt(assignmentIndex).getValue();
	            uint64 timeoutAgeSeconds = 0;
	            uint64 timeoutSeconds = 0;
	            String timeoutReason =
	                getMinerIntelligentAssignmentTimeoutReason(
	                    assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, false);
	            bool expired = !timeoutReason.isEmpty();
            bool active =
                !expired && isMinerIntelligentAssignmentActive(assignment);
            bool untrusted =
                !expired && isCoverageAlignmentUntrustedAssignment(assignment);
            int bestScore = -1;
            int bestRank = 0;
            String bestResourceName;
            String bestResourceType;
            String bestProfile;
            String bestZones;
            bool bestResourceMatch = false;
            bool bestNormalizedResourceMatch = false;
            bool bestProfileMatch = false;
            bool bestZoneMatch = false;

            for (int resultIndex = 0;
                    resultIndex < demandResults.size() && resultIndex < coverageTopLimit;
                    ++resultIndex) {
                DemandStateSimulationResult demandResult = demandResults.get(resultIndex);

                if (!demandResult.hasActiveOpportunity)
                    continue;

                bool resourceMatch = assignmentTargetsResource(
                    assignment, demandResult.activeResource);
                bool normalizedResourceMatch =
                    !resourceMatch &&
                    assignmentTargetsResourceNormalized(
                        assignment, demandResult.activeResource);
                bool profileMatch =
                    assignment.selectedProfileKey == demandResult.profileKey;
                bool zoneMatch = resourceCoverageZoneContains(
                    assignment.targetZoneName, demandResult.activeResource);
                int matchScore = 0;

                if (resourceMatch)
                    matchScore += 40;
                else if (normalizedResourceMatch)
                    matchScore += 30;

                if (profileMatch)
                    matchScore += 20;
                if (zoneMatch)
                    matchScore += 10;

                if (matchScore > bestScore) {
                    bestScore = matchScore;
                    bestRank = resultIndex + 1;
                    bestResourceName = demandResult.activeResource.name;
                    bestResourceType = demandResult.activeResource.type;
                    bestProfile = demandResult.profileKey;
                    bestZones = demandResult.activeResource.zones;
                    bestResourceMatch = resourceMatch;
                    bestNormalizedResourceMatch = normalizedResourceMatch;
                    bestProfileMatch = profileMatch;
                    bestZoneMatch = zoneMatch;
                }
            }

            bool matchesTopOpportunity =
                bestResourceMatch && bestProfileMatch && bestZoneMatch;
            String assignmentCoverageStatus = "not_top_opportunity";

            if (matchesTopOpportunity) {
                alignmentAssignmentsExactTopMatch++;

                if (expired) {
                    assignmentCoverageStatus = "stale";
                    alignmentAssignmentsStale++;
                } else if (active) {
                    assignmentCoverageStatus = "covered";
                    alignmentAssignmentsCovered++;
                } else if (untrusted) {
                    assignmentCoverageStatus = "untrusted";
                    alignmentAssignmentsUntrusted++;
                } else if (assignment.status == "validated") {
                    assignmentCoverageStatus = "validated";
                    alignmentAssignmentsValidated++;
                } else {
                    assignmentCoverageStatus = "candidate";
                    alignmentAssignmentsCandidate++;
                }
            } else if (bestResourceMatch && bestProfileMatch) {
                assignmentCoverageStatus = "zone_mismatch";
                alignmentAssignmentsZoneMismatch++;
            } else if (bestResourceMatch) {
                assignmentCoverageStatus = "profile_mismatch";
                alignmentAssignmentsProfileMismatch++;
            } else if (bestNormalizedResourceMatch) {
                assignmentCoverageStatus = "key_mismatch";
                alignmentAssignmentsNormalizedKeyMismatch++;
            } else if (bestProfileMatch) {
                assignmentCoverageStatus = "resource_mismatch";
                alignmentAssignmentsResourceMismatch++;
            } else {
                alignmentAssignmentsNotTopOpportunity++;
            }

            JSONSerializationType assignmentRow = JSONSerializationType::object();
            assignmentRow["minerId"] = assignment.minerID;
	            assignmentRow["assignmentStatus"] =
	                expired ? timeoutReason : assignment.status;
	            assignmentRow["lifecycleStatus"] =
	                expired ? timeoutReason : assignment.status;
            assignmentRow["assignmentGenerationId"] =
                assignment.assignmentGenerationId;
            assignmentRow["targetHash"] = assignment.targetHash;
            assignmentRow["coverageStatus"] = assignmentCoverageStatus;
            assignmentRow["matchReason"] = getCoverageAlignmentMatchReason(
                bestResourceMatch,
                bestNormalizedResourceMatch,
                bestProfileMatch,
                bestZoneMatch,
                expired,
                active,
                untrusted,
                assignment.status);
            assignmentRow["matchesTopOpportunity"] = matchesTopOpportunity;
            assignmentRow["matchedTopOpportunityRank"] = bestRank;
            assignmentRow["matchedResourceName"] = bestResourceName;
            assignmentRow["matchedResourceType"] = bestResourceType;
            assignmentRow["matchedProfile"] = bestProfile;
            assignmentRow["matchedZones"] = bestZones;
            assignmentRow["assignmentResource"] = assignment.targetResourceName;
            assignmentRow["assignmentResourceType"] = assignment.targetResourceType;
            assignmentRow["assignmentZone"] = assignment.targetZoneName;
            assignmentRow["assignmentProfile"] = assignment.selectedProfileKey;
            assignmentRow["demandState"] = assignment.demandState;
            assignmentRow["pressureScore"] =
                Math::getPrecision(assignment.pressureScore, 1);
            assignmentRow["density"] = Math::getPrecision(assignment.targetDensity, 3);
            assignmentRow["densityTargetStatus"] = assignment.densityTargetStatus;
            assignmentRow["pathValidationStatus"] =
                assignment.pathValidationStatus;
            assignmentRow["pathTrustStatus"] =
                assignment.pathValidationTrustStatus;
            assignmentRow["latestValidationStatus"] =
                assignment.pathValidationStatus;
            assignmentRow["latestPathTrustStatus"] =
                assignment.pathValidationTrustStatus;
            assignmentRow["activationSnapshotId"] =
                assignment.activationSnapshotId;
            assignmentRow["activationValidationStatus"] =
                assignment.activationPathValidationStatus;
            assignmentRow["activationPathTrustStatus"] =
                assignment.activationPathTrustStatus;
            assignmentRow["latestValidationSnapshotId"] =
                assignment.latestValidationSnapshotId;
            assignmentRow["validatedSnapshotId"] =
                assignment.validatedSnapshotId;
            assignmentRow["validationMismatchReason"] =
                assignment.latestValidationMismatchReason;
            assignmentRow["lastActivationResult"] =
                assignment.lastActivationResult;
            assignmentRow["lastFailureReason"] = assignment.lastFailureReason;
            assignmentRow["ageSeconds"] =
                assignment.createdAtMs > 0 && nowMs > assignment.createdAtMs ?
                (nowMs - assignment.createdAtMs) / 1000 : 0;
	            assignmentRow["remainingSeconds"] =
	                timeoutSeconds > timeoutAgeSeconds ?
	                timeoutSeconds - timeoutAgeSeconds : 0;
            coverageAlignmentAssignmentRows.push_back(assignmentRow);
        }
    }

    JSONSerializationType resourceCoverage = JSONSerializationType::object();
    resourceCoverage["readOnly"] = true;
    resourceCoverage["mode"] = "read-only";
    resourceCoverage["status"] = coverageRows.size() > 0 ?
        String("ready") : String("no_data");
    resourceCoverage["topOpportunities"] = coverageRows.size();
    resourceCoverage["covered"] = coverageCovered;
    resourceCoverage["uncovered"] = coverageUncovered;
    resourceCoverage["blockedByPath"] = coverageBlockedByPath;
    resourceCoverage["blockedByDensity"] = coverageBlockedByDensity;
    resourceCoverage["wrongPlanet"] = coverageWrongPlanet;
    resourceCoverage["cooldown"] = coverageCooldown;
    resourceCoverage["capped"] = coverageCapped;
    resourceCoverage["assignedMiners"] = coverageAssignedMiners;
    resourceCoverage["activeMinerAssignments"] = coverageActiveMiners;
    resourceCoverage["highestUncoveredAvailable"] = hasHighestUncovered;
    resourceCoverage["highestUncovered"] = highestUncovered;
    resourceCoverage["opportunities"] = coverageRows;
    result["resourceCoverage"] = resourceCoverage;

    JSONSerializationType coverageAlignmentCounts =
        JSONSerializationType::object();
    coverageAlignmentCounts["topOpportunities"] =
        coverageAlignmentOpportunityRows.size();
    coverageAlignmentCounts["assignments"] =
        coverageAlignmentAssignmentRows.size();
    coverageAlignmentCounts["opportunitiesWithExactMatch"] =
        alignmentOpportunitiesWithExactMatch;
    coverageAlignmentCounts["opportunitiesWithActiveMatch"] =
        alignmentOpportunitiesWithActiveMatch;
    coverageAlignmentCounts["opportunitiesWithCandidateMatch"] =
        alignmentOpportunitiesWithCandidateMatch;
    coverageAlignmentCounts["opportunitiesWithValidatedMatch"] =
        alignmentOpportunitiesWithValidatedMatch;
    coverageAlignmentCounts["opportunitiesWithUntrustedMatch"] =
        alignmentOpportunitiesWithUntrustedMatch;
    coverageAlignmentCounts["opportunitiesWithStaleMatch"] =
        alignmentOpportunitiesWithStaleMatch;
    coverageAlignmentCounts["opportunitiesWithProfileMismatch"] =
        alignmentOpportunitiesWithProfileMismatch;
    coverageAlignmentCounts["opportunitiesWithResourceMismatch"] =
        alignmentOpportunitiesWithResourceMismatch;
    coverageAlignmentCounts["opportunitiesWithZoneMismatch"] =
        alignmentOpportunitiesWithZoneMismatch;
    coverageAlignmentCounts["opportunitiesWithNormalizedKeyMismatch"] =
        alignmentOpportunitiesWithNormalizedKeyMismatch;
    coverageAlignmentCounts["opportunitiesWithoutActiveLocalMiner"] =
        alignmentOpportunitiesWithoutActiveLocalMiner;
    coverageAlignmentCounts["opportunitiesWithoutConfiguredSpawnZone"] =
        alignmentOpportunitiesWithoutConfiguredSpawnZone;
    coverageAlignmentCounts["opportunitiesTravelRequiredUnsupported"] =
        alignmentOpportunitiesTravelRequiredUnsupported;
    coverageAlignmentCounts["assignmentsExactTopMatch"] =
        alignmentAssignmentsExactTopMatch;
    coverageAlignmentCounts["assignmentsCovered"] =
        alignmentAssignmentsCovered;
    coverageAlignmentCounts["assignmentsCandidate"] =
        alignmentAssignmentsCandidate;
    coverageAlignmentCounts["assignmentsValidated"] =
        alignmentAssignmentsValidated;
    coverageAlignmentCounts["assignmentsUntrusted"] =
        alignmentAssignmentsUntrusted;
    coverageAlignmentCounts["assignmentsStale"] =
        alignmentAssignmentsStale;
    coverageAlignmentCounts["assignmentsNotTopOpportunity"] =
        alignmentAssignmentsNotTopOpportunity;
    coverageAlignmentCounts["assignmentsProfileMismatch"] =
        alignmentAssignmentsProfileMismatch;
    coverageAlignmentCounts["assignmentsResourceMismatch"] =
        alignmentAssignmentsResourceMismatch;
    coverageAlignmentCounts["assignmentsZoneMismatch"] =
        alignmentAssignmentsZoneMismatch;
    coverageAlignmentCounts["assignmentsNormalizedKeyMismatch"] =
        alignmentAssignmentsNormalizedKeyMismatch;

    JSONSerializationType coverageAlignmentDiagnostics =
        JSONSerializationType::object();
    coverageAlignmentDiagnostics["enabled"] = true;
    coverageAlignmentDiagnostics["readOnly"] = true;
    coverageAlignmentDiagnostics["mode"] = "read-only";
    coverageAlignmentDiagnostics["status"] =
        coverageAlignmentOpportunityRows.size() > 0 ||
        coverageAlignmentAssignmentRows.size() > 0 ?
        String("ready") : String("no_data");
    coverageAlignmentDiagnostics["summary"] =
        "Explains how top resource opportunities and intelligent assignments line up without changing behavior.";
    coverageAlignmentDiagnostics["behaviorChanged"] = false;
    coverageAlignmentDiagnostics["persistenceChanged"] = false;
    coverageAlignmentDiagnostics["realResourceCreated"] = false;
    coverageAlignmentDiagnostics["resourceContainerCreated"] = false;
    coverageAlignmentDiagnostics["inventoryMutated"] = false;
    coverageAlignmentDiagnostics["economyMutated"] = false;
    coverageAlignmentDiagnostics["activeMinerZones"] =
        joinCoverageZones(activeMinerZones);
    coverageAlignmentDiagnostics["configuredMinerSpawnZones"] =
        joinCoverageZones(configuredMinerSpawnZones);
    coverageAlignmentDiagnostics["travelSupported"] = false;
    coverageAlignmentDiagnostics["samePlanetRequired"] =
        minerIntelligentTargetingLimitedRequireSamePlanet;
    coverageAlignmentDiagnostics["counts"] = coverageAlignmentCounts;
    coverageAlignmentDiagnostics["opportunities"] =
        coverageAlignmentOpportunityRows;
    coverageAlignmentDiagnostics["assignments"] =
        coverageAlignmentAssignmentRows;
    result["coverageAlignmentDiagnostics"] = coverageAlignmentDiagnostics;

    JSONSerializationType pathDiagnosticRows = JSONSerializationType::array();
    int pathDiagnosticRowsTotal = 0;
    int pathDiagnosticRowsTruncated = 0;
    int pathDiagnosticCandidateAssignments = 0;
    int pathDiagnosticFailedValidations = 0;
    int pathDiagnosticDirectFallbackUnverified = 0;
    int pathDiagnosticStaleValidations = 0;
    int pathDiagnosticTargetMismatches = 0;
    int pathDiagnosticDensityTargetMismatches = 0;
    int pathDiagnosticPathTooLong = 0;
    int pathDiagnosticExceedsMaxPathDistance = 0;
    int pathDiagnosticTooManyPathNodes = 0;
    int pathDiagnosticNoPath = 0;
    int pathDiagnosticPathException = 0;
    int pathDiagnosticMinerNotInNavmesh = 0;
    int pathDiagnosticTargetOutsideNavmesh = 0;
    int pathDiagnosticBadTerrainOrHeight = 0;
    int pathDiagnosticUnknownPathFailures = 0;
    int pathDiagnosticVerifiedPaths = 0;
    const int maxPathDiagnosticRows = 32;
    uint64 pathValidationFreshnessWindowMs =
        static_cast<uint64>(
            (minerPathValidationSimulationIntervalSeconds > 0 ?
                minerPathValidationSimulationIntervalSeconds : 300) * 3) * 1000;

	    for (int i = 0; i < dashboardAssignmentSnapshots.size(); ++i) {
	        MinerIntelligentTargetAssignment assignment =
	            dashboardAssignmentSnapshots.get(i);
	        uint64 timeoutAgeSeconds = 0;
	        uint64 timeoutSeconds = 0;
	        String timeoutReason =
	            getMinerIntelligentAssignmentTimeoutReason(
	                assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, false);
	        bool expired = !timeoutReason.isEmpty();
	        String assignmentStatus = expired ? timeoutReason : assignment.status;

        if (assignmentStatus == "candidate")
            pathDiagnosticCandidateAssignments++;

        MinerPathValidationSnapshot snapshot;
        bool snapshotAvailable =
            getMinerPathValidationSnapshot(assignment.minerID, snapshot);
        if (snapshotAvailable && snapshot.targetHash.isEmpty())
            snapshot.targetHash = buildMinerAssignmentTargetHash(snapshot);
        uint64 validationAgeSeconds =
            snapshotAvailable && snapshot.recordedAtMs > 0 &&
            nowMs > snapshot.recordedAtMs ?
            (nowMs - snapshot.recordedAtMs) / 1000 : 0;
        uint64 assignmentAgeSeconds =
            assignment.createdAtMs > 0 && nowMs > assignment.createdAtMs ?
            (nowMs - assignment.createdAtMs) / 1000 : 0;
        bool validationStale =
            snapshotAvailable && snapshot.recordedAtMs > 0 &&
            nowMs > snapshot.recordedAtMs + pathValidationFreshnessWindowMs;
        float coordinateMismatchDistance = 0.f;

        if (snapshotAvailable) {
            float dx = assignment.targetX - snapshot.targetX;
            float dy = assignment.targetY - snapshot.targetY;
            float dz = assignment.targetZ - snapshot.targetZ;
            coordinateMismatchDistance = Math::sqrt(dx * dx + dy * dy + dz * dz);
        }
        bool validationMatchesAssignment = snapshotAvailable &&
            minerValidationSnapshotMatchesAssignment(assignment, snapshot);
        bool validationMatchesActivation =
            snapshotAvailable &&
            assignment.activationSnapshotId > 0 &&
            assignment.activationSnapshotId == snapshot.validationSnapshotId &&
            assignment.activationTargetHash == assignment.targetHash;
        String mismatchReason = assignment.latestValidationMismatchReason.isEmpty() ?
            String("none") : assignment.latestValidationMismatchReason;

        String explanationKey = getPathValidationDiagnosticKey(
            assignment,
            snapshotAvailable,
            snapshot,
            coordinateMismatchDistance,
            validationStale);

        bool directFallbackUnverified =
            (snapshotAvailable && snapshot.directFallback) ||
            (snapshotAvailable &&
                (snapshot.rejectReason == "directFallbackUnverified" ||
                 snapshot.pathTrustStatus == "directFallbackUnverified")) ||
            assignment.pathValidationTrustStatus == "directFallbackUnverified";
        bool targetOutsideNavmesh =
            snapshotAvailable && snapshot.targetNavmeshChecked &&
            !snapshot.targetInNavmesh;
        bool minerNotInNavmesh =
            snapshotAvailable && snapshot.minerInNavmeshKnown &&
            !snapshot.minerInNavmesh;
        bool badTerrainOrHeight =
            snapshotAvailable && snapshot.targetTerrainHeightKnown &&
            snapshot.targetZDelta * snapshot.targetZDelta > 9.f;

        if (assignment.pathValidationStatus == "failed")
            pathDiagnosticFailedValidations++;
        if (directFallbackUnverified)
            pathDiagnosticDirectFallbackUnverified++;
        if (explanationKey == "stale")
            pathDiagnosticStaleValidations++;
        if (explanationKey == "target_mismatch" ||
                assignment.pathValidationStatus == "target_mismatch")
            pathDiagnosticTargetMismatches++;
        if (explanationKey == "density_target_coordinate_mismatch")
            pathDiagnosticDensityTargetMismatches++;
        if (explanationKey == "path_too_long")
            pathDiagnosticPathTooLong++;
        if (explanationKey == "exceeds_max_path_distance")
            pathDiagnosticExceedsMaxPathDistance++;
        if (explanationKey == "too_many_path_nodes")
            pathDiagnosticTooManyPathNodes++;
        if (explanationKey == "no_path")
            pathDiagnosticNoPath++;
        if (explanationKey == "path_exception")
            pathDiagnosticPathException++;
        if (minerNotInNavmesh)
            pathDiagnosticMinerNotInNavmesh++;
        if (targetOutsideNavmesh)
            pathDiagnosticTargetOutsideNavmesh++;
        if (badTerrainOrHeight)
            pathDiagnosticBadTerrainOrHeight++;
        if (explanationKey == "unknown_path_failure")
            pathDiagnosticUnknownPathFailures++;
        if (explanationKey == "verified_path")
            pathDiagnosticVerifiedPaths++;

        pathDiagnosticRowsTotal++;

        if (pathDiagnosticRows.size() >= maxPathDiagnosticRows) {
            pathDiagnosticRowsTruncated++;
            continue;
        }

        bool minerPositionAvailable = false;
        float minerX = 0.f;
        float minerY = 0.f;
        float minerZ = 0.f;

        if (snapshotAvailable) {
            minerPositionAvailable = true;
            minerX = snapshot.minerX;
            minerY = snapshot.minerY;
            minerZ = snapshot.minerZ;
        } else if (activeMinerXById.contains(assignment.minerID)) {
            minerPositionAvailable = true;
            minerX = activeMinerXById.get(assignment.minerID);
            minerY = activeMinerYById.get(assignment.minerID);
            minerZ = activeMinerZById.get(assignment.minerID);
        }

        bool minerInNavmeshAvailable =
            (snapshotAvailable && snapshot.minerInNavmeshKnown) ||
            activeMinerNavmeshById.contains(assignment.minerID);
        bool minerInNavmesh =
            snapshotAvailable && snapshot.minerInNavmeshKnown ?
            snapshot.minerInNavmesh :
            (activeMinerNavmeshById.contains(assignment.minerID) ?
                activeMinerNavmeshById.get(assignment.minerID) != 0 : false);
        float straightLineDistance =
            snapshotAvailable ? snapshot.directDistance : 0.f;

        if (!snapshotAvailable && minerPositionAvailable) {
            float dx = assignment.targetX - minerX;
            float dy = assignment.targetY - minerY;
            float dz = assignment.targetZ - minerZ;
            straightLineDistance = Math::sqrt(dx * dx + dy * dy + dz * dz);
        }

        String rejectReason =
            snapshotAvailable ? snapshot.rejectReason :
            (assignment.pathValidationTrustStatus.isEmpty() ?
                String("none") : assignment.pathValidationTrustStatus);

        JSONSerializationType row = JSONSerializationType::object();
        row["minerId"] = assignment.minerID;
        row["assignmentStatus"] = assignmentStatus;
        row["lifecycleStatus"] = assignmentStatus;
        row["assignmentGenerationId"] = assignment.assignmentGenerationId;
        row["targetHash"] = assignment.targetHash;
        row["selectedProfile"] = assignment.selectedProfileKey;
        row["demandState"] = assignment.demandState;
        row["pressureScore"] = Math::getPrecision(assignment.pressureScore, 1);
        row["targetResource"] = assignment.targetResourceName;
        row["targetResourceType"] = assignment.targetResourceType;
        row["targetZone"] = assignment.targetZoneName;
        row["pathValidationStatus"] = assignment.pathValidationStatus;
        row["pathTrustStatus"] = assignment.pathValidationTrustStatus;
        row["currentPathValidationStatus"] =
            assignment.currentPathValidationStatus.isEmpty() ?
            assignment.pathValidationStatus :
            assignment.currentPathValidationStatus;
        row["currentPathTrustStatus"] =
            assignment.currentPathTrustStatus.isEmpty() ?
            assignment.pathValidationTrustStatus :
            assignment.currentPathTrustStatus;
        row["latestValidationStatus"] = assignment.pathValidationStatus;
        row["latestPathTrustStatus"] = assignment.pathValidationTrustStatus;
        row["validatedSnapshotId"] = assignment.validatedSnapshotId;
        row["validatedTargetHash"] = assignment.validatedTargetHash;
        row["validatedValidationStatus"] =
            assignment.validatedPathValidationStatus;
        row["validatedPathTrustStatus"] =
            assignment.validatedPathTrustStatus;
        row["activationSnapshotId"] = assignment.activationSnapshotId;
        row["activationTargetHash"] = assignment.activationTargetHash;
        row["activationValidationStatus"] =
            assignment.activationPathValidationStatus;
        row["activationPathTrustStatus"] =
            assignment.activationPathTrustStatus;
        row["latestValidationSnapshotId"] =
            assignment.latestValidationSnapshotId;
        row["latestValidationTargetHash"] =
            assignment.latestValidationTargetHash;
        row["validationSnapshotId"] =
            snapshotAvailable ? snapshot.validationSnapshotId : 0;
        row["latestValidationTargetHashFromSnapshot"] =
            snapshotAvailable ? snapshot.targetHash : String("");
        row["validationMatchesAssignment"] = validationMatchesAssignment;
        row["validationMatchesActivation"] = validationMatchesActivation;
        row["mismatchReason"] = mismatchReason;
        row["lifecycleDowngradePrevented"] =
            assignment.lifecycleDowngradePrevented;
        row["rejectReason"] = rejectReason;
        row["densityTargetStatus"] = assignment.densityTargetStatus;
        row["density"] = Math::getPrecision(assignment.targetDensity, 3);
        row["minerPositionAvailable"] = minerPositionAvailable;
        row["minerX"] = Math::getPrecision(minerX, 1);
        row["minerY"] = Math::getPrecision(minerY, 1);
        row["minerZ"] = Math::getPrecision(minerZ, 1);
        row["targetX"] = Math::getPrecision(assignment.targetX, 1);
        row["targetY"] = Math::getPrecision(assignment.targetY, 1);
        row["targetZ"] = Math::getPrecision(assignment.targetZ, 1);
        row["validationSnapshotAvailable"] = snapshotAvailable;
        row["validationTargetX"] =
            snapshotAvailable ? Math::getPrecision(snapshot.targetX, 1) : 0;
        row["validationTargetY"] =
            snapshotAvailable ? Math::getPrecision(snapshot.targetY, 1) : 0;
        row["validationTargetZ"] =
            snapshotAvailable ? Math::getPrecision(snapshot.targetZ, 1) : 0;
        row["straightLineDistance"] =
            Math::getPrecision(straightLineDistance, 1);
        row["pathDistance"] =
            snapshotAvailable ? Math::getPrecision(snapshot.pathDistance, 1) : 0;
        row["pathNodes"] = snapshotAvailable ? snapshot.pathNodes : 0;
        row["directFallback"] = directFallbackUnverified;
        row["minerInNavmeshAvailable"] = minerInNavmeshAvailable;
        row["minerInNavmesh"] = minerInNavmesh;
        row["targetNavmeshChecked"] =
            snapshotAvailable && snapshot.targetNavmeshChecked;
        row["targetInNavmeshAvailable"] =
            snapshotAvailable && snapshot.targetNavmeshChecked;
        row["targetInNavmesh"] =
            snapshotAvailable && snapshot.targetInNavmesh;
        row["targetTerrainHeightAvailable"] =
            snapshotAvailable && snapshot.targetTerrainHeightKnown;
        row["targetTerrainHeight"] =
            snapshotAvailable ?
            Math::getPrecision(snapshot.targetTerrainHeight, 1) : 0;
        row["zDelta"] =
            snapshotAvailable ? Math::getPrecision(snapshot.targetZDelta, 1) : 0;
        row["coordinateMismatchDistance"] =
            Math::getPrecision(coordinateMismatchDistance, 2);
        row["validationAgeSeconds"] = validationAgeSeconds;
        row["assignmentAgeSeconds"] = assignmentAgeSeconds;
        row["stale"] = validationStale;
        row["maxPathDistance"] =
            snapshotAvailable ? snapshot.maxPathDistance :
            minerPathValidationMaxPathDistance;
        row["maxPathNodes"] =
            snapshotAvailable ? snapshot.maxPathNodes :
            minerPathValidationMaxPathNodes;
        row["explanationKey"] = explanationKey;
        row["humanReason"] = getPathValidationHumanReason(explanationKey);
        row["recommendedAction"] =
            getPathValidationRecommendedAction(explanationKey);
        row["mode"] = "read-only";
        row["behaviorChanged"] = false;
        row["travelImplemented"] = false;
        pathDiagnosticRows.push_back(row);
    }

    JSONSerializationType pathValidationDiagnostics =
        JSONSerializationType::object();
    pathValidationDiagnostics["enabled"] = true;
    pathValidationDiagnostics["readOnly"] = true;
    pathValidationDiagnostics["mode"] = "read-only";
    pathValidationDiagnostics["status"] =
        pathDiagnosticRowsTotal == 0 ? String("no_data") :
        (pathDiagnosticFailedValidations > 0 ||
         pathDiagnosticDirectFallbackUnverified > 0 ||
         pathDiagnosticStaleValidations > 0 ||
         pathDiagnosticTargetMismatches > 0 ||
         pathDiagnosticUnknownPathFailures > 0 ?
            String("watch") : String("ready"));
    pathValidationDiagnostics["summary"] =
        "Explains candidate and assignment path validation blockers without relaxing path trust.";
    pathValidationDiagnostics["candidateAssignments"] =
        pathDiagnosticCandidateAssignments;
    pathValidationDiagnostics["failedValidations"] =
        pathDiagnosticFailedValidations;
    pathValidationDiagnostics["directFallbackUnverified"] =
        pathDiagnosticDirectFallbackUnverified;
    pathValidationDiagnostics["staleValidations"] =
        pathDiagnosticStaleValidations;
    pathValidationDiagnostics["targetMismatches"] =
        pathDiagnosticTargetMismatches;
    pathValidationDiagnostics["densityTargetCoordinateMismatches"] =
        pathDiagnosticDensityTargetMismatches;
    pathValidationDiagnostics["pathTooLong"] = pathDiagnosticPathTooLong;
    pathValidationDiagnostics["exceedsMaxPathDistance"] =
        pathDiagnosticExceedsMaxPathDistance;
    pathValidationDiagnostics["tooManyPathNodes"] =
        pathDiagnosticTooManyPathNodes;
    pathValidationDiagnostics["noPath"] = pathDiagnosticNoPath;
    pathValidationDiagnostics["pathException"] = pathDiagnosticPathException;
    pathValidationDiagnostics["minerNotInNavmesh"] =
        pathDiagnosticMinerNotInNavmesh;
    pathValidationDiagnostics["targetOutsideNavmesh"] =
        pathDiagnosticTargetOutsideNavmesh;
    pathValidationDiagnostics["badTerrainOrHeight"] =
        pathDiagnosticBadTerrainOrHeight;
    pathValidationDiagnostics["unknownPathFailures"] =
        pathDiagnosticUnknownPathFailures;
    pathValidationDiagnostics["verifiedPaths"] = pathDiagnosticVerifiedPaths;
    pathValidationDiagnostics["rowCount"] = pathDiagnosticRowsTotal;
    pathValidationDiagnostics["maxRows"] = maxPathDiagnosticRows;
    pathValidationDiagnostics["rowsTruncated"] = pathDiagnosticRowsTruncated;
    pathValidationDiagnostics["freshnessWindowSeconds"] =
        pathValidationFreshnessWindowMs / 1000;
    pathValidationDiagnostics["pathTrustRequired"] = "verifiedPath";
    pathValidationDiagnostics["pathTrustRelaxed"] = false;
    pathValidationDiagnostics["movementReadinessStatus"] =
        movementReadinessStatus;
    pathValidationDiagnostics["movementReadinessReason"] =
        movementReadinessReason;
    pathValidationDiagnostics["forceMovementReadinessPassedCount"] =
        forceMovementReadinessPassedCount;
    pathValidationDiagnostics["forceMovementBlockedCount"] =
        forceMovementBlockedCount;
    pathValidationDiagnostics["behaviorChanged"] = false;
    pathValidationDiagnostics["persistenceChanged"] = false;
    pathValidationDiagnostics["realResourceCreated"] = false;
    pathValidationDiagnostics["resourceContainerCreated"] = false;
    pathValidationDiagnostics["inventoryMutated"] = false;
    pathValidationDiagnostics["economyMutated"] = false;
    pathValidationDiagnostics["rows"] = pathDiagnosticRows;
    result["pathValidationDiagnostics"] = pathValidationDiagnostics;

    MinerReachabilityCalibrationBucket reachabilityTotals;
    VectorMap<String, MinerReachabilityCalibrationBucket> reachabilityByPlanet;
    VectorMap<String, MinerReachabilityCalibrationBucket> reachabilityByResourceClass;
    VectorMap<String, MinerReachabilityCalibrationBucket> reachabilityByDensitySource;
    VectorMap<String, MinerReachabilityCalibrationBucket> reachabilityByDistanceBand;
    VectorMap<String, MinerReachabilityValidationOutcome> reachabilityOutcomes;
    VectorMap<String, int> reachabilityFailures;

    {
        Locker reachabilityLocker(&minerReachabilityCalibrationMutex);
        reachabilityTotals = minerReachabilityTotals;
        reachabilityByPlanet = minerReachabilityByPlanet;
        reachabilityByResourceClass = minerReachabilityByResourceClass;
        reachabilityByDensitySource = minerReachabilityByDensitySource;
        reachabilityByDistanceBand = minerReachabilityByDistanceBand;
        reachabilityOutcomes = minerReachabilityValidationOutcomes;
        reachabilityFailures = minerReachabilityFailureReasons;
    }

    JSONSerializationType reachabilityCalibration =
        JSONSerializationType::object();
    reachabilityCalibration["enabled"] = true;
    reachabilityCalibration["readOnly"] = true;
    reachabilityCalibration["mode"] = "runtime-rolling-read-only";
    reachabilityCalibration["status"] =
        reachabilityTotals.candidatesGenerated > 0 ||
        reachabilityOutcomes.size() > 0 ?
        String("ready") : String("no_data");
    reachabilityCalibration["summary"] =
        "Tracks where density-selected miner candidates are lost before activation without changing validation or movement behavior.";
    reachabilityCalibration["behaviorChanged"] = false;
    reachabilityCalibration["validationRelaxed"] = false;
    reachabilityCalibration["movementChanged"] = false;
    reachabilityCalibration["persistenceChanged"] = false;
    reachabilityCalibration["realResourceCreated"] = false;
    reachabilityCalibration["resourceContainerCreated"] = false;
    reachabilityCalibration["inventoryMutated"] = false;
    reachabilityCalibration["economyMutated"] = false;
    reachabilityCalibration["validationFunnel"] =
        buildReachabilityFunnelJSON(reachabilityTotals);
    reachabilityCalibration["densityConversion"] =
        buildReachabilityDensityJSON(reachabilityTotals);
    reachabilityCalibration["validationOutcomes"] =
        buildReachabilityOutcomeRowsJSON(reachabilityOutcomes);
    reachabilityCalibration["byPlanet"] =
        buildReachabilityBucketRowsJSON(reachabilityByPlanet, "planet");
    reachabilityCalibration["byResourceClass"] =
        buildReachabilityBucketRowsJSON(
            reachabilityByResourceClass, "resourceClass");
    reachabilityCalibration["byDensitySource"] =
        buildReachabilityBucketRowsJSON(
            reachabilityByDensitySource, "densitySource");
    reachabilityCalibration["byDistanceBand"] =
        buildReachabilityBucketRowsJSON(
            reachabilityByDistanceBand, "distanceBand");
    reachabilityCalibration["topFailureReasons"] =
        buildReachabilityFailureRowsJSON(reachabilityFailures);
    result["reachabilityCalibration"] = reachabilityCalibration;

    result["reachabilityMemory"] =
        buildReachabilityMemoryJSON(
            reachabilityMemoryEnabled,
            reachabilityCandidatePreferenceEnabled,
            reachabilityMemoryTtlSeconds,
            reachabilityBucketSizeMeters,
            reachabilityMaxMemoryRows,
            reachabilityMinAttemptsBeforePenalty,
            reachabilityVerifiedPathScoreBonus,
            reachabilitySampleCompleteScoreBonus,
            reachabilityRepeatedFailurePenalty,
            reachabilityLongDistancePenalty512Plus,
            reachabilityPlanetPenaltyEnabled,
            reachabilityResourcePenaltyEnabled);

    result["navAreaDensitySelection"] =
        buildNavAreaDensitySelectionDiagnosticsJSON();

    JSONSerializationType travelPlanRows = JSONSerializationType::array();
    JSONSerializationType topRemoteOpportunity = JSONSerializationType::object();
    bool hasTopRemoteOpportunity = false;
    int remoteHighPriorityOpportunityCount = 0;
    int localHighPriorityOpportunityCount = 0;
    int resourceRushPlanCount = 0;
    int hubPlanCount = 0;
    int maxTravelPlans = aiTravelSimulationEnabled ?
        aiTravelSimulationMaxPlans : 0;

    if (aiTravelSimulationEnabled &&
            aiTravelSimulationIncludeResourceRushPlans) {
        for (int resultIndex = 0;
                resultIndex < demandResults.size() && resultIndex < coverageTopLimit;
                ++resultIndex) {
            DemandStateSimulationResult demandResult = demandResults.get(resultIndex);

            if (!demandResult.hasActiveOpportunity)
                continue;

            bool highPriority =
                demandResult.activeMatch.demandScore >= highValueDemandScore ||
                demandResult.pressureScore >=
                    demandWeightedMinerPlanSimulationMinimumPressureThreshold;

            if (!highPriority)
                continue;

            bool hasLocalMinerCoverage =
                resourceCoverageZonesContainAny(
                    activeMinerZones, demandResult.activeResource);
            bool hasConfiguredSpawnZone =
                resourceCoverageZonesContainAny(
                    configuredMinerSpawnZones, demandResult.activeResource);

            if (hasLocalMinerCoverage) {
                localHighPriorityOpportunityCount++;
                continue;
            }

            remoteHighPriorityOpportunityCount++;

            if (!hasTopRemoteOpportunity) {
                int localMinerCount = 0;

                for (int minerIndex = 0; minerIndex < activeMinerIds.size(); ++minerIndex) {
                    uint64 minerID = activeMinerIds.get(minerIndex);
                    String minerZone = activeMinerZoneById.contains(minerID) ?
                        activeMinerZoneById.get(minerID) : String("unknown");

                    if (resourceCoverageZoneContains(
                            minerZone, demandResult.activeResource))
                        localMinerCount++;
                }

                topRemoteOpportunity["resourceName"] =
                    demandResult.activeResource.name;
                topRemoteOpportunity["resourceType"] =
                    demandResult.activeResource.type;
                topRemoteOpportunity["zone"] =
                    getResourceScoutPlanet(demandResult.activeResource);
                topRemoteOpportunity["zones"] =
                    demandResult.activeResource.zones;
                topRemoteOpportunity["profile"] = demandResult.profileKey;
                topRemoteOpportunity["demandState"] = demandResult.state;
                topRemoteOpportunity["pressureScore"] =
                    Math::getPrecision(demandResult.pressureScore, 1);
                topRemoteOpportunity["demandScore"] =
                    demandResult.activeMatch.demandScore;
                topRemoteOpportunity["localMiners"] = localMinerCount;
                topRemoteOpportunity["configuredSpawnZone"] =
                    hasConfiguredSpawnZone;
                topRemoteOpportunity["travelRequired"] = true;
                topRemoteOpportunity["travelSupported"] = false;
                hasTopRemoteOpportunity = true;
            }

            for (int minerIndex = 0;
                    minerIndex < activeMinerIds.size() &&
                    travelPlanRows.size() < maxTravelPlans;
                    ++minerIndex) {
                uint64 minerID = activeMinerIds.get(minerIndex);
                String currentZone = activeMinerZoneById.contains(minerID) ?
                    activeMinerZoneById.get(minerID) : String("unknown");

                if (resourceCoverageZoneContains(
                        currentZone, demandResult.activeResource))
                    continue;

                JSONSerializationType plan = JSONSerializationType::object();
                plan["planType"] = "resource_rush";
                plan["minerId"] = minerID;
                plan["currentZone"] = currentZone;
                plan["targetZone"] =
                    getResourceScoutPlanet(demandResult.activeResource);
                plan["targetZones"] = demandResult.activeResource.zones;
                plan["targetResource"] = demandResult.activeResource.name;
                plan["targetResourceType"] =
                    demandResult.activeResource.type;
                plan["selectedProfile"] = demandResult.profileKey;
                plan["demandState"] = demandResult.state;
                plan["pressureScore"] =
                    Math::getPrecision(demandResult.pressureScore, 1);
                plan["demandScore"] = demandResult.activeMatch.demandScore;
                plan["configuredSpawnZone"] = hasConfiguredSpawnZone;
                plan["travelRequired"] = true;
                plan["travelSupported"] = false;
                plan["travelImplemented"] = false;
                plan["recommendedAction"] = "travel_when_supported";
                plan["reason"] = hasConfiguredSpawnZone ?
                    String("remote high-priority opportunity; no local miner coverage; travel not implemented") :
                    String("remote high-priority opportunity outside configured miner spawn zones; travel not implemented");
                plan["mode"] = "simulation-only";
                plan["behaviorChanged"] = false;
                travelPlanRows.push_back(plan);
                addIntCounter(populationRemotePlansFromZone, currentZone);
                resourceRushPlanCount++;
            }
        }
    }

    if (aiTravelSimulationEnabled &&
            aiTravelSimulationIncludeHubReturnPlans &&
            aiTravelSimulationHomeHubEnabled) {
        for (int minerIndex = 0;
                minerIndex < activeMinerIds.size() &&
                travelPlanRows.size() < maxTravelPlans;
                ++minerIndex) {
            uint64 minerID = activeMinerIds.get(minerIndex);
            String currentZone = activeMinerZoneById.contains(minerID) ?
                activeMinerZoneById.get(minerID) : String("unknown");

            if (currentZone == aiTravelSimulationHomeHubZone)
                continue;

            JSONSerializationType plan = JSONSerializationType::object();
            plan["planType"] = "hub_return";
            plan["minerId"] = minerID;
            plan["currentZone"] = currentZone;
            plan["targetZone"] = aiTravelSimulationHomeHubZone;
            plan["targetHub"] = aiTravelSimulationHomeHubKey;
            plan["targetCity"] = aiTravelSimulationHomeHubCity;
            plan["targetX"] = Math::getPrecision(aiTravelSimulationHomeHubX, 1);
            plan["targetY"] = Math::getPrecision(aiTravelSimulationHomeHubY, 1);
            plan["purpose"] = aiTravelSimulationHomeHubPurpose;
            plan["travelRequired"] = currentZone != aiTravelSimulationHomeHubZone;
            plan["travelSupported"] = false;
            plan["travelImplemented"] = false;
            plan["recommendedAction"] =
                "return_to_hub_when_selling_supported";
            plan["reason"] =
                "future resource-selling hub; no selling behavior implemented";
            plan["mode"] = "simulation-only";
            plan["behaviorChanged"] = false;
            travelPlanRows.push_back(plan);
            addIntCounter(
                populationHubPlansToZone, aiTravelSimulationHomeHubZone);
            hubPlanCount++;
        }
    }

    JSONSerializationType resourceRush = JSONSerializationType::object();
    resourceRush["active"] = aiTravelSimulationEnabled &&
        (remoteHighPriorityOpportunityCount > 0 ||
         localHighPriorityOpportunityCount > 0);
    resourceRush["mode"] = "simulation-only";
    resourceRush["readOnly"] = true;
    resourceRush["remoteHighPriorityCount"] =
        remoteHighPriorityOpportunityCount;
    resourceRush["localHighPriorityCount"] =
        localHighPriorityOpportunityCount;
    resourceRush["topRemoteOpportunityAvailable"] =
        hasTopRemoteOpportunity;
    resourceRush["topRemoteOpportunity"] = topRemoteOpportunity;
    resourceRush["travelImplemented"] = false;
    resourceRush["travelSupported"] = false;
    result["resourceRush"] = resourceRush;

    JSONSerializationType travelSimulation = JSONSerializationType::object();
    travelSimulation["enabled"] = aiTravelSimulationEnabled;
    travelSimulation["readOnly"] = true;
    travelSimulation["mode"] = "simulation-only";
    travelSimulation["status"] =
        aiTravelSimulationEnabled ? String("ready") : String("disabled");
    travelSimulation["travelImplemented"] = false;
    travelSimulation["travelSupported"] = false;
    travelSimulation["behaviorChanged"] = false;
    travelSimulation["persistenceChanged"] = false;
    travelSimulation["realResourceCreated"] = false;
    travelSimulation["resourceContainerCreated"] = false;
    travelSimulation["inventoryMutated"] = false;
    travelSimulation["economyMutated"] = false;
    travelSimulation["totalPlans"] = travelPlanRows.size();
    travelSimulation["maxPlans"] = aiTravelSimulationMaxPlans;
    travelSimulation["resourceRushPlanCount"] = resourceRushPlanCount;
    travelSimulation["remoteOpportunityCount"] =
        remoteHighPriorityOpportunityCount;
    travelSimulation["localOpportunityCount"] =
        localHighPriorityOpportunityCount;
    travelSimulation["hubPlanCount"] = hubPlanCount;
    travelSimulation["activeMinerZones"] =
        joinCoverageZones(activeMinerZones);
    travelSimulation["configuredMinerSpawnZones"] =
        joinCoverageZones(configuredMinerSpawnZones);
    travelSimulation["samePlanetRequired"] =
        minerIntelligentTargetingLimitedRequireSamePlanet;

    JSONSerializationType hub = JSONSerializationType::object();
    hub["enabled"] = aiTravelSimulationHomeHubEnabled;
    hub["key"] = aiTravelSimulationHomeHubKey;
    hub["zone"] = aiTravelSimulationHomeHubZone;
    hub["city"] = aiTravelSimulationHomeHubCity;
    hub["x"] = Math::getPrecision(aiTravelSimulationHomeHubX, 1);
    hub["y"] = Math::getPrecision(aiTravelSimulationHomeHubY, 1);
    hub["purpose"] = aiTravelSimulationHomeHubPurpose;
    hub["mode"] = "simulation-only";
    travelSimulation["homeHub"] = hub;
    travelSimulation["resourceRush"] = resourceRush;
    travelSimulation["plans"] = travelPlanRows;
    result["travelPlanSimulation"] = travelSimulation;

    int assignedMinerTotal = assignmentQueued + assignmentMoving +
        assignmentSampling + assignmentStationed + assignmentCandidate +
        assignmentValidated + assignmentFailed;
    int blockedMinerTotal = 0;

    for (int i = 0; i < populationBlockedByZone.size(); ++i)
        blockedMinerTotal += populationBlockedByZone.get(i);

    JSONSerializationType aiPopulationZones = JSONSerializationType::array();
    Vector<String> zoneLabels;

    for (int i = 0; i < populationTotalByZone.size(); ++i)
        addUniqueLabel(zoneLabels, populationTotalByZone.elementAt(i).getKey());
    for (int i = 0; i < configuredMinerSpawnZones.size(); ++i)
        addUniqueLabel(zoneLabels, configuredMinerSpawnZones.get(i));
    if (aiTravelSimulationHomeHubEnabled)
        addUniqueLabel(zoneLabels, aiTravelSimulationHomeHubZone);

    for (int i = 0; i < zoneLabels.size(); ++i) {
        String zone = zoneLabels.get(i);
        int zoneActiveMiners = populationMinersByZone.contains(zone) ?
            populationMinersByZone.get(zone) : 0;
        int zoneAssignedMiners = populationAssignedMinersByZone.contains(zone) ?
            populationAssignedMinersByZone.get(zone) : 0;
        int zoneIdleMiners = zoneActiveMiners > zoneAssignedMiners ?
            zoneActiveMiners - zoneAssignedMiners : 0;

        JSONSerializationType zoneRow = JSONSerializationType::object();
        zoneRow["zone"] = zone;
        zoneRow["total"] = populationTotalByZone.contains(zone) ?
            populationTotalByZone.get(zone) : 0;
        zoneRow["activeMiners"] = zoneActiveMiners;
        zoneRow["pvp"] = populationPvpByZone.contains(zone) ?
            populationPvpByZone.get(zone) : 0;
        zoneRow["assignedMiners"] = zoneAssignedMiners;
        zoneRow["candidateAssignments"] =
            populationCandidateByZone.contains(zone) ?
            populationCandidateByZone.get(zone) : 0;
        zoneRow["validatedAssignments"] =
            populationValidatedByZone.contains(zone) ?
            populationValidatedByZone.get(zone) : 0;
        zoneRow["sampling"] = populationSamplingByZone.contains(zone) ?
            populationSamplingByZone.get(zone) : 0;
        zoneRow["stationed"] = populationStationedByZone.contains(zone) ?
            populationStationedByZone.get(zone) : 0;
        zoneRow["moving"] = populationMovingByZone.contains(zone) ?
            populationMovingByZone.get(zone) : 0;
        zoneRow["idle"] = zoneIdleMiners;
        zoneRow["blocked"] = populationBlockedByZone.contains(zone) ?
            populationBlockedByZone.get(zone) : 0;
        zoneRow["remotePlansFromZone"] =
            populationRemotePlansFromZone.contains(zone) ?
            populationRemotePlansFromZone.get(zone) : 0;
        zoneRow["hubPlansToZone"] = populationHubPlansToZone.contains(zone) ?
            populationHubPlansToZone.get(zone) : 0;
        zoneRow["configuredMinerSpawnZone"] =
            configuredMinerSpawnZones.contains(zone);
        zoneRow["homeHub"] = aiTravelSimulationHomeHubEnabled &&
            zone == aiTravelSimulationHomeHubZone;
        aiPopulationZones.push_back(zoneRow);
    }

    JSONSerializationType aiPopulation = JSONSerializationType::object();
    aiPopulation["readOnly"] = true;
    aiPopulation["mode"] = "read-only";
    aiPopulation["status"] = controllerCount > 0 ?
        String("ready") : String("no_data");
    aiPopulation["total"] = controllerCount;
    aiPopulation["miners"] = activeMiners;
    aiPopulation["pvp"] = activePvpBots;
    aiPopulation["activeMiners"] = activeMiners;
    aiPopulation["assignedMiners"] = assignedMinerTotal;
    aiPopulation["validatedAssignments"] = assignmentValidated;
    aiPopulation["candidateAssignments"] = assignmentCandidate;
    aiPopulation["sampling"] = assignmentSampling;
    aiPopulation["stationed"] = assignmentStationed;
    aiPopulation["moving"] = assignmentMoving + assignmentQueued;
    aiPopulation["idle"] = activeMiners > activeMinerAssigned.size() ?
        activeMiners - activeMinerAssigned.size() : 0;
    aiPopulation["blocked"] = blockedMinerTotal;
    aiPopulation["travelPlanned"] = travelPlanRows.size();
    aiPopulation["travelSupported"] = false;
    aiPopulation["travelImplemented"] = false;
    aiPopulation["activeMinerZones"] =
        joinCoverageZones(activeMinerZones);
    aiPopulation["configuredMinerSpawnZones"] =
        joinCoverageZones(configuredMinerSpawnZones);
    aiPopulation["byZone"] = aiPopulationZones;
    result["aiPopulation"] = aiPopulation;

    JSONSerializationType scoutRisk = JSONSerializationType::object();
    scoutRisk["staleFindings"] = 0;
    scoutRisk["expiredFindings"] = 0;
    scoutRisk["noDensityTargets"] = noDensityTargets;
    scoutRisk["wrongPlanetOpportunities"] = 0;
    scoutRisk["highValueUnassigned"] = highValueUnassigned;
    scoutRisk["densityConfidence"] = noDensityTargets > 0 ?
        String("not_observed") : String("none");
    scoutRisk["highValueDemandScoreThreshold"] = highValueDemandScore;
    resourceScout["risk"] = scoutRisk;

    JSONSerializationType scoutBoundaries = JSONSerializationType::object();
    scoutBoundaries["publishesKnowledgeOnly"] = true;
    scoutBoundaries["realExtraction"] = false;
    scoutBoundaries["resourceContainerCreation"] = false;
    scoutBoundaries["inventoryMutation"] = false;
    scoutBoundaries["marketMutation"] = false;
    scoutBoundaries["persistenceWrites"] = false;
    resourceScout["boundaries"] = scoutBoundaries;

    result["resourceScout"] = resourceScout;

    int topResourceAwareProfileIndex = -1;
    uint64 topResourceAwareProfileQuantity = 0;

    for (int i = 0; i < resourceAwareQuantityByProfile.size(); ++i) {
        uint64 quantity = resourceAwareQuantityByProfile.get(i);

        if (quantity > topResourceAwareProfileQuantity) {
            topResourceAwareProfileQuantity = quantity;
            topResourceAwareProfileIndex = i;
        }
    }

    String topResourceAwareProfile =
        topResourceAwareProfileIndex >= 0 ?
        resourceAwareQuantityByProfile.elementAt(topResourceAwareProfileIndex).getKey() :
        String("none");
    int hardCoverageBlockers =
        coverageBlockedByPath + coverageBlockedByDensity + coverageWrongPlanet;
    bool activationUnhealthy =
        minerIntelligentTargetingLimitedEmergencyDisabled ||
        activationFailures > 0 ||
        healthPathFailures > 0;
    bool overFocusedProfile =
        resourceAwareTotalQuantity > 0 &&
        topResourceAwareProfileQuantity * 100 >= resourceAwareTotalQuantity * 80 &&
        coverageUncovered > 0;
    bool safetyViolation = false;
    bool auditNoData = coverageRows.size() == 0 &&
        recentYieldRows.size() == 0 &&
        resourceAwareRowCount == 0 &&
        demandRows.size() == 0;
    bool remoteTravelPending = remoteHighPriorityOpportunityCount > 0;
    bool pathDiagnosticsWatch =
        pathDiagnosticDirectFallbackUnverified > 0 ||
        pathDiagnosticStaleValidations > 0 ||
        pathDiagnosticTargetMismatches > 0 ||
        pathDiagnosticDensityTargetMismatches > 0 ||
        pathDiagnosticUnknownPathFailures > 0 ||
        pathDiagnosticMinerNotInNavmesh > 0 ||
        pathDiagnosticTargetOutsideNavmesh > 0;
    String auditStatus = "watch";
    String auditRecommendation = "do_not_change_behavior_yet";
    String auditSummary = "Audit waiting for more aligned coverage and yield data.";

    if (safetyViolation) {
        auditStatus = "unsafe";
        auditRecommendation = "do_not_change_behavior_yet";
        auditSummary =
            "Safety flags indicate unexpected real economy mutation; keep behavior stopped until investigated.";
    } else if (auditNoData) {
        auditStatus = "no_data";
        auditRecommendation = "do_not_change_behavior_yet";
        auditSummary =
            "No resource, coverage, demand, or yield data is available in the dashboard snapshot yet.";
    } else if (activationUnhealthy) {
        auditStatus = "blocked";
        auditRecommendation = "investigate_blockers";
        auditSummary =
            "Limited activation health shows emergency disablement, path failures, or activation failures.";
    } else if (pathDiagnosticsWatch) {
        auditStatus = "watch";
        auditRecommendation = "inspect_navmesh_or_target_coordinate";
        auditSummary =
            "Path diagnostics show candidate assignments blocked by validation trust or target checks.";
    } else if (coverageUncovered > 0 && hardCoverageBlockers >= coverageUncovered) {
        auditStatus = "blocked";
        auditRecommendation = "investigate_blockers";
        auditSummary =
            "Most uncovered opportunities are blocked by path, density, or planet constraints.";
    } else if (remoteTravelPending) {
        auditStatus = "watch";
        auditRecommendation = "enable_travel_or_add_local_miners_later";
        auditSummary =
            "Remote high-priority opportunities have simulation-only travel plans; current behavior should remain unchanged.";
    } else if (highValueUnassigned > 0 || coverageUncovered > 0) {
        auditStatus = "watch";
        auditRecommendation = "watch_uncovered_priority";
        auditSummary =
            "Some high-priority or top resource opportunities are still uncovered.";
    } else if (recentYieldRows.size() == 0 || resourceAwareRowCount == 0) {
        auditStatus = "watch";
        auditRecommendation = "do_not_change_behavior_yet";
        auditSummary =
            "Coverage exists, but recent intelligent yields or resource-aware stockpile rows are not present yet.";
    } else if (!stockpileInspectionReady || !stockpileSnapshot.persistenceReady) {
        auditStatus = "watch";
        auditRecommendation = "do_not_change_behavior_yet";
        auditSummary =
            "Stockpile inspection is not fully readable; keep observing before changing behavior.";
    } else if (overFocusedProfile) {
        auditStatus = "watch";
        auditRecommendation = "watch_uncovered_priority";
        auditSummary =
            "Recent resource-aware output is concentrated in one profile while other opportunities remain uncovered.";
    } else if (coverageCovered > 0 &&
            recentYieldRows.size() > 0 &&
            resourceAwareRowCount > 0) {
        auditStatus = "healthy";
        auditRecommendation = "keep_current";
        auditSummary =
            "Coverage, recent intelligent yield, resource-aware stockpile, and safety checks are aligned.";
    }

    JSONSerializationType profileAudit = JSONSerializationType::array();

    for (int i = 0; i < demandResults.size() && i < 8; ++i) {
        DemandStateSimulationResult demandResult = demandResults.get(i);
        String profileKey = demandResult.profileKey;
        int coveredForProfile =
            profileCoveredOpportunities.contains(profileKey) ?
            profileCoveredOpportunities.get(profileKey) : 0;
        int uncoveredForProfile =
            profileUncoveredOpportunities.contains(profileKey) ?
            profileUncoveredOpportunities.get(profileKey) : 0;
        uint64 recentQuantity =
            recentYieldQuantityByProfile.contains(profileKey) ?
            recentYieldQuantityByProfile.get(profileKey) : 0;
        int recentEvents =
            recentYieldCountByProfile.contains(profileKey) ?
            recentYieldCountByProfile.get(profileKey) : 0;
        uint64 resourceAwareQuantity =
            resourceAwareQuantityByProfile.contains(profileKey) ?
            resourceAwareQuantityByProfile.get(profileKey) : 0;

        if (!demandResult.hasActiveOpportunity &&
                coveredForProfile == 0 &&
                uncoveredForProfile == 0 &&
                recentQuantity == 0 &&
                resourceAwareQuantity == 0)
            continue;

        bool shortageState =
            demandResult.state == "critical" ||
            demandResult.state == "low";
        String profileStatus = "observed";
        String profileReason = "Profile has demand data but no immediate audit concern.";

        if (demandResult.state == "surplus" &&
                (recentQuantity > 0 || resourceAwareQuantity > 0)) {
            profileStatus = "watch_surplus_focus";
            profileReason =
                "Profile is surplus while recent intelligent output is still accumulating.";
        } else if (shortageState && uncoveredForProfile > 0 &&
                (recentQuantity > 0 || resourceAwareQuantity > 0)) {
            profileStatus = "needs_more_coverage";
            profileReason =
                "Profile has shortage pressure and recent matching yield, but still has uncovered opportunities.";
        } else if (shortageState && uncoveredForProfile > 0) {
            profileStatus = "needs_coverage";
            profileReason =
                "Profile has shortage pressure and uncovered active opportunities.";
        } else if (shortageState && coveredForProfile > 0 &&
                (recentQuantity > 0 || resourceAwareQuantity > 0)) {
            profileStatus = "aligned";
            profileReason =
                "Profile shortage has coverage and recent matching conceptual output.";
        } else if (coveredForProfile > 0 &&
                (recentQuantity > 0 || resourceAwareQuantity > 0)) {
            profileStatus = "aligned";
            profileReason =
                "Profile coverage and recent matching conceptual output are aligned.";
        } else if (coveredForProfile > 0) {
            profileStatus = "covered_waiting_for_yield";
            profileReason =
                "Profile has coverage, but no recent matching intelligent yield yet.";
        } else if (uncoveredForProfile > 0) {
            profileStatus = "uncovered";
            profileReason =
                "Profile has an active opportunity but no active miner coverage.";
        }

        JSONSerializationType row = JSONSerializationType::object();
        row["profile"] = profileKey;
        row["demandState"] = demandResult.state;
        row["pressureScore"] = Math::getPrecision(demandResult.pressureScore, 1);
        row["coveredOpportunities"] = coveredForProfile;
        row["uncoveredOpportunities"] = uncoveredForProfile;
        row["recentYieldQuantity"] = recentQuantity;
        row["recentYieldEvents"] = recentEvents;
        row["resourceAwareQuantity"] = resourceAwareQuantity;
        row["status"] = profileStatus;
        row["reason"] = profileReason;
        profileAudit.push_back(row);
    }

    JSONSerializationType blockerSummary = JSONSerializationType::object();
    blockerSummary["blockedByPath"] = coverageBlockedByPath;
    blockerSummary["blockedByDensity"] = coverageBlockedByDensity;
    blockerSummary["wrongPlanet"] = coverageWrongPlanet;
    blockerSummary["cooldown"] = coverageCooldown;
    blockerSummary["capped"] = coverageCapped;
    blockerSummary["remoteTravelPending"] = remoteHighPriorityOpportunityCount;
    blockerSummary["travelPlansPending"] = travelPlanRows.size();
    blockerSummary["travelSupported"] = false;
    blockerSummary["directFallbackUnverified"] =
        pathDiagnosticDirectFallbackUnverified;
    blockerSummary["stalePathValidations"] =
        pathDiagnosticStaleValidations;
    blockerSummary["targetMismatches"] = pathDiagnosticTargetMismatches;
    blockerSummary["densityTargetCoordinateMismatches"] =
        pathDiagnosticDensityTargetMismatches;
    blockerSummary["minerNotInNavmesh"] =
        pathDiagnosticMinerNotInNavmesh;
    blockerSummary["targetOutsideNavmesh"] =
        pathDiagnosticTargetOutsideNavmesh;
    blockerSummary["badTerrainOrHeight"] =
        pathDiagnosticBadTerrainOrHeight;
    blockerSummary["unknownPathFailures"] =
        pathDiagnosticUnknownPathFailures;
    blockerSummary["activationFailures"] = activationFailures;
    blockerSummary["pathFailed"] = healthPathFailures;
    blockerSummary["emergencyDisabled"] =
        minerIntelligentTargetingLimitedEmergencyDisabled;

    JSONSerializationType auditCounts = JSONSerializationType::object();
    auditCounts["topOpportunities"] = coverageRows.size();
    auditCounts["coveredOpportunities"] = coverageCovered;
    auditCounts["uncoveredOpportunities"] = coverageUncovered;
    auditCounts["blockedByPath"] = coverageBlockedByPath;
    auditCounts["blockedByDensity"] = coverageBlockedByDensity;
    auditCounts["wrongPlanet"] = coverageWrongPlanet;
    auditCounts["cooldown"] = coverageCooldown;
    auditCounts["capped"] = coverageCapped;
    auditCounts["recentIntelligentYieldCount"] = recentYieldRows.size();
    auditCounts["resourceAwareStockpileRows"] = resourceAwareRowCount;
    auditCounts["resourceAwareStockpileQuantity"] = resourceAwareTotalQuantity;
    auditCounts["activationFallbacks"] = activationFailures;
    auditCounts["pathFailed"] = healthPathFailures;
    auditCounts["emergencyDisabled"] =
        minerIntelligentTargetingLimitedEmergencyDisabled;
    auditCounts["persistenceReady"] = stockpileSnapshot.persistenceReady;
    auditCounts["stockpileRuntimeOnly"] = true;
    auditCounts["conceptualOnly"] = true;
    auditCounts["highValueUnassigned"] = highValueUnassigned;
    auditCounts["remoteHighPriorityOpportunities"] =
        remoteHighPriorityOpportunityCount;
    auditCounts["localHighPriorityOpportunities"] =
        localHighPriorityOpportunityCount;
    auditCounts["travelPlanCount"] = travelPlanRows.size();
    auditCounts["resourceRushPlanCount"] = resourceRushPlanCount;
    auditCounts["hubPlanCount"] = hubPlanCount;
    auditCounts["travelSupported"] = false;
    auditCounts["travelImplemented"] = false;
    auditCounts["pathDiagnosticRows"] = pathDiagnosticRowsTotal;
    auditCounts["candidateAssignments"] = pathDiagnosticCandidateAssignments;
    auditCounts["failedPathValidations"] = pathDiagnosticFailedValidations;
    auditCounts["directFallbackUnverified"] =
        pathDiagnosticDirectFallbackUnverified;
    auditCounts["stalePathValidations"] =
        pathDiagnosticStaleValidations;
    auditCounts["targetMismatches"] = pathDiagnosticTargetMismatches;
    auditCounts["densityTargetCoordinateMismatches"] =
        pathDiagnosticDensityTargetMismatches;
    auditCounts["minerNotInNavmesh"] =
        pathDiagnosticMinerNotInNavmesh;
    auditCounts["targetOutsideNavmesh"] =
        pathDiagnosticTargetOutsideNavmesh;
    auditCounts["topResourceAwareProfile"] = topResourceAwareProfile;
    auditCounts["topResourceAwareProfileQuantity"] =
        topResourceAwareProfileQuantity;

    JSONSerializationType economyDecisionAudit = JSONSerializationType::object();
    economyDecisionAudit["status"] = auditStatus;
    economyDecisionAudit["recommendation"] = auditRecommendation;
    economyDecisionAudit["summary"] = auditSummary;
    economyDecisionAudit["mode"] = "read-only";
    economyDecisionAudit["readOnly"] = true;
    economyDecisionAudit["behaviorChanged"] = false;
    economyDecisionAudit["persistenceChanged"] = false;
    economyDecisionAudit["economyMutated"] = false;
    economyDecisionAudit["realResourceCreated"] = false;
    economyDecisionAudit["resourceContainerCreated"] = false;
    economyDecisionAudit["inventoryMutated"] = false;
    economyDecisionAudit["marketMutated"] = false;
    economyDecisionAudit["conceptualOnly"] = true;
    economyDecisionAudit["counts"] = auditCounts;
    economyDecisionAudit["blockers"] = blockerSummary;
    economyDecisionAudit["profileAudit"] = profileAudit;
    result["economyDecisionAudit"] = economyDecisionAudit;

    JSONSerializationType safety = JSONSerializationType::object();
    safety["realResourceCreation"] = "no";
    safety["realResourceCreationEnabled"] = false;
    safety["resourceContainerCreation"] = "no";
    safety["resourceContainerCreationEnabled"] = false;
    safety["marketMutation"] = "no";
    safety["marketMutationEnabled"] = false;
    safety["inventoryMutation"] = "no";
    safety["inventoryMutationEnabled"] = false;
    result["safetyBoundaries"] = safety;

    return result;
}

void SimPlayerManager::logConceptualMinerSummary() {
    Vector<String> resourceNames;
    Vector<uint64> amounts;
    collectConceptualMinerTotals(resourceNames, amounts);

    int activeMiners = countActiveMiners();

    if (activeMiners == 0 && resourceNames.size() == 0)
        return;

    String message = "SimMiner totals: activeMiners=" + String::valueOf(activeMiners);

    if (resourceNames.size() == 0) {
        message += " totals=empty";
    } else {
        for (int i = 0; i < resourceNames.size(); ++i) {
            message += " " + resourceNames.get(i) + "=" + String::valueOf(amounts.get(i));
        }
    }

    info(message, true);
}

void SimPlayerManager::scheduleResourceIntelligenceTask() {
    if (!enabled || !resourceIntelligenceEnabled || resourceIntelligenceTaskScheduled)
        return;

    resourceIntelligenceTaskScheduled = true;

    Reference<ResourceIntelligenceTask*> task = new ResourceIntelligenceTask();
    task->schedule(resourceIntelligenceIntervalSeconds * 1000);
}

void SimPlayerManager::runResourceIntelligenceTask() {
    resourceIntelligenceTaskScheduled = false;

    if (!enabled || !resourceIntelligenceEnabled)
        return;

    logResourceIntelligenceSummary();
    scheduleResourceIntelligenceTask();
}

void SimPlayerManager::logResourceIntelligenceSummary() {
    Vector<ResourceIntelligenceEntry> entries;
    String errorMessage;

    if (!collectResourceIntelligenceSnapshot(entries, errorMessage)) {
        info(String("ResourceIntelligence: ") + errorMessage + "; skipping read-only snapshot", true);
        return;
    }

    calculateResourceIntelligenceScores(entries);

    info("ResourceIntelligence: read-only snapshot activeResources=" + String::valueOf(entries.size()) +
         " heuristicScores=true topLogging=" + String(resourceIntelligenceLogTopResources ? "true" : "false") +
         " curatedProfiles=" + String(resourceScoringProfilesEnabled ? "true" : "false"), true);

    if (resourceScoringProfilesEnabled) {
        Vector<ResourceScoringProfile> profiles = createCuratedResourceScoringProfiles();

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            ResourceScoringProfile profile = profiles.get(profileIndex);

            if (!configuredProfileKeyEnabled(resourceScoringProfileKeys, profile.key))
                continue;

            int bestIndex = -1;
            int bestScore = 0;
            String bestMatchedType;

            for (int i = 0; i < entries.size(); ++i) {
                ResourceIntelligenceEntry entry = entries.get(i);
                String matchedType = getBestMatchedResourceType(entry, profile);

                if (matchedType.isEmpty())
                    continue;

                int score = calculateProfileScore(entry, profile);

                if (score > bestScore) {
                    bestScore = score;
                    bestIndex = i;
                    bestMatchedType = matchedType;
                }
            }

            if (bestIndex >= 0 && bestScore > 0) {
                info(formatResourceScoringProfileLine(profile, entries.get(bestIndex), bestScore, bestMatchedType), true);
            } else {
                info(String("ResourceIntelligence profile ") + profile.key +
                     " category=" + profile.category +
                     " no eligible active resource matched requiredTypes=" +
                     formatProfileRequiredTypes(profile) +
                     " mode=log-only", true);
            }
        }
    }

    if (!resourceIntelligenceLogTopResources || entries.size() == 0)
        return;

    const char* labels[] = {"generic", "weaponsmith", "armorsmith", "chef", "architect"};

    for (int scoreFamily = 0; scoreFamily < 5; ++scoreFamily) {
        Vector<int> usedIndexes;
        int logged = 0;

        while (logged < resourceIntelligenceTopN) {
            int bestIndex = -1;
            int bestScore = 0;

            for (int i = 0; i < entries.size(); ++i) {
                if (resourceIntelligenceIndexUsed(usedIndexes, i))
                    continue;

                ResourceIntelligenceEntry entry = entries.get(i);

                if (!broadScoreFamilyAllowsResource(entry, scoreFamily))
                    continue;

                int score = getResourceIntelligenceScore(entry, scoreFamily);

                if (score > bestScore) {
                    bestScore = score;
                    bestIndex = i;
                }
            }

            if (bestIndex < 0 || bestScore <= 0)
                break;

            usedIndexes.add(bestIndex);
            ++logged;

            info(formatResourceIntelligenceLine(String(labels[scoreFamily]), entries.get(bestIndex), bestScore, logged), true);
        }
    }
}

void SimPlayerManager::scheduleDemandProfileSimulationTask() {
    if (!enabled || !demandProfileSimulationEnabled || demandProfileSimulationTaskScheduled)
        return;

    demandProfileSimulationTaskScheduled = true;

    Reference<DemandProfileSimulationTask*> task = new DemandProfileSimulationTask();
    task->schedule(demandProfileSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runDemandProfileSimulationTask() {
    demandProfileSimulationTaskScheduled = false;

    if (!enabled)
        return;

    refreshDemandProfileSimulationConfig();

    if (!demandProfileSimulationEnabled)
        return;

    logDemandProfileSimulations();
    scheduleDemandProfileSimulationTask();
}

void SimPlayerManager::refreshDemandProfileSimulationConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        info(String("DemandProfileSimulation configReloadFailed=true reason=\"") +
             e.getMessage() + "\" retainingPreviousConfig=true mode=log-only", true);
        return;
    }

    LuaObject managerConfig = configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable()) {
        info("DemandProfileSimulation configReloadFailed=true reason=missingManagerConfig retainingPreviousConfig=true mode=log-only", true);
        managerConfig.pop();
        return;
    }

    LuaObject demandConfig = managerConfig.getObjectField("demandProfileSimulationConfig");

    if (!demandConfig.isValidTable()) {
        info("DemandProfileSimulation configReloadFailed=true reason=missingDemandConfig retainingPreviousConfig=true mode=log-only", true);
        demandConfig.pop();
        managerConfig.pop();
        return;
    }

    demandProfileSimulationEnabled = demandConfig.getBooleanField(
        "enabled", demandProfileSimulationEnabled);
    demandProfileSimulationIntervalSeconds = clampMinerInt(
        demandConfig.getIntField("intervalSeconds"),
        demandProfileSimulationIntervalSeconds, 30, 3600);
    demandProfileSimulationLogTopN = clampMinerInt(
        demandConfig.getIntField("logTopN"),
        demandProfileSimulationLogTopN, 1, 20);

    String serverPhase = demandConfig.getStringField("serverPhase");

    if (serverPhase == "early_server" || serverPhase == "mature_server" ||
            serverPhase == "resource_rush" || serverPhase == "stockpile_phase") {
        demandProfileSimulationServerPhase = serverPhase;
    }

    const char* profileKeys[] = {
        "composite_armor_supply",
        "master_weaponsmith_staples",
        "high_damage_weapon_components",
        "chef_buff_foods",
        "chef_high_value_consumables",
        "production_infrastructure"
    };

    LuaObject profiles = demandConfig.getObjectField("profiles");
    if (profiles.isValidTable()) {
        for (int i = 0; i < 6; ++i) {
            String profileKey = profileKeys[i];
            LuaObject profile = profiles.getObjectField(profileKey);

            if (profile.isValidTable()) {
                int currentEnabled = demandProfileSimulationProfileEnabled.contains(profileKey) ?
                    demandProfileSimulationProfileEnabled.get(profileKey) : 1;
                float currentWeight = demandProfileSimulationProfileWeights.contains(profileKey) ?
                    demandProfileSimulationProfileWeights.get(profileKey) : 1.f;
                int currentPriority = demandProfileSimulationProfilePriorities.contains(profileKey) ?
                    demandProfileSimulationProfilePriorities.get(profileKey) : 0;

                demandProfileSimulationProfileEnabled.put(
                    profileKey,
                    profile.getBooleanField("enabled", currentEnabled != 0) ? 1 : 0);
                demandProfileSimulationProfileWeights.put(
                    profileKey,
                    clampFloatRange(profile.getFloatField("weight", currentWeight), 0.f, 10.f));
                demandProfileSimulationProfilePriorities.put(
                    profileKey,
                    clampIntRange(
                        static_cast<int>(profile.getFloatField(
                            "priority", static_cast<float>(currentPriority))),
                        0, 1000));
            }

            profile.pop();
        }
    }

    profiles.pop();
    demandConfig.pop();
    managerConfig.pop();
}

void SimPlayerManager::logDemandProfileSimulations() {
    Vector<ResourceIntelligenceEntry> entries;
    String errorMessage;

    if (!collectResourceIntelligenceSnapshot(entries, errorMessage)) {
        info(String("DemandProfileSimulation skipped=true reason=") + errorMessage +
             " mode=log-only", true);
        return;
    }

    if (entries.size() == 0) {
        info("DemandProfileSimulation skipped=true reason=noActiveResources mode=log-only", true);
        return;
    }

    Vector<DemandProfileDefinition> profiles = createDemandProfileDefinitions();

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        DemandProfileDefinition profile = profiles.get(profileIndex);
        bool profileEnabled = !demandProfileSimulationProfileEnabled.contains(profile.key) ||
            demandProfileSimulationProfileEnabled.get(profile.key) != 0;
        float weight = demandProfileSimulationProfileWeights.contains(profile.key) ?
            demandProfileSimulationProfileWeights.get(profile.key) : 1.f;
        int priority = demandProfileSimulationProfilePriorities.contains(profile.key) ?
            demandProfileSimulationProfilePriorities.get(profile.key) : 0;

        if (!profileEnabled || weight <= 0.f || priority <= 0) {
            info(String("DemandProfileSimulation profile=") + profile.key +
                 " phase=" + demandProfileSimulationServerPhase +
                 " skipped=true reason=disabledProfile mode=log-only", true);
            continue;
        }

        if (!demandProfileActiveForPhase(profile, demandProfileSimulationServerPhase)) {
            info(String("DemandProfileSimulation profile=") + profile.key +
                 " phase=" + demandProfileSimulationServerPhase +
                 " skipped=true reason=inactiveForServerPhase mode=log-only", true);
            continue;
        }

        Vector<int> usedIndexes;
        int logged = 0;

        while (logged < demandProfileSimulationLogTopN) {
            int bestIndex = -1;
            DemandProfileMatch bestMatch;

            for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                if (resourceIntelligenceIndexUsed(usedIndexes, entryIndex))
                    continue;

                ResourceIntelligenceEntry entry = entries.get(entryIndex);
                DemandProfileMatch match = evaluateDemandProfileResource(
                    entry, profile, weight, priority);

                if (!match.eligible || match.demandScore <= bestMatch.demandScore)
                    continue;

                bestIndex = entryIndex;
                bestMatch = match;
            }

            if (bestIndex < 0)
                break;

            usedIndexes.add(bestIndex);
            ++logged;

            info(formatDemandProfileSimulationLine(
                profile,
                entries.get(bestIndex),
                bestMatch,
                demandProfileSimulationServerPhase,
                weight,
                priority,
                logged), true);
        }

        if (logged == 0) {
            info(String("DemandProfileSimulation profile=") + profile.key +
                 " phase=" + demandProfileSimulationServerPhase +
                 " skipped=true reason=noEligibleActiveResource mode=log-only", true);
        }
    }
}

void SimPlayerManager::applyMarketSupplyObservationConfig(LuaObject& marketSupplyConfig) {
    marketSupplyObservationEnabled = marketSupplyConfig.getBooleanField(
        "enabled", marketSupplyObservationEnabled);
    marketSupplyObservationIntervalSeconds = clampMinerInt(
        marketSupplyConfig.getIntField("intervalSeconds"),
        marketSupplyObservationIntervalSeconds, 60, 3600);
    marketSupplyObservationMaxListingsScanned = clampMinerInt(
        marketSupplyConfig.getIntField("maxListingsScanned"),
        marketSupplyObservationMaxListingsScanned, 100, 50000);
    marketSupplyObservationIncludeBazaar = marketSupplyConfig.getBooleanField(
        "includeBazaar", marketSupplyObservationIncludeBazaar);
    marketSupplyObservationIncludePlayerVendors = marketSupplyConfig.getBooleanField(
        "includePlayerVendors", marketSupplyObservationIncludePlayerVendors);
    marketSupplyObservationIncludeVendorStockrooms = marketSupplyConfig.getBooleanField(
        "includeVendorStockrooms", marketSupplyObservationIncludeVendorStockrooms);
    marketSupplyObservationIncludePlayerInventory = marketSupplyConfig.getBooleanField(
        "includePlayerInventory", marketSupplyObservationIncludePlayerInventory);
    marketSupplyObservationIncludePrivateContainers = marketSupplyConfig.getBooleanField(
        "includePrivateContainers", marketSupplyObservationIncludePrivateContainers);
    marketSupplyObservationMinQuantity = clampMinerInt(
        marketSupplyConfig.getIntField("minQuantity"),
        marketSupplyObservationMinQuantity, 1, 100000000);
    marketSupplyObservationLogTopN = clampMinerInt(
        marketSupplyConfig.getIntField("logTopN"),
        marketSupplyObservationLogTopN, 1, 20);

    if (!marketSupplyObservationEnabled)
        clearMarketSupplyObservationSnapshot();
}

void SimPlayerManager::scheduleMarketSupplyObservationTask() {
    if (!enabled || !marketSupplyObservationEnabled || marketSupplyObservationTaskScheduled)
        return;

    marketSupplyObservationTaskScheduled = true;

    Reference<MarketSupplyObservationTask*> task = new MarketSupplyObservationTask();
    task->schedule(marketSupplyObservationIntervalSeconds * 1000);
}

void SimPlayerManager::runMarketSupplyObservationTask() {
    marketSupplyObservationTaskScheduled = false;

    if (!enabled) {
        clearMarketSupplyObservationSnapshot();
        return;
    }

    refreshMarketSupplyObservationConfig();

    if (!marketSupplyObservationEnabled) {
        clearMarketSupplyObservationSnapshot();
        return;
    }

    observeMarketSupply();
    scheduleMarketSupplyObservationTask();
}

void SimPlayerManager::refreshMarketSupplyObservationConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        info(String("MarketSupplyObservation configReloadFailed=true reason=\"") +
             e.getMessage() + "\" retainingPreviousConfig=true mode=read-only", true);
        return;
    }

    LuaObject managerConfig = configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable()) {
        info("MarketSupplyObservation configReloadFailed=true reason=missingManagerConfig retainingPreviousConfig=true mode=read-only", true);
        managerConfig.pop();
        return;
    }

    LuaObject marketSupplyConfig =
        managerConfig.getObjectField("marketSupplyObservationConfig");

    if (!marketSupplyConfig.isValidTable()) {
        info("MarketSupplyObservation configReloadFailed=true reason=missingMarketSupplyConfig retainingPreviousConfig=true mode=read-only", true);
        marketSupplyConfig.pop();
        managerConfig.pop();
        return;
    }

    applyMarketSupplyObservationConfig(marketSupplyConfig);

    marketSupplyConfig.pop();
    managerConfig.pop();
}

void SimPlayerManager::clearMarketSupplyObservationSnapshot() {
    Locker locker(&marketSupplyObservationMutex);

    marketSupplyObservationListingsScanned = 0;
    marketSupplyObservationResourceListings = 0;
    marketSupplyObservationTotalQuantity = 0;
    marketSupplyProfileQuantities.removeAll();
    marketSupplyProfileListings.removeAll();
    marketSupplyProfileCheapestPricePerUnit.removeAll();
    marketSupplyProfileMedianPricePerUnit.removeAll();
    marketSupplyProfileConfidence.removeAll();
    marketSupplyProfileTopResource.removeAll();
    marketSupplyProfileTopType.removeAll();
}

void SimPlayerManager::observeMarketSupply() {
    ZoneServer* zoneServer = ServerCore::getZoneServer();

    if (zoneServer == nullptr) {
        clearMarketSupplyObservationSnapshot();
        info("MarketSupplyObservation skipped=true reason=zoneServerUnavailable mode=read-only", true);
        return;
    }

    ManagedReference<AuctionManager*> auctionManager = zoneServer->getAuctionManager();

    if (auctionManager == nullptr) {
        clearMarketSupplyObservationSnapshot();
        info("MarketSupplyObservation skipped=true reason=auctionManagerUnavailable mode=read-only", true);
        return;
    }

    ManagedReference<AuctionsMap*> auctionsMap = auctionManager->getAuctionMap();

    if (auctionsMap == nullptr) {
        clearMarketSupplyObservationSnapshot();
        info("MarketSupplyObservation skipped=true reason=auctionsMapUnavailable mode=read-only", true);
        return;
    }

    Vector<MarketListingSnapshot> listingSnapshots;
    int listingsExamined = 0;

    if (marketSupplyObservationIncludeBazaar) {
        TerminalListVector bazaarLists = auctionsMap->getBazaarTerminalData("", "", nullptr);
        collectMarketListingSnapshots(
            bazaarLists, true, marketSupplyObservationMaxListingsScanned,
            listingsExamined, listingSnapshots);
    }

    if (marketSupplyObservationIncludePlayerVendors &&
            listingsExamined < marketSupplyObservationMaxListingsScanned) {
        TerminalListVector vendorLists = auctionsMap->getVendorTerminalData("", "", nullptr);
        collectMarketListingSnapshots(
            vendorLists, false, marketSupplyObservationMaxListingsScanned,
            listingsExamined, listingSnapshots);
    }

    Vector<MarketSupplyRow> rows;

    for (int listingIndex = 0; listingIndex < listingSnapshots.size(); ++listingIndex) {
        MarketListingSnapshot listing = listingSnapshots.get(listingIndex);
        Reference<SceneObject*> object = zoneServer->getObject(listing.objectID);
        Reference<ResourceContainer*> resource = object.castTo<ResourceContainer*>();

        if (resource == nullptr)
            continue;

        uint64 quantity = 0;
        ManagedReference<ResourceSpawn*> spawn;

        {
            Locker resourceLocker(resource);
            quantity = resource->getQuantity();
            spawn = resource->getSpawnObject();
        }

        if (quantity < static_cast<uint64>(marketSupplyObservationMinQuantity) ||
                spawn == nullptr) {
            continue;
        }

        MarketSupplyRow row;
        row.quantity = quantity;
        row.price = listing.price;
        row.pricePerUnit = listing.price >= 0 ?
            static_cast<float>(listing.price) / static_cast<float>(quantity) : -1.f;
        row.sourceType = listing.onBazaar ? "bazaar" : "player_vendor";
        row.ownerID = listing.ownerID;
        row.vendorID = listing.vendorID;

        {
            Locker spawnLocker(spawn);
            row.resource.objectID = spawn->getObjectID();
            row.resource.name = spawn->getName();
            row.resource.type = spawn->getType();
            row.resource.inShift = spawn->inShift();
            row.resource.despawned = spawn->getDespawned();
            row.resource.surveyToolType = spawn->getSurveyToolType();

            for (int classIndex = 0; classIndex < 12; ++classIndex) {
                String stfClass = spawn->getStfClass(classIndex);

                if (stfClass.isEmpty())
                    break;

                if (!row.resource.classChain.isEmpty())
                    row.resource.classChain += ">";

                row.resource.classChain += stfClass;
            }

            row.resource.oq = getResourceAttribute(spawn, "res_quality");
            row.resource.cd = getResourceAttribute(spawn, "res_conductivity");
            row.resource.dr = getResourceAttribute(spawn, "res_decay_resist");
            row.resource.hr = getResourceAttribute(spawn, "res_heat_resist");
            row.resource.fl = getResourceAttribute(spawn, "res_flavor");
            row.resource.ma = getResourceAttribute(spawn, "res_malleability");
            row.resource.pe = getResourceAttribute(spawn, "res_potential_energy");
            row.resource.sr = getResourceAttribute(spawn, "res_shock_resistance");
            row.resource.ut = getResourceAttribute(spawn, "res_toughness");
            row.resource.cr = getResourceAttribute(spawn, "res_cold_resist");
        }

        Reference<SceneObject*> vendor = zoneServer->getObject(listing.vendorID);
        Zone* vendorZone = vendor != nullptr ? vendor->getZone() : nullptr;

        if (vendorZone != nullptr)
            row.planet = vendorZone->getZoneName();

        row.resource.zones = row.planet;
        rows.add(row);
    }

    Vector<DemandProfileDefinition> profiles = createDemandProfileDefinitions();
    Vector<MarketProfileSupplyAggregate> aggregates;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        MarketProfileSupplyAggregate aggregate;
        aggregate.profileKey = profiles.get(profileIndex).key;
        aggregates.add(aggregate);
    }

    int matchedResourceListings = 0;
    uint64 matchedResourceQuantity = 0;

    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        MarketSupplyRow row = rows.get(rowIndex);
        bool matchedAnyProfile = false;

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            DemandProfileDefinition profile = profiles.get(profileIndex);
            DemandProfileMatch match =
                evaluateDemandProfileResource(row.resource, profile, 1.f, 100);

            if (!match.eligible)
                continue;

            MarketProfileSupplyAggregate* aggregate =
                findMarketProfileAggregate(aggregates, profile.key);

            if (aggregate == nullptr)
                continue;

            matchedAnyProfile = true;
            aggregate->quantity += row.quantity;
            aggregate->listings++;

            if (match.exact)
                aggregate->confidence = "exact_type";
            else if (aggregate->confidence == "none")
                aggregate->confidence = "coarse_family";

            if (row.pricePerUnit >= 0.f) {
                aggregate->pricesPerUnit.add(row.pricePerUnit);

                if (aggregate->cheapestPricePerUnit < 0.f ||
                        row.pricePerUnit < aggregate->cheapestPricePerUnit) {
                    aggregate->cheapestPricePerUnit = row.pricePerUnit;
                }
            }

            if (row.quantity > aggregate->topQuantity) {
                aggregate->topQuantity = row.quantity;
                aggregate->topResource = row.resource.name;
                aggregate->topType = row.resource.type;
            }
        }

        if (matchedAnyProfile) {
            matchedResourceListings++;
            matchedResourceQuantity += row.quantity;
        }
    }

    for (int aggregateIndex = 0; aggregateIndex < aggregates.size(); ++aggregateIndex) {
        MarketProfileSupplyAggregate& aggregate = aggregates.get(aggregateIndex);
        aggregate.medianPricePerUnit = calculateMedianPrice(aggregate.pricesPerUnit);
    }

    {
        Locker snapshotLocker(&marketSupplyObservationMutex);

        marketSupplyObservationListingsScanned = listingsExamined;
        marketSupplyObservationResourceListings = matchedResourceListings;
        marketSupplyObservationTotalQuantity = matchedResourceQuantity;
        marketSupplyProfileQuantities.removeAll();
        marketSupplyProfileListings.removeAll();
        marketSupplyProfileCheapestPricePerUnit.removeAll();
        marketSupplyProfileMedianPricePerUnit.removeAll();
        marketSupplyProfileConfidence.removeAll();
        marketSupplyProfileTopResource.removeAll();
        marketSupplyProfileTopType.removeAll();

        for (int aggregateIndex = 0; aggregateIndex < aggregates.size(); ++aggregateIndex) {
            MarketProfileSupplyAggregate aggregate = aggregates.get(aggregateIndex);
            marketSupplyProfileQuantities.put(aggregate.profileKey, aggregate.quantity);
            marketSupplyProfileListings.put(aggregate.profileKey, aggregate.listings);
            marketSupplyProfileCheapestPricePerUnit.put(
                aggregate.profileKey, aggregate.cheapestPricePerUnit);
            marketSupplyProfileMedianPricePerUnit.put(
                aggregate.profileKey, aggregate.medianPricePerUnit);
            marketSupplyProfileConfidence.put(aggregate.profileKey, aggregate.confidence);
            marketSupplyProfileTopResource.put(aggregate.profileKey, aggregate.topResource);
            marketSupplyProfileTopType.put(aggregate.profileKey, aggregate.topType);
        }
    }

    String deferredSources;

    if (marketSupplyObservationIncludeVendorStockrooms)
        deferredSources += "vendorStockrooms";
    if (marketSupplyObservationIncludePlayerInventory)
        deferredSources +=
            (deferredSources.isEmpty() ? String("") : String(",")) + "playerInventory";
    if (marketSupplyObservationIncludePrivateContainers)
        deferredSources +=
            (deferredSources.isEmpty() ? String("") : String(",")) + "privateContainers";

    info(String("MarketSupplyObservation enabled=true listingsScanned=") +
         String::valueOf(listingsExamined) +
         " resourceContainersObserved=" + String::valueOf(rows.size()) +
         " matchedResourceListings=" + String::valueOf(matchedResourceListings) +
         " totalQuantity=" + String::valueOf(matchedResourceQuantity) +
         " sources=" +
            ((marketSupplyObservationIncludeBazaar ? String("bazaar") : String("")) +
             (marketSupplyObservationIncludeBazaar &&
                      marketSupplyObservationIncludePlayerVendors ?
                  String(",") : String("")) +
             (marketSupplyObservationIncludePlayerVendors ?
                  String("player_vendor") : String(""))) +
         (deferredSources.isEmpty() ?
              String("") : String(" deferredSources=") + deferredSources) +
         " mode=read-only", true);

    for (int i = 0; i < aggregates.size(); ++i) {
        for (int j = i + 1; j < aggregates.size(); ++j) {
            if (aggregates.get(j).quantity <= aggregates.get(i).quantity)
                continue;

            MarketProfileSupplyAggregate swap = aggregates.get(i);
            aggregates.set(i, aggregates.get(j));
            aggregates.set(j, swap);
        }
    }

    int logged = 0;

    for (int aggregateIndex = 0;
            aggregateIndex < aggregates.size() &&
            logged < marketSupplyObservationLogTopN;
            ++aggregateIndex) {
        MarketProfileSupplyAggregate aggregate = aggregates.get(aggregateIndex);

        if (aggregate.listings <= 0)
            continue;

        String line = String("MarketSupplyObservation profile=") +
            aggregate.profileKey +
            " matchedListings=" + String::valueOf(aggregate.listings) +
            " matchedQuantity=" + String::valueOf(aggregate.quantity) +
            " confidence=" + aggregate.confidence +
            " topResource=" +
                (aggregate.topResource.isEmpty() ? String("unknown") : aggregate.topResource) +
            " type=" +
                (aggregate.topType.isEmpty() ? String("unknown") : aggregate.topType);

        if (aggregate.cheapestPricePerUnit >= 0.f) {
            line += " cheapestPPU=" +
                String::valueOf(Math::getPrecision(
                    aggregate.cheapestPricePerUnit, 3));
        } else {
            line += " cheapestPPU=unavailable";
        }

        if (aggregate.medianPricePerUnit >= 0.f) {
            line += " medianPPU=" +
                String::valueOf(Math::getPrecision(
                    aggregate.medianPricePerUnit, 3));
        } else {
            line += " medianPPU=unavailable";
        }

        line += " mode=read-only";
        info(line, true);
        logged++;
    }
}

void SimPlayerManager::applyStockpileSnapshotSimulationConfig(
        LuaObject& stockpileSnapshotConfig) {
    stockpileSnapshotSimulationEnabled = stockpileSnapshotConfig.getBooleanField(
        "enabled", stockpileSnapshotSimulationEnabled);
    stockpileSnapshotSimulationIntervalSeconds = clampMinerInt(
        stockpileSnapshotConfig.getIntField("intervalSeconds"),
        stockpileSnapshotSimulationIntervalSeconds, 30, 3600);
    stockpileSnapshotSimulationLogTopN = clampMinerInt(
        stockpileSnapshotConfig.getIntField("logTopN"),
        stockpileSnapshotSimulationLogTopN, 1, 20);
    stockpileSnapshotSimulationIncludeConceptualMinerTotals =
        stockpileSnapshotConfig.getBooleanField(
            "includeConceptualMinerTotals",
            stockpileSnapshotSimulationIncludeConceptualMinerTotals);
    stockpileSnapshotSimulationIncludeMarketObservation =
        stockpileSnapshotConfig.getBooleanField(
            "includeMarketObservation",
            stockpileSnapshotSimulationIncludeMarketObservation);
}

void SimPlayerManager::scheduleStockpileSnapshotSimulationTask() {
    if (!enabled || !stockpileSnapshotSimulationEnabled ||
            stockpileSnapshotSimulationTaskScheduled)
        return;

    stockpileSnapshotSimulationTaskScheduled = true;

    Reference<StockpileSnapshotSimulationTask*> task =
        new StockpileSnapshotSimulationTask();
    task->schedule(stockpileSnapshotSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runStockpileSnapshotSimulationTask() {
    stockpileSnapshotSimulationTaskScheduled = false;

    if (!enabled)
        return;

    refreshStockpileSnapshotSimulationConfig();

    if (!stockpileSnapshotSimulationEnabled)
        return;

    logStockpileSnapshotSimulation();
    scheduleStockpileSnapshotSimulationTask();
}

void SimPlayerManager::refreshStockpileSnapshotSimulationConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        info(String("StockpileSnapshotSimulation configReloadFailed=true reason=\"") +
             e.getMessage() +
             "\" retainingPreviousConfig=true mode=simulation-only", true);
        return;
    }

    LuaObject managerConfig = configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable()) {
        info("StockpileSnapshotSimulation configReloadFailed=true reason=missingManagerConfig retainingPreviousConfig=true mode=simulation-only", true);
        managerConfig.pop();
        return;
    }

    if (!managerConfig.getBooleanField("enabled", false)) {
        stockpileSnapshotSimulationEnabled = false;
        managerConfig.pop();
        return;
    }

    LuaObject stockpileSnapshotConfig =
        managerConfig.getObjectField("stockpileSnapshotSimulationConfig");

    if (!stockpileSnapshotConfig.isValidTable()) {
        info("StockpileSnapshotSimulation configReloadFailed=true reason=missingStockpileSnapshotConfig retainingPreviousConfig=true mode=simulation-only", true);
        stockpileSnapshotConfig.pop();
        managerConfig.pop();
        return;
    }

    applyStockpileSnapshotSimulationConfig(stockpileSnapshotConfig);

    stockpileSnapshotConfig.pop();
    managerConfig.pop();
}

void SimPlayerManager::logStockpileSnapshotSimulation() {
    Vector<StockpileSnapshotLot> ownedLots;
    uint64 totalOwnedQuantity = 0;

    if (stockpileSnapshotSimulationIncludeConceptualMinerTotals) {
        Vector<String> conceptualResourceNames;
        Vector<uint64> conceptualAmounts;
        collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

        for (int i = 0; i < conceptualResourceNames.size(); ++i) {
            String conceptualLabel = conceptualResourceNames.get(i);
            uint64 quantity = conceptualAmounts.get(i);

            if (conceptualLabel.isEmpty() || quantity == 0)
                continue;

            StockpileSnapshotLot lot;
            lot.conceptualLabel = conceptualLabel;
            lot.quantity = quantity;
            ownedLots.add(lot);
            totalOwnedQuantity += quantity;
        }
    }

    Vector<StockpileMarketReference> marketReferences;

    if (stockpileSnapshotSimulationIncludeMarketObservation) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int i = 0; i < marketSupplyProfileQuantities.size(); ++i) {
            String profileKey = marketSupplyProfileQuantities.elementAt(i).getKey();
            uint64 quantity = marketSupplyProfileQuantities.get(i);

            if (profileKey.isEmpty() || quantity == 0)
                continue;

            StockpileMarketReference reference;
            reference.profileKey = profileKey;
            reference.quantity = quantity;
            reference.listings = marketSupplyProfileListings.contains(profileKey) ?
                marketSupplyProfileListings.get(profileKey) : 0;
            reference.resourceName = marketSupplyProfileTopResource.contains(profileKey) ?
                marketSupplyProfileTopResource.get(profileKey) : "";
            reference.resourceType = marketSupplyProfileTopType.contains(profileKey) ?
                marketSupplyProfileTopType.get(profileKey) : "";
            reference.confidence = marketSupplyProfileConfidence.contains(profileKey) ?
                marketSupplyProfileConfidence.get(profileKey) : "none";
            marketReferences.add(reference);
        }
    }

    for (int i = 0; i < ownedLots.size(); ++i) {
        for (int j = i + 1; j < ownedLots.size(); ++j) {
            StockpileSnapshotLot left = ownedLots.get(i);
            StockpileSnapshotLot right = ownedLots.get(j);

            if (right.quantity < left.quantity ||
                    (right.quantity == left.quantity &&
                     right.conceptualLabel.compareTo(left.conceptualLabel) >= 0))
                continue;

            ownedLots.set(i, right);
            ownedLots.set(j, left);
        }
    }

    for (int i = 0; i < marketReferences.size(); ++i) {
        for (int j = i + 1; j < marketReferences.size(); ++j) {
            StockpileMarketReference left = marketReferences.get(i);
            StockpileMarketReference right = marketReferences.get(j);

            if (right.quantity < left.quantity ||
                    (right.quantity == left.quantity &&
                     right.profileKey.compareTo(left.profileKey) >= 0))
                continue;

            marketReferences.set(i, right);
            marketReferences.set(j, left);
        }
    }

    info(String("StockpileSnapshotSimulation enabled=true simulatedOwnedLots=") +
         String::valueOf(ownedLots.size()) +
         " totalOwnedQuantity=" + String::valueOf(totalOwnedQuantity) +
         " identityConfidence=conceptual_label ownerScope=galaxy" +
         " includeConceptualMinerTotals=" +
            (stockpileSnapshotSimulationIncludeConceptualMinerTotals ?
                String("true") : String("false")) +
         " includeMarketObservation=" +
            (stockpileSnapshotSimulationIncludeMarketObservation ?
                String("true") : String("false")) +
         " marketReferenceRows=" + String::valueOf(marketReferences.size()) +
         " persistentStockpileSupplyChanged=false mode=simulation-only", true);

    int loggedRows = 0;

    for (int i = 0; i < ownedLots.size() &&
            loggedRows < stockpileSnapshotSimulationLogTopN; ++i) {
        StockpileSnapshotLot lot = ownedLots.get(i);

        info(String("StockpileSnapshotSimulation lot=") +
             String::valueOf(i + 1) +
             " owned=true conceptualLabel=" + lot.conceptualLabel +
             " quantity=" + String::valueOf(lot.quantity) +
             " reservedQuantity=0 availableQuantity=" +
                String::valueOf(lot.quantity) +
             " acquisitionSource=conceptual_miner" +
             " resourceLifecycleState=conceptual ownerScope=galaxy" +
             " identityConfidence=conceptual_label persisted=false" +
             " mode=simulation-only", true);
        loggedRows++;
    }

    for (int i = 0; i < marketReferences.size() &&
            loggedRows < stockpileSnapshotSimulationLogTopN; ++i) {
        StockpileMarketReference reference = marketReferences.get(i);

        info(String("StockpileSnapshotSimulation marketReference=") +
             String::valueOf(i + 1) +
             " owned=false profile=" + reference.profileKey +
             " resource=" +
                (reference.resourceName.isEmpty() ?
                    String("unknown") : reference.resourceName) +
             " type=" +
                (reference.resourceType.isEmpty() ?
                    String("unknown") : reference.resourceType) +
             " quantity=" + String::valueOf(reference.quantity) +
             " listings=" + String::valueOf(reference.listings) +
             " identityConfidence=" + reference.confidence +
             " source=market_observation imported=false" +
             " mode=simulation-only", true);
        loggedRows++;
    }
}

void SimPlayerManager::applyAiEconomyPersistenceConfig(
        LuaObject& persistenceConfig) {
    aiEconomyPersistConceptualMinerTotals =
        persistenceConfig.getBooleanField(
            "persistConceptualMinerTotals",
            aiEconomyPersistConceptualMinerTotals);
    aiEconomyPersistenceIntervalSeconds = clampMinerInt(
        persistenceConfig.getIntField("intervalSeconds"),
        aiEconomyPersistenceIntervalSeconds, 60, 3600);
    aiEconomyPersistenceLogSummary = persistenceConfig.getBooleanField(
        "logSummary", aiEconomyPersistenceLogSummary);
}

void SimPlayerManager::applyPersistentStockpileDemandConfig(
        LuaObject& stockpileDemandConfig) {
    persistentStockpileDemandEnabled =
        stockpileDemandConfig.getBooleanField(
            "enabled", persistentStockpileDemandEnabled);
    persistentStockpileDemandIncludeConceptualMinerLots =
        stockpileDemandConfig.getBooleanField(
            "includeConceptualMinerLots",
            persistentStockpileDemandIncludeConceptualMinerLots);
    persistentStockpileDemandLogSummary =
        stockpileDemandConfig.getBooleanField(
            "logSummary", persistentStockpileDemandLogSummary);
}

void SimPlayerManager::scheduleAiEconomyPersistenceTask() {
    if (!enabled || !aiEconomyPersistConceptualMinerTotals ||
            aiEconomyPersistenceTaskScheduled)
        return;

    if (!AiEconomyManager::instance()->isPersistenceReady()) {
        if (!aiEconomyPersistenceFailureLogged) {
            warning("AiEconomyPersistenceConceptualTotals skipped=true reason=persistenceUnavailable mode=persisted-conceptual persistentStockpileSupplyChanged=false");
            aiEconomyPersistenceFailureLogged = true;
        }

        return;
    }

    aiEconomyPersistenceFailureLogged = false;
    aiEconomyPersistenceTaskScheduled = true;

    Reference<AiEconomyConceptualTotalsPersistenceTask*> task =
        new AiEconomyConceptualTotalsPersistenceTask();
    task->schedule(aiEconomyPersistenceIntervalSeconds * 1000);
}

void SimPlayerManager::runAiEconomyPersistenceTask() {
    aiEconomyPersistenceTaskScheduled = false;

    if (!enabled)
        return;

    refreshAiEconomyPersistenceConfig();

    if (!aiEconomyPersistConceptualMinerTotals)
        return;

    if (!AiEconomyManager::instance()->isPersistenceReady()) {
        if (!aiEconomyPersistenceFailureLogged) {
            warning("AiEconomyPersistenceConceptualTotals skipped=true reason=persistenceUnavailable mode=persisted-conceptual persistentStockpileSupplyChanged=false");
            aiEconomyPersistenceFailureLogged = true;
        }

        return;
    }

    Vector<String> resourceNames;
    Vector<uint64> amounts;
    collectConceptualMinerTotals(resourceNames, amounts);

    VectorMap<String, uint64> totalsSnapshot;
    int rejectedLabels = 0;

    for (int i = 0; i < resourceNames.size(); ++i) {
        String label = resourceNames.get(i);
        uint64 quantity = amounts.get(i);

        if (label.isEmpty() || label.length() > 128 || quantity == 0 ||
                quantity > 1000000000000ULL) {
            rejectedLabels++;
            continue;
        }

        totalsSnapshot.put(label, quantity);
    }

    if (rejectedLabels > 0) {
        warning(String("AiEconomyPersistenceConceptualTotals rejectedLabels=") +
            String::valueOf(rejectedLabels) +
            " reason=invalidLabelOrQuantity mode=persisted-conceptual persistentStockpileSupplyChanged=false");
    }

    if (totalsSnapshot.size() == 0) {
        scheduleAiEconomyPersistenceTask();
        return;
    }

    int createdLots = 0;
    int updatedLots = 0;
    uint64 totalQuantity = 0;
    String failureReason;
    bool updated = AiEconomyManager::instance()->updateConceptualMinerTotals(
        totalsSnapshot, createdLots, updatedLots, totalQuantity,
        failureReason);

    if (!updated) {
        warning(String("AiEconomyPersistenceConceptualTotals updated=false reason=\"") +
            failureReason +
            "\" mode=persisted-conceptual totalsImported=false persistentStockpileSupplyChanged=false");
        aiEconomyPersistenceFailureLogged = true;
        return;
    }

    aiEconomyPersistenceFailureLogged = false;

    if (aiEconomyPersistenceLogSummary) {
        info(String("AiEconomyPersistenceConceptualTotals updated=true labels=") +
            String::valueOf(totalsSnapshot.size()) +
            " createdLots=" + String::valueOf(createdLots) +
            " updatedLots=" + String::valueOf(updatedLots) +
            " totalQuantity=" + String::valueOf(totalQuantity) +
            " mode=persisted-conceptual totalsImported=true persistentStockpileSupplyChanged=false", true);
    }

    scheduleAiEconomyPersistenceTask();
}

void SimPlayerManager::refreshAiEconomyPersistenceConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        aiEconomyPersistConceptualMinerTotals = false;
        warning(String("AiEconomyPersistenceConceptualTotals configReloadFailed=true reason=\"") +
            e.getMessage() +
            "\" persistenceStopped=true mode=persisted-conceptual");
        return;
    }

    LuaObject managerConfig =
        configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable()) {
        aiEconomyPersistConceptualMinerTotals = false;
        warning("AiEconomyPersistenceConceptualTotals configReloadFailed=true reason=missingManagerConfig persistenceStopped=true mode=persisted-conceptual");
        managerConfig.pop();
        return;
    }

    if (!managerConfig.getBooleanField("enabled", false)) {
        aiEconomyPersistConceptualMinerTotals = false;
        managerConfig.pop();
        return;
    }

    LuaObject persistenceConfig =
        managerConfig.getObjectField("aiEconomyPersistenceConfig");

    if (!persistenceConfig.isValidTable()) {
        aiEconomyPersistConceptualMinerTotals = false;
        warning("AiEconomyPersistenceConceptualTotals configReloadFailed=true reason=missingPersistenceConfig persistenceStopped=true mode=persisted-conceptual");
        persistenceConfig.pop();
        managerConfig.pop();
        return;
    }

    applyAiEconomyPersistenceConfig(persistenceConfig);

    persistenceConfig.pop();
    managerConfig.pop();
}

void SimPlayerManager::applyDemandWeightedMinerPlanSimulationConfig(
        LuaObject& demandWeightedConfig) {
    demandWeightedMinerPlanSimulationEnabled =
        demandWeightedConfig.getBooleanField(
            "enabled", demandWeightedMinerPlanSimulationEnabled);
    demandWeightedMinerPlanSimulationIntervalSeconds = clampMinerInt(
        demandWeightedConfig.getIntField("intervalSeconds"),
        demandWeightedMinerPlanSimulationIntervalSeconds, 30, 3600);
    demandWeightedMinerPlanSimulationLogTopN = clampMinerInt(
        demandWeightedConfig.getIntField("logTopN"),
        demandWeightedMinerPlanSimulationLogTopN, 1, 100);
    demandWeightedMinerPlanSimulationSamePlanetBonus = clampIntRange(
        static_cast<int>(demandWeightedConfig.getFloatField(
            "samePlanetBonus",
            static_cast<float>(demandWeightedMinerPlanSimulationSamePlanetBonus))),
        0, 1000);
    demandWeightedMinerPlanSimulationTravelPenalty = clampIntRange(
        static_cast<int>(demandWeightedConfig.getFloatField(
            "travelPenalty",
            static_cast<float>(demandWeightedMinerPlanSimulationTravelPenalty))),
        0, 1000);
    demandWeightedMinerPlanSimulationMaxMinersPerProfile = clampMinerInt(
        demandWeightedConfig.getIntField("maxMinersPerProfile"),
        demandWeightedMinerPlanSimulationMaxMinersPerProfile, 1, 100);
    demandWeightedMinerPlanSimulationMinimumPressureThreshold = clampFloatRange(
        demandWeightedConfig.getFloatField(
            "minimumPressureThreshold",
            demandWeightedMinerPlanSimulationMinimumPressureThreshold),
        0.f, 1000000.f);
    demandWeightedMinerPlanSimulationStrongPressureRatio = clampFloatRange(
        demandWeightedConfig.getFloatField(
            "strongPressureRatio",
            demandWeightedMinerPlanSimulationStrongPressureRatio),
        1.f, 10.f);
}

void SimPlayerManager::applyDemandWeightedMinerPlanDependencyConfig(
        LuaObject& managerConfig) {
    LuaObject demandProfileConfig =
        managerConfig.getObjectField("demandProfileSimulationConfig");

    if (demandProfileConfig.isValidTable()) {
        String serverPhase = demandProfileConfig.getStringField("serverPhase");

        if (serverPhase == "early_server" || serverPhase == "mature_server" ||
                serverPhase == "resource_rush" ||
                serverPhase == "stockpile_phase") {
            demandWeightedMinerPlanSimulationServerPhase = serverPhase;
        }
    }

    demandProfileConfig.pop();

    LuaObject demandStateConfig =
        managerConfig.getObjectField("demandStateSimulationConfig");

    if (!demandStateConfig.isValidTable()) {
        demandStateConfig.pop();
        return;
    }

    demandWeightedMinerPlanSimulationActiveOpportunityWeight = clampFloatRange(
        demandStateConfig.getFloatField(
            "activeOpportunityWeight",
            demandWeightedMinerPlanSimulationActiveOpportunityWeight),
        0.f, 10.f);
    demandWeightedMinerPlanSimulationShortageWeight = clampFloatRange(
        demandStateConfig.getFloatField(
            "shortageWeight",
            demandWeightedMinerPlanSimulationShortageWeight),
        0.f, 10.f);
    demandWeightedMinerPlanSimulationSurplusDampening = clampFloatRange(
        demandStateConfig.getFloatField(
            "surplusDampening",
            demandWeightedMinerPlanSimulationSurplusDampening),
        0.f, 1.f);

    const char* profileKeys[] = {
        "composite_armor_supply",
        "master_weaponsmith_staples",
        "high_damage_weapon_components",
        "chef_buff_foods",
        "chef_high_value_consumables",
        "production_infrastructure"
    };

    LuaObject profiles = demandStateConfig.getObjectField("profiles");

    if (profiles.isValidTable()) {
        for (int i = 0; i < 6; ++i) {
            String profileKey = profileKeys[i];
            LuaObject profile = profiles.getObjectField(profileKey);

            if (profile.isValidTable()) {
                int currentEnabled =
                    demandWeightedMinerPlanSimulationProfileEnabled.contains(
                        profileKey) ?
                    demandWeightedMinerPlanSimulationProfileEnabled.get(
                        profileKey) : 1;
                int currentReserve =
                    demandWeightedMinerPlanSimulationDesiredReserve.contains(
                        profileKey) ?
                    demandWeightedMinerPlanSimulationDesiredReserve.get(
                        profileKey) : 0;
                float currentLow =
                    demandWeightedMinerPlanSimulationLowStockThreshold.contains(
                        profileKey) ?
                    demandWeightedMinerPlanSimulationLowStockThreshold.get(
                        profileKey) : 0.35f;
                float currentCritical =
                    demandWeightedMinerPlanSimulationCriticalStockThreshold.contains(
                        profileKey) ?
                    demandWeightedMinerPlanSimulationCriticalStockThreshold.get(
                        profileKey) : 0.10f;
                int desiredReserve = clampIntRange(
                    static_cast<int>(profile.getFloatField(
                        "desiredReserve", static_cast<float>(currentReserve))),
                    0, 100000000);
                float lowThreshold = clampFloatRange(
                    profile.getFloatField("lowStockThreshold", currentLow),
                    0.f, 1.f);
                float criticalThreshold = clampFloatRange(
                    profile.getFloatField(
                        "criticalStockThreshold", currentCritical),
                    0.f, 1.f);

                if (criticalThreshold > lowThreshold)
                    criticalThreshold = lowThreshold;

                demandWeightedMinerPlanSimulationProfileEnabled.put(
                    profileKey,
                    profile.getBooleanField(
                        "enabled", currentEnabled != 0) ? 1 : 0);
                demandWeightedMinerPlanSimulationDesiredReserve.put(
                    profileKey, desiredReserve);
                demandWeightedMinerPlanSimulationLowStockThreshold.put(
                    profileKey, lowThreshold);
                demandWeightedMinerPlanSimulationCriticalStockThreshold.put(
                    profileKey, criticalThreshold);
            }

            profile.pop();
        }
    }

    profiles.pop();
    demandStateConfig.pop();

    LuaObject marketSupplyConfig =
        managerConfig.getObjectField("marketSupplyObservationConfig");

    if (marketSupplyConfig.isValidTable()) {
        demandWeightedMinerPlanSimulationIncludeMarketSupply =
            marketSupplyConfig.getBooleanField(
                "enabled",
                demandWeightedMinerPlanSimulationIncludeMarketSupply);
    }

    marketSupplyConfig.pop();
}

void SimPlayerManager::applyAiTravelSimulationConfig(
        LuaObject& travelSimulationConfig) {
    aiTravelSimulationEnabled =
        travelSimulationConfig.getBooleanField(
            "enabled", aiTravelSimulationEnabled);
    aiTravelSimulationMaxPlans = clampMinerInt(
        travelSimulationConfig.getIntField("maxPlans"),
        aiTravelSimulationMaxPlans, 1, 100);
    aiTravelSimulationIncludeResourceRushPlans =
        travelSimulationConfig.getBooleanField(
            "includeResourceRushPlans",
            aiTravelSimulationIncludeResourceRushPlans);
    aiTravelSimulationIncludeHubReturnPlans =
        travelSimulationConfig.getBooleanField(
            "includeHubReturnPlans",
            aiTravelSimulationIncludeHubReturnPlans);

    LuaObject homeHub = travelSimulationConfig.getObjectField("homeHub");

    if (homeHub.isValidTable()) {
        aiTravelSimulationHomeHubEnabled =
            homeHub.getBooleanField(
                "enabled", aiTravelSimulationHomeHubEnabled);

        String key = homeHub.getStringField("key");
        String zone = homeHub.getStringField("zone");
        String city = homeHub.getStringField("city");
        String purpose = homeHub.getStringField("purpose");

        if (!key.isEmpty())
            aiTravelSimulationHomeHubKey = key;
        if (!zone.isEmpty())
            aiTravelSimulationHomeHubZone = zone;
        if (!city.isEmpty())
            aiTravelSimulationHomeHubCity = city;
        if (!purpose.isEmpty())
            aiTravelSimulationHomeHubPurpose = purpose;

        aiTravelSimulationHomeHubX =
            homeHub.getFloatField("x", aiTravelSimulationHomeHubX);
        aiTravelSimulationHomeHubY =
            homeHub.getFloatField("y", aiTravelSimulationHomeHubY);
    }

    homeHub.pop();
}

void SimPlayerManager::applyStationedMinerConfig(
        LuaObject& stationedConfig) {
    stationedMinerLifecycleEnabled =
        stationedConfig.getBooleanField(
            "enableStationedLifecycle",
            stationedMinerLifecycleEnabled);
    stationedMinerRepeatedSamplingEnabled =
        stationedConfig.getBooleanField(
            "enableStationedRepeatedSampling",
            stationedMinerRepeatedSamplingEnabled);
    stationedMinerSampleIntervalSeconds = clampMinerInt(
        stationedConfig.getIntField("stationedSampleIntervalSeconds"),
        stationedMinerSampleIntervalSeconds, 30, 7200);
    stationedMinerSampleJitterSeconds = clampMinerInt(
        stationedConfig.getIntField("stationedSampleJitterSeconds"),
        stationedMinerSampleJitterSeconds, 0, 3600);
    stationedMinerMaxSamplesPerAssignment = clampMinerInt(
        stationedConfig.getIntField("stationedMaxSamplesPerAssignment"),
        stationedMinerMaxSamplesPerAssignment, 1, 1000);
    stationedMinerMaxDurationSeconds = clampMinerInt(
        stationedConfig.getIntField("stationedMaxDurationSeconds"),
        stationedMinerMaxDurationSeconds, 60, 86400);
    stationedMinerRequireDemandStillValid =
        stationedConfig.getBooleanField(
            "stationedRequireDemandStillValid",
            stationedMinerRequireDemandStillValid);
    stationedMinerRequireResourceStillActive =
        stationedConfig.getBooleanField(
            "stationedRequireResourceStillActive",
            stationedMinerRequireResourceStillActive);
    stationedMinerRequireSamePlanet =
        stationedConfig.getBooleanField(
            "stationedRequireSamePlanet",
            stationedMinerRequireSamePlanet);
    stationedMinerClearWhenReserveSatisfied =
        stationedConfig.getBooleanField(
            "stationedClearWhenReserveSatisfied",
            stationedMinerClearWhenReserveSatisfied);
}

void SimPlayerManager::applyMinerIntelligentTargetingConfig(
        LuaObject& targetingConfig) {
    minerIntelligentTargetingEnabled =
        targetingConfig.getBooleanField(
            "enabled", minerIntelligentTargetingEnabled);
    minerIntelligentTargetingIntervalSeconds = clampMinerInt(
        targetingConfig.getIntField("intervalSeconds"),
        minerIntelligentTargetingIntervalSeconds, 30, 3600);
    minerIntelligentTargetingMaxActiveMiners = clampMinerInt(
        targetingConfig.getIntField("maxActiveMiners"),
        minerIntelligentTargetingMaxActiveMiners, 1, 100);
    minerIntelligentTargetingRequireDemandWeightedPlan =
        targetingConfig.getBooleanField(
            "requireDemandWeightedPlan",
            minerIntelligentTargetingRequireDemandWeightedPlan);
    minerIntelligentTargetingRequireAcceptedDensityTarget =
        targetingConfig.getBooleanField(
            "requireAcceptedDensityTarget",
            minerIntelligentTargetingRequireAcceptedDensityTarget);
    minerIntelligentTargetingRequireValidPath =
        targetingConfig.getBooleanField(
            "requireValidPath",
            minerIntelligentTargetingRequireValidPath);
    minerIntelligentTargetingFallbackToConceptualLoop =
        targetingConfig.getBooleanField(
            "fallbackToConceptualLoop",
            minerIntelligentTargetingFallbackToConceptualLoop);
    minerIntelligentTargetingRollbackOnFailureCount = clampMinerInt(
        targetingConfig.getIntField("rollbackOnFailureCount"),
        minerIntelligentTargetingRollbackOnFailureCount, 1, 100);
	minerIntelligentTargetingLogDecisionSummary =
		targetingConfig.getBooleanField(
			"logDecisionSummary",
			minerIntelligentTargetingLogDecisionSummary);
	minerIntelligentTargetingLogVerboseSwitchDecisions =
		targetingConfig.getBooleanField(
			"logVerboseSwitchDecisions",
			minerIntelligentTargetingLogVerboseSwitchDecisions);

    LuaObject assignmentConfig = targetingConfig.getObjectField("assignmentConfig");

    if (assignmentConfig.isValidTable()) {
        minerIntelligentTargetingAssignmentEnabled =
            assignmentConfig.getBooleanField(
                "enabled",
                minerIntelligentTargetingAssignmentEnabled);
        minerIntelligentTargetingAssignmentTtlSeconds = clampMinerInt(
            assignmentConfig.getIntField("ttlSeconds"),
            minerIntelligentTargetingAssignmentTtlSeconds, 5, 600);
        minerIntelligentTargetingCandidateAssignmentTtlSeconds = clampMinerInt(
            assignmentConfig.getIntField("candidateAssignmentTtlSeconds"),
            minerIntelligentTargetingAssignmentTtlSeconds, 5, 3600);
        minerIntelligentTargetingValidatedAssignmentTtlSeconds = clampMinerInt(
            assignmentConfig.getIntField("validatedAssignmentTtlSeconds"),
            minerIntelligentTargetingAssignmentTtlSeconds, 5, 3600);
        minerIntelligentTargetingQueuedActivationTtlSeconds = clampMinerInt(
            assignmentConfig.getIntField("queuedActivationTtlSeconds"),
            minerIntelligentTargetingQueuedActivationTtlSeconds, 30, 3600);
        minerIntelligentTargetingMovementArrivalTimeoutSeconds = clampMinerInt(
            assignmentConfig.getIntField("movementArrivalTimeoutSeconds"),
            minerIntelligentTargetingMovementArrivalTimeoutSeconds, 60, 7200);
        minerIntelligentTargetingMovementArrivalTimeoutMinSeconds = clampMinerInt(
            assignmentConfig.getIntField("movementArrivalTimeoutMinSeconds"),
            minerIntelligentTargetingMovementArrivalTimeoutMinSeconds, 60, 7200);
        minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds = clampMinerInt(
            assignmentConfig.getIntField("movementArrivalTimeoutMaxSeconds"),
            minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds, 60, 7200);
        float secondsPerMeter =
            assignmentConfig.getFloatField(
                "movementArrivalSecondsPerMeter",
                minerIntelligentTargetingMovementArrivalSecondsPerMeter);
        if (secondsPerMeter > 0.f)
            minerIntelligentTargetingMovementArrivalSecondsPerMeter =
                clampFloatRange(secondsPerMeter, 0.05f, 10.f);
        minerIntelligentTargetingSampleStartedTimeoutSeconds = clampMinerInt(
            assignmentConfig.getIntField("sampleStartedTimeoutSeconds"),
            minerIntelligentTargetingSampleStartedTimeoutSeconds, 30, 3600);
        minerIntelligentTargetingPreventNormalTtlForActiveMovement =
            assignmentConfig.getBooleanField(
                "preventNormalTtlForActiveMovement",
                minerIntelligentTargetingPreventNormalTtlForActiveMovement);
        if (minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds <
                minerIntelligentTargetingMovementArrivalTimeoutMinSeconds)
            minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds =
                minerIntelligentTargetingMovementArrivalTimeoutMinSeconds;
        minerIntelligentTargetingAssignmentReplaceOnlyWhenExpiredOrInvalid =
            assignmentConfig.getBooleanField(
                "replaceOnlyWhenExpiredOrInvalid",
                minerIntelligentTargetingAssignmentReplaceOnlyWhenExpiredOrInvalid);
        minerIntelligentTargetingAssignmentClearOnSampleComplete =
            assignmentConfig.getBooleanField(
                "clearOnSampleComplete",
                minerIntelligentTargetingAssignmentClearOnSampleComplete);
        minerIntelligentTargetingAssignmentClearOnCombat =
            assignmentConfig.getBooleanField(
                "clearOnCombat",
                minerIntelligentTargetingAssignmentClearOnCombat);
        minerIntelligentTargetingAssignmentClearOnIncapOrDeath =
            assignmentConfig.getBooleanField(
                "clearOnIncapOrDeath",
                minerIntelligentTargetingAssignmentClearOnIncapOrDeath);
        minerIntelligentTargetingAssignmentClearOnZoneChange =
            assignmentConfig.getBooleanField(
                "clearOnZoneChange",
                minerIntelligentTargetingAssignmentClearOnZoneChange);
		minerIntelligentTargetingAssignmentLogLifecycle =
			assignmentConfig.getBooleanField(
				"logAssignmentLifecycle",
				minerIntelligentTargetingAssignmentLogLifecycle);
		minerIntelligentTargetingAssignmentLogRetained =
			assignmentConfig.getBooleanField(
				"logRetainedAssignments",
				minerIntelligentTargetingAssignmentLogRetained);
        minerMovementReadinessDiagnosticsEnabled =
            assignmentConfig.getBooleanField(
                "movementReadinessDiagnosticsEnabled",
                minerMovementReadinessDiagnosticsEnabled);
	}

    assignmentConfig.pop();

    LuaObject limitedActivationConfig =
        targetingConfig.getObjectField("limitedActivationConfig");

    if (limitedActivationConfig.isValidTable()) {
        minerIntelligentTargetingLimitedActivationEnabled =
            limitedActivationConfig.getBooleanField(
                "enabled",
                minerIntelligentTargetingLimitedActivationEnabled);
        minerIntelligentTargetingLimitedMaxActivationsPerInterval = clampMinerInt(
            limitedActivationConfig.getIntField("maxActivationsPerInterval"),
            minerIntelligentTargetingLimitedMaxActivationsPerInterval, 1, 10);
        minerIntelligentTargetingLimitedMaxActiveIntelligentMiners = clampMinerInt(
            limitedActivationConfig.getIntField("maxActiveIntelligentMiners"),
            minerIntelligentTargetingLimitedMaxActiveIntelligentMiners, 1, 100);
        minerIntelligentTargetingLimitedCooldownSecondsPerMiner = clampMinerInt(
            limitedActivationConfig.getIntField("cooldownSecondsPerMiner"),
            minerIntelligentTargetingLimitedCooldownSecondsPerMiner, 0, 3600);
        minerIntelligentTargetingLimitedRequireSamePlanet =
            limitedActivationConfig.getBooleanField(
                "requireSamePlanet",
                minerIntelligentTargetingLimitedRequireSamePlanet);
        minerIntelligentTargetingLimitedDisableOnFirstFailure =
            limitedActivationConfig.getBooleanField(
                "disableOnFirstActivationFailure",
                minerIntelligentTargetingLimitedDisableOnFirstFailure);
        minerIntelligentTargetingLimitedDisableOnActivationFailure =
            limitedActivationConfig.getBooleanField(
                "disableOnActivationFailure",
                minerIntelligentTargetingLimitedDisableOnActivationFailure);
        minerIntelligentTargetingLimitedLogActivationLifecycle =
            limitedActivationConfig.getBooleanField(
                "logActivationLifecycle",
                minerIntelligentTargetingLimitedLogActivationLifecycle);
        minerIntelligentTargetingLimitedLogHealthSummary =
            limitedActivationConfig.getBooleanField(
                "logHealthSummary",
                minerIntelligentTargetingLimitedLogHealthSummary);

        LuaObject allowedZones = limitedActivationConfig.getObjectField("allowedZones");

        if (allowedZones.isValidTable()) {
            minerIntelligentTargetingLimitedAllowedZones.removeAll();
            int allowedZoneCount = allowedZones.getTableSize();

            for (int zoneIndex = 1; zoneIndex <= allowedZoneCount; ++zoneIndex) {
                String zoneName = allowedZones.getStringAt(zoneIndex);

                if (!zoneName.isEmpty())
                    minerIntelligentTargetingLimitedAllowedZones.add(zoneName);
            }
        }

        allowedZones.pop();
    }

    limitedActivationConfig.pop();

    String mode = targetingConfig.getStringField("mode");

	if (mode == "shadow" || mode == "limited") {
		minerIntelligentTargetingMode = mode;
	} else if (mode == "soak") {
		// Soak is an operator recipe for limited mode with conservative caps,
		// health summaries, cooldowns, and emergency-stop controls enabled.
		minerIntelligentTargetingMode = "limited";
	} else {
		minerIntelligentTargetingMode = "off";
		minerIntelligentTargetingEnabled = false;
	}

    if (minerIntelligentTargetingMode != "limited" ||
            !minerIntelligentTargetingLimitedActivationEnabled ||
            !minerIntelligentTargetingLimitedDisableOnActivationFailure) {
        minerIntelligentTargetingLimitedEmergencyDisabled = false;
    }

    if (minerIntelligentTargetingRequireValidPath) {
        int minimumUsefulTtl =
            minerIntelligentTargetingIntervalSeconds +
            minerPathValidationSimulationIntervalSeconds + 5;

        if (minerIntelligentTargetingAssignmentTtlSeconds < minimumUsefulTtl)
            minerIntelligentTargetingAssignmentTtlSeconds = minimumUsefulTtl;
        if (minerIntelligentTargetingCandidateAssignmentTtlSeconds < minimumUsefulTtl)
            minerIntelligentTargetingCandidateAssignmentTtlSeconds = minimumUsefulTtl;
        if (minerIntelligentTargetingValidatedAssignmentTtlSeconds < minimumUsefulTtl)
            minerIntelligentTargetingValidatedAssignmentTtlSeconds = minimumUsefulTtl;
    }
}

void SimPlayerManager::scheduleDemandWeightedMinerPlanSimulationTask() {
    if (!enabled || !demandWeightedMinerPlanSimulationEnabled ||
            demandWeightedMinerPlanSimulationTaskScheduled)
        return;

    demandWeightedMinerPlanSimulationTaskScheduled = true;

    Reference<DemandWeightedMinerPlanSimulationTask*> task =
        new DemandWeightedMinerPlanSimulationTask();
    task->schedule(demandWeightedMinerPlanSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runDemandWeightedMinerPlanSimulationTask() {
    demandWeightedMinerPlanSimulationTaskScheduled = false;

    if (!enabled)
        return;

    refreshDemandWeightedMinerPlanSimulationConfig();

    if (!demandWeightedMinerPlanSimulationEnabled)
        return;

    logDemandWeightedMinerPlanSimulations();
    scheduleDemandWeightedMinerPlanSimulationTask();
}

void SimPlayerManager::refreshDemandWeightedMinerPlanSimulationConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        info(String("DemandWeightedMinerPlanSimulation configReloadFailed=true reason=\"") +
             e.getMessage() +
             "\" retainingPreviousConfig=true mode=simulation-only", true);
        return;
    }

    LuaObject managerConfig = configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable()) {
        info("DemandWeightedMinerPlanSimulation configReloadFailed=true reason=missingManagerConfig retainingPreviousConfig=true mode=simulation-only", true);
        managerConfig.pop();
        return;
    }

    if (!managerConfig.getBooleanField("enabled", false)) {
        demandWeightedMinerPlanSimulationEnabled = false;
        managerConfig.pop();
        return;
    }

    LuaObject demandWeightedConfig =
        managerConfig.getObjectField("demandWeightedMinerPlanSimulationConfig");

    if (!demandWeightedConfig.isValidTable()) {
        info("DemandWeightedMinerPlanSimulation configReloadFailed=true reason=missingDemandWeightedConfig retainingPreviousConfig=true mode=simulation-only", true);
        demandWeightedConfig.pop();
        managerConfig.pop();
        return;
    }

    applyDemandWeightedMinerPlanSimulationConfig(demandWeightedConfig);
    applyDemandWeightedMinerPlanDependencyConfig(managerConfig);

    demandWeightedConfig.pop();
    managerConfig.pop();
}

void SimPlayerManager::logDemandWeightedMinerPlanSimulations() {
    Vector<DemandWeightedMinerSnapshot> miners;
    int controllerCount = controllers.size();

    for (int controllerIndex = 0; controllerIndex < controllerCount; ++controllerIndex) {
        uint64 controllerKey = controllers.getKey(controllerIndex);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr || dynamic_cast<SimMinerController*>(ctrl.get()) == nullptr)
            continue;

        ManagedReference<AiAgent*> agent = ctrl->getAgent();

        if (agent == nullptr)
            continue;

        DemandWeightedMinerSnapshot miner;
        miner.objectID = agent->getObjectID();
        Zone* zone = agent->getZone();
        miner.zoneName = zone != nullptr ? zone->getZoneName() : "unknown";
        miners.add(miner);
    }

    if (miners.size() == 0)
        return;

    for (int i = 0; i < miners.size(); ++i) {
        for (int j = i + 1; j < miners.size(); ++j) {
            if (miners.get(j).objectID >= miners.get(i).objectID)
                continue;

            DemandWeightedMinerSnapshot swap = miners.get(i);
            miners.set(i, miners.get(j));
            miners.set(j, swap);
        }
    }

    Vector<String> conceptualResourceNames;
    Vector<uint64> conceptualAmounts;
    collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

    Vector<ResourceIntelligenceEntry> entries;
    String snapshotError;

    if (!collectResourceIntelligenceSnapshot(entries, snapshotError)) {
        info(String("DemandWeightedMinerPlanSimulation skipped=true reason=\"") +
             snapshotError + "\" mode=simulation-only", true);
        return;
    }

    if (entries.size() == 0) {
        info("DemandWeightedMinerPlanSimulation skipped=true reason=noActiveResources mode=simulation-only", true);
        return;
    }

    Vector<DemandProfileDefinition> profiles = createDemandProfileDefinitions();
    VectorMap<String, uint64> marketQuantities;
    Vector<ResourceScoringProfile> d4Profiles =
        createCuratedResourceScoringProfiles();
    Vector<int> d4EnabledProfileIndexes;

    for (int profileIndex = 0; profileIndex < d4Profiles.size(); ++profileIndex) {
        ResourceScoringProfile profile = d4Profiles.get(profileIndex);
        float profileWeight = getMinerTargetSimulationProfileWeight(
            minerTargetSimulationProfileWeights, profile.key);

        if (profileWeight > 0.f)
            d4EnabledProfileIndexes.add(profileIndex);
    }

    if (demandWeightedMinerPlanSimulationIncludeMarketSupply) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            String profileKey = profiles.get(profileIndex).key;

            if (marketSupplyProfileQuantities.contains(profileKey))
                marketQuantities.put(
                    profileKey, marketSupplyProfileQuantities.get(profileKey));
        }
    }

    Vector<DemandStateSimulationResult> pressureResults;
    int profilesDisabled = 0;
    int profilesInactivePhase = 0;
    int profilesBelowPressure = 0;
    int profilesNoEligibleResource = 0;
    String profileRejectionSummary;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        DemandProfileDefinition profile = profiles.get(profileIndex);
        bool profileEnabled =
            !demandWeightedMinerPlanSimulationProfileEnabled.contains(profile.key) ||
            demandWeightedMinerPlanSimulationProfileEnabled.get(profile.key) != 0;

        if (!profileEnabled) {
            profilesDisabled++;
            profileRejectionSummary +=
                (profileRejectionSummary.isEmpty() ? String("") : String(",")) +
                profile.key + ":disabledProfile";
            continue;
        }

        if (!demandProfileActiveForPhase(
                profile, demandWeightedMinerPlanSimulationServerPhase)) {
            profilesInactivePhase++;
            profileRejectionSummary +=
                (profileRejectionSummary.isEmpty() ? String("") : String(",")) +
                profile.key + ":inactiveServerPhase";
            continue;
        }

        DemandStateSimulationResult result;
        result.profileKey = profile.key;
        result.desiredReserve =
            demandWeightedMinerPlanSimulationDesiredReserve.contains(profile.key) ?
            static_cast<uint64>(
                demandWeightedMinerPlanSimulationDesiredReserve.get(profile.key)) : 0;
        result.aiConceptualSupply = estimateConceptualDemandStateSupply(
            profile.key,
            conceptualResourceNames,
            conceptualAmounts,
            result.supplyConfidence,
            result.supplyLabels);
        result.marketObservedSupply = marketQuantities.contains(profile.key) ?
            marketQuantities.get(profile.key) : 0;
        float lowThreshold =
            demandWeightedMinerPlanSimulationLowStockThreshold.contains(profile.key) ?
            demandWeightedMinerPlanSimulationLowStockThreshold.get(profile.key) :
            0.35f;
        float criticalThreshold =
            demandWeightedMinerPlanSimulationCriticalStockThreshold.contains(
                profile.key) ?
            demandWeightedMinerPlanSimulationCriticalStockThreshold.get(
                profile.key) : 0.10f;

        for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            ResourceIntelligenceEntry entry = entries.get(entryIndex);
            DemandProfileMatch match =
                evaluateDemandProfileResource(entry, profile, 1.f, 100);

            if (!match.eligible ||
                    (result.hasActiveOpportunity &&
                     match.demandScore <= result.activeMatch.demandScore)) {
                continue;
            }

            result.hasActiveOpportunity = true;
            result.activeResource = entry;
            result.activeMatch = match;
        }

        calculateDemandStatePressure(
            result,
            lowThreshold,
            criticalThreshold,
            demandWeightedMinerPlanSimulationShortageWeight,
            demandWeightedMinerPlanSimulationActiveOpportunityWeight,
            demandWeightedMinerPlanSimulationSurplusDampening);

        if (!result.hasActiveOpportunity) {
            profilesNoEligibleResource++;
            profileRejectionSummary +=
                (profileRejectionSummary.isEmpty() ? String("") : String(",")) +
                profile.key + ":noEligibleActiveResource";
            continue;
        }

        if (result.pressureScore <
                demandWeightedMinerPlanSimulationMinimumPressureThreshold) {
            profilesBelowPressure++;
            profileRejectionSummary +=
                (profileRejectionSummary.isEmpty() ? String("") : String(",")) +
                profile.key + ":belowMinimumPressure";
            continue;
        }

        pressureResults.add(result);
    }

    if (pressureResults.size() == 0) {
        info("DemandWeightedMinerPlanSimulation skipped=true reason=noEligibleProfileAbovePressureThreshold mode=simulation-only", true);
    }

    VectorMap<String, int> assignmentsByProfile;
    VectorMap<String, int> cappedProfiles;
    int plansProduced = 0;
    int plansLogged = 0;
    int noPlanCount = 0;
    int overflowAssignments = 0;
    int comparisonSameProfiles = 0;
    int comparisonDifferentProfiles = 0;
    int comparisonSameTargets = 0;
    int comparisonDifferentTargets = 0;
    int comparisonNoD4Plan = 0;
    int comparisonNoDemandPlan = 0;

    for (int minerIndex = 0; minerIndex < miners.size(); ++minerIndex) {
        DemandWeightedMinerSnapshot miner = miners.get(minerIndex);
        int d4AssignedProfileIndex = -1;
        MinerTargetSimulationPlan d4Plan =
            selectAssignedMinerTargetSimulationPlan(
                entries,
                d4Profiles,
                d4EnabledProfileIndexes,
                minerTargetSimulationProfileWeights,
                minerIndex,
                miner.zoneName,
                minerTargetSimulationPreferSamePlanet,
                minerTargetSimulationSamePlanetBonus,
                minerTargetSimulationTravelPenalty,
                d4AssignedProfileIndex);
        DemandWeightedMinerCandidate bestWithinCap;
        DemandWeightedMinerCandidate strongestCapped;
        DemandWeightedMinerCandidate secondStrongestCapped;
        Vector<DemandWeightedMinerCandidate> evaluatedCandidates;

        for (int resultIndex = 0;
                resultIndex < pressureResults.size(); ++resultIndex) {
            DemandStateSimulationResult result = pressureResults.get(resultIndex);
            DemandProfileDefinition profile;

            if (!findDemandProfileDefinition(
                    profiles, result.profileKey, profile)) {
                continue;
            }

            DemandWeightedMinerTarget target =
                selectDemandWeightedMinerTarget(
                    entries,
                    profile,
                    miner.zoneName,
                    demandWeightedMinerPlanSimulationSamePlanetBonus,
                    demandWeightedMinerPlanSimulationTravelPenalty);

            if (!target.isValid())
                continue;

            int assignmentCount = assignmentsByProfile.contains(result.profileKey) ?
                assignmentsByProfile.get(result.profileKey) : 0;
            DemandWeightedMinerCandidate candidate;
            candidate.resultIndex = resultIndex;
            candidate.target = target;
            candidate.existingAssignments = assignmentCount;
            candidate.exceedsProfileCap =
                assignmentCount >=
                    demandWeightedMinerPlanSimulationMaxMinersPerProfile;

            if (candidate.exceedsProfileCap &&
                    !cappedProfiles.contains(result.profileKey)) {
                cappedProfiles.put(result.profileKey, 1);
            }

            candidate.locationAdjustedPressure = result.pressureScore +
                (target.samePlanet ?
                    static_cast<float>(
                        demandWeightedMinerPlanSimulationSamePlanetBonus) :
                    -static_cast<float>(
                        demandWeightedMinerPlanSimulationTravelPenalty));
            candidate.balancedScore =
                candidate.locationAdjustedPressure /
                static_cast<float>(assignmentCount + 1);
            evaluatedCandidates.add(candidate);

            if (!candidate.exceedsProfileCap &&
                    (!bestWithinCap.isValid() ||
                     candidate.balancedScore > bestWithinCap.balancedScore)) {
                bestWithinCap = candidate;
            }

            if (candidate.exceedsProfileCap &&
                    (!strongestCapped.isValid() ||
                     candidate.locationAdjustedPressure >
                        strongestCapped.locationAdjustedPressure)) {
                secondStrongestCapped = strongestCapped;
                strongestCapped = candidate;
            } else if (candidate.exceedsProfileCap &&
                    (!secondStrongestCapped.isValid() ||
                     candidate.locationAdjustedPressure >
                        secondStrongestCapped.locationAdjustedPressure)) {
                secondStrongestCapped = candidate;
            }
        }

        DemandWeightedMinerCandidate selected;
        bool strongPressureOverflow = false;

        if (bestWithinCap.isValid()) {
            selected = bestWithinCap;

            if (strongestCapped.isValid()) {
                DemandStateSimulationResult overflowResult =
                    pressureResults.get(strongestCapped.resultIndex);
                DemandStateSimulationResult boundedResult =
                    pressureResults.get(bestWithinCap.resultIndex);

                if (overflowResult.pressureScore >=
                        boundedResult.pressureScore *
                        demandWeightedMinerPlanSimulationStrongPressureRatio) {
                    selected = strongestCapped;
                    strongPressureOverflow = true;
                }
            }
        } else if (strongestCapped.isValid()) {
            DemandStateSimulationResult strongestResult =
                pressureResults.get(strongestCapped.resultIndex);
            bool overflowJustified = !secondStrongestCapped.isValid();

            if (secondStrongestCapped.isValid()) {
                DemandStateSimulationResult secondResult =
                    pressureResults.get(secondStrongestCapped.resultIndex);
                overflowJustified = strongestResult.pressureScore >=
                    secondResult.pressureScore *
                    demandWeightedMinerPlanSimulationStrongPressureRatio;
            }

            if (overflowJustified) {
                selected = strongestCapped;
                strongPressureOverflow = true;
            }
        }

        if (!selected.isValid()) {
            noPlanCount++;
            comparisonNoDemandPlan++;

            if (!d4Plan.isValid())
                comparisonNoD4Plan++;

            if (plansLogged < demandWeightedMinerPlanSimulationLogTopN) {
                String noPlanReason =
                    "allCandidatesCappedWithoutStrongPressure";

                if (evaluatedCandidates.size() == 0) {
                    if (profilesBelowPressure > 0) {
                        noPlanReason = "allProfilesBelowMinimumPressure";
                    } else if (profilesNoEligibleResource > 0) {
                        noPlanReason = "noEligibleActiveResource";
                    } else if (profilesInactivePhase > 0) {
                        noPlanReason = "inactiveServerPhase";
                    } else {
                        noPlanReason = "noEnabledDemandProfile";
                    }
                }

                String rejectedCandidates;

                for (int candidateIndex = 0;
                        candidateIndex < evaluatedCandidates.size();
                        ++candidateIndex) {
                    DemandWeightedMinerCandidate candidate =
                        evaluatedCandidates.get(candidateIndex);
                    DemandStateSimulationResult candidateResult =
                        pressureResults.get(candidate.resultIndex);

                    rejectedCandidates +=
                        (rejectedCandidates.isEmpty() ?
                            String("") : String(",")) +
                        candidateResult.profileKey +
                        (candidate.exceedsProfileCap ?
                            String(":cappedByMaxMinersPerProfile") :
                            String(":noSelection"));
                }

                info(String("DemandWeightedMinerPlanSimulation miner=") +
                     String::valueOf(miner.objectID) +
                     " zone=" + miner.zoneName +
                     " noEligiblePlan=true" +
                     " noPlanReason=" + noPlanReason +
                     " candidatesEvaluated=" +
                        String::valueOf(evaluatedCandidates.size()) +
                     (rejectedCandidates.isEmpty() ?
                        String("") :
                        String(" rejectedCandidates=\"") +
                            rejectedCandidates + "\"") +
                     " rawPressureScore=unavailable" +
                     " locationAdjustedResourceScore=unavailable" +
                     " adjustedPlanScore=unavailable" +
                     " selectionRank=0 mode=simulation-only", true);
                plansLogged++;
            }

            continue;
        }

        DemandStateSimulationResult selectedResult =
            pressureResults.get(selected.resultIndex);
        DemandProfileDefinition selectedProfile;
        findDemandProfileDefinition(
            profiles, selectedResult.profileKey, selectedProfile);
        ResourceIntelligenceEntry selectedResource =
            entries.get(selected.target.resourceIndex);
        int selectionRank = 1;
        int cappedCandidateCount = 0;
        int travelPenaltyCandidateCount = 0;
        int rejectedCandidateCount = 0;
        int lostHigherAdjustedScoreCount = 0;
        int lostStrongPressureOverflowCount = 0;
        String rejectedCandidates;

        for (int candidateIndex = 0;
                candidateIndex < evaluatedCandidates.size();
                ++candidateIndex) {
            DemandWeightedMinerCandidate candidate =
                evaluatedCandidates.get(candidateIndex);

            if (candidate.balancedScore >
                    selected.balancedScore) {
                selectionRank++;
            }

            if (candidate.exceedsProfileCap)
                cappedCandidateCount++;

            if (!candidate.target.samePlanet)
                travelPenaltyCandidateCount++;

            if (candidate.resultIndex == selected.resultIndex &&
                    candidate.target.resourceIndex ==
                        selected.target.resourceIndex) {
                continue;
            }

            rejectedCandidateCount++;
            DemandStateSimulationResult candidateResult =
                pressureResults.get(candidate.resultIndex);
            String rejectionReason;

            if (candidate.exceedsProfileCap) {
                rejectionReason = "cappedByMaxMinersPerProfile";
            } else if (strongPressureOverflow) {
                lostStrongPressureOverflowCount++;
                rejectionReason = "lostToStrongPressureOverflow";
            } else if (candidate.balancedScore < selected.balancedScore) {
                lostHigherAdjustedScoreCount++;
                rejectionReason = "lostToHigherAdjustedPlanScore";
            } else if (!candidate.target.samePlanet) {
                rejectionReason = "travelPenaltyAndLostToStableTieBreak";
            } else {
                rejectionReason = "lostToStableTieBreak";
            }

            if (!candidate.target.samePlanet &&
                    rejectionReason.indexOf("travelPenalty") < 0) {
                if (rejectionReason == "cappedByMaxMinersPerProfile") {
                    rejectionReason =
                        "travelPenaltyAndCappedByMaxMinersPerProfile";
                } else if (rejectionReason ==
                        "lostToStrongPressureOverflow") {
                    rejectionReason =
                        "travelPenaltyAndLostToStrongPressureOverflow";
                } else {
                    rejectionReason =
                        "travelPenaltyAndLostToHigherAdjustedPlanScore";
                }
            }

            rejectedCandidates +=
                (rejectedCandidates.isEmpty() ? String("") : String(",")) +
                candidateResult.profileKey + ":" + rejectionReason;
        }

        int newAssignmentCount = selected.existingAssignments + 1;
        assignmentsByProfile.put(
            selectedResult.profileKey, newAssignmentCount);
        plansProduced++;

        if (strongPressureOverflow)
            overflowAssignments++;

        if (!d4Plan.isValid()) {
            comparisonNoD4Plan++;
        } else {
            ResourceScoringProfile d4Profile =
                d4Profiles.get(d4Plan.profileIndex);
            ResourceIntelligenceEntry d4Resource =
                entries.get(d4Plan.resourceIndex);

            if (d4Profile.category == selectedProfile.category)
                comparisonSameProfiles++;
            else
                comparisonDifferentProfiles++;

            bool sameTargetResource =
                d4Resource.objectID != 0 && selectedResource.objectID != 0 ?
                d4Resource.objectID == selectedResource.objectID :
                d4Resource.name == selectedResource.name &&
                    d4Resource.type == selectedResource.type;

            if (sameTargetResource)
                comparisonSameTargets++;
            else
                comparisonDifferentTargets++;
        }

        if (plansLogged >= demandWeightedMinerPlanSimulationLogTopN)
            continue;

        String assignmentReason;

        if (strongPressureOverflow) {
            assignmentReason = "strong pressure justified profile cap overflow";
        } else if (selected.existingAssignments > 0) {
            assignmentReason = "load-balanced demand pressure";
        } else {
            assignmentReason = "highest demand pressure";
        }

        assignmentReason += selected.target.samePlanet ?
            String("; same-planet opportunity") :
            String("; travel-required opportunity");

        info(String("DemandWeightedMinerPlanSimulation miner=") +
             String::valueOf(miner.objectID) +
             " zone=" + miner.zoneName +
             " selectedProfile=" + selectedResult.profileKey +
             " pressureScore=" +
                String::valueOf(Math::getPrecision(
                    selectedResult.pressureScore, 1)) +
             " rawPressureScore=" +
                String::valueOf(Math::getPrecision(
                    selectedResult.pressureScore, 1)) +
             " locationAdjustedResourceScore=" +
                String::valueOf(Math::getPrecision(
                    selected.target.adjustedResourceScore, 1)) +
             " adjustedPlanScore=" +
                String::valueOf(Math::getPrecision(
                    selected.balancedScore, 1)) +
             " selectionRank=" + String::valueOf(selectionRank) +
             " candidatesEvaluated=" +
                String::valueOf(evaluatedCandidates.size()) +
             " candidatesCapped=" +
                String::valueOf(cappedCandidateCount) +
             " candidatesTravelPenalized=" +
                String::valueOf(travelPenaltyCandidateCount) +
             " candidatesRejected=" +
                String::valueOf(rejectedCandidateCount) +
             " candidatesLostHigherScore=" +
                String::valueOf(lostHigherAdjustedScoreCount) +
             " candidatesLostStrongPressureOverflow=" +
                String::valueOf(lostStrongPressureOverflowCount) +
             (rejectedCandidates.isEmpty() ?
                String("") :
                String(" rejectedCandidates=\"") +
                    rejectedCandidates + "\"") +
             " demandState=" + selectedResult.state +
             " target=" + selectedResource.name +
             " type=" + selectedResource.type +
             " zones=" +
                (selectedResource.zones.isEmpty() ?
                    String("unknown") : selectedResource.zones) +
             " resourceDemandScore=" +
                String::valueOf(selected.target.match.demandScore) +
             " samePlanet=" +
                (selected.target.samePlanet ? String("true") : String("false")) +
             " travelRequired=" +
                (selected.target.samePlanet ? String("false") : String("true")) +
             " profileAssignmentCount=" +
                String::valueOf(newAssignmentCount) +
             " assignmentReason=\"" + assignmentReason + "\"" +
             " mode=simulation-only", true);
        plansLogged++;
    }

    info(String("DemandWeightedMinerPlanSimulation summary activeMiners=") +
         String::valueOf(miners.size()) +
         " eligibleProfiles=" + String::valueOf(pressureResults.size()) +
         " plansProduced=" + String::valueOf(plansProduced) +
         " plansLogged=" + String::valueOf(plansLogged) +
         " truncated=" +
            (miners.size() > plansLogged ? String("true") : String("false")) +
         " mode=simulation-only", true);

    info(String("DemandWeightedMinerPlanComparison minersEvaluated=") +
         String::valueOf(miners.size()) +
         " comparisonBasis=category" +
         " sameProfile=" + String::valueOf(comparisonSameProfiles) +
         " differentProfile=" +
            String::valueOf(comparisonDifferentProfiles) +
         " sameTargetResource=" +
            String::valueOf(comparisonSameTargets) +
         " differentTargetResource=" +
            String::valueOf(comparisonDifferentTargets) +
         " noD4Plan=" + String::valueOf(comparisonNoD4Plan) +
         " noD66Plan=" + String::valueOf(comparisonNoDemandPlan) +
         " mode=simulation-only", true);

    info(String("DemandWeightedMinerPlanCalibration activeMiners=") +
         String::valueOf(miners.size()) +
         " eligibleProfiles=" + String::valueOf(pressureResults.size()) +
         " profilesDisabled=" + String::valueOf(profilesDisabled) +
         " profilesInactivePhase=" +
            String::valueOf(profilesInactivePhase) +
         " profilesBelowPressure=" +
            String::valueOf(profilesBelowPressure) +
         " profilesNoEligibleResource=" +
            String::valueOf(profilesNoEligibleResource) +
         " profilesCapped=" + String::valueOf(cappedProfiles.size()) +
         " plansProduced=" + String::valueOf(plansProduced) +
         " noPlanCount=" + String::valueOf(noPlanCount) +
         " overflowAssignments=" +
            String::valueOf(overflowAssignments) +
         " comparisonChangedProfiles=" +
            String::valueOf(comparisonDifferentProfiles) +
         " comparisonChangedTargets=" +
            String::valueOf(comparisonDifferentTargets) +
         (profileRejectionSummary.isEmpty() ?
            String("") :
            String(" rejectedProfiles=\"") +
                profileRejectionSummary + "\"") +
         " mode=simulation-only", true);
}

void SimPlayerManager::scheduleMinerIntelligentTargetingTask() {
    if (!enabled || !minerIntelligentTargetingEnabled ||
            (minerIntelligentTargetingMode != "shadow" &&
             minerIntelligentTargetingMode != "limited") ||
            minerIntelligentTargetingTaskScheduled)
        return;

    minerIntelligentTargetingTaskScheduled = true;

    Reference<MinerIntelligentTargetingTask*> task =
        new MinerIntelligentTargetingTask();
    task->schedule(minerIntelligentTargetingIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerIntelligentTargetingTask() {
    minerIntelligentTargetingTaskScheduled = false;

    if (!enabled)
        return;

    refreshMinerIntelligentTargetingConfig();

    if (!minerIntelligentTargetingEnabled ||
            (minerIntelligentTargetingMode != "shadow" &&
             minerIntelligentTargetingMode != "limited"))
        return;

    logMinerIntelligentTargetingDecisions();
    scheduleMinerIntelligentTargetingTask();
}

void SimPlayerManager::refreshMinerIntelligentTargetingConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        minerIntelligentTargetingEnabled = false;
        minerIntelligentTargetingMode = "off";
        warning(String("MinerTargetingSwitchDecision configReloadFailed=true reason=\"") +
            e.getMessage() + "\" mode=off diagnosticOnly=true");
        return;
    }

    LuaObject managerConfig =
        configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable() ||
            !managerConfig.getBooleanField("enabled", false)) {
        minerIntelligentTargetingEnabled = false;
        minerIntelligentTargetingMode = "off";
        managerConfig.pop();
        return;
    }

    LuaObject demandWeightedConfig =
        managerConfig.getObjectField("demandWeightedMinerPlanSimulationConfig");

    if (demandWeightedConfig.isValidTable())
        applyDemandWeightedMinerPlanSimulationConfig(demandWeightedConfig);

    demandWeightedConfig.pop();
    applyDemandWeightedMinerPlanDependencyConfig(managerConfig);

    LuaObject stationedConfig =
        managerConfig.getObjectField("stationedMinerConfig");

    if (stationedConfig.isValidTable())
        applyStationedMinerConfig(stationedConfig);

    stationedConfig.pop();

    LuaObject targetingConfig =
        managerConfig.getObjectField("minerIntelligentTargetingConfig");

    if (!targetingConfig.isValidTable()) {
        minerIntelligentTargetingEnabled = false;
        minerIntelligentTargetingMode = "off";
        targetingConfig.pop();
        managerConfig.pop();
        return;
    }

    applyMinerIntelligentTargetingConfig(targetingConfig);

    targetingConfig.pop();
    managerConfig.pop();
}

bool SimPlayerManager::getMinerIntelligentTargetAssignment(
        uint64 minerID, MinerIntelligentTargetAssignment& assignment) {
    if (minerID == 0)
        return false;

    Locker locker(&minerIntelligentTargetingAssignmentMutex);

    if (!minerIntelligentTargetAssignments.contains(minerID))
        return false;

    assignment = minerIntelligentTargetAssignments.get(minerID);
    return assignment.isValid();
}

void SimPlayerManager::putMinerIntelligentTargetAssignment(
        const MinerIntelligentTargetAssignment& assignment) {
    if (!assignment.isValid())
        return;

    Locker locker(&minerIntelligentTargetingAssignmentMutex);
    minerIntelligentTargetAssignments.put(assignment.minerID, assignment);
}

bool SimPlayerManager::isMinerIntelligentTargetZoneAllowed(const String& zoneName) {
    if (minerIntelligentTargetingLimitedAllowedZones.size() == 0)
        return true;

    for (int i = 0; i < minerIntelligentTargetingLimitedAllowedZones.size(); ++i) {
        if (minerIntelligentTargetingLimitedAllowedZones.get(i) == zoneName)
            return true;
    }

    return false;
}

bool SimPlayerManager::isMinerIntelligentAssignmentActive(
        const MinerIntelligentTargetAssignment& assignment) {
    return assignment.status == "queued" ||
        assignment.status == "activation_started" ||
        assignment.status == "sample_started" ||
        assignment.status == "stationed" ||
        assignment.status == "sample_complete";
}

bool SimPlayerManager::isMinerIntelligentAssignmentNormalTtlElapsed(
        const MinerIntelligentTargetAssignment& assignment, uint64 nowMs) {
    if (assignment.createdAtMs == 0)
        return false;

    uint64 timeoutSeconds = minerIntelligentTargetingCandidateAssignmentTtlSeconds;

    if (assignment.status == "candidate" || assignment.status.isEmpty()) {
        timeoutSeconds = minerIntelligentTargetingCandidateAssignmentTtlSeconds;
    } else if (assignment.status == "validated") {
        timeoutSeconds = minerIntelligentTargetingValidatedAssignmentTtlSeconds;
    }

    uint64 baseMs = assignment.status == "validated" &&
        assignment.validatedAtMs > 0 ? assignment.validatedAtMs :
        assignment.createdAtMs;
    uint64 expiresAtMs = baseMs + timeoutSeconds * 1000;

    if (assignment.expiresAtMs > 0 &&
            (assignment.status == "candidate" || assignment.status.isEmpty()))
        expiresAtMs = assignment.expiresAtMs;

    return nowMs > expiresAtMs;
}

uint64 SimPlayerManager::getMinerIntelligentMovementArrivalTimeoutSeconds(
        const MinerIntelligentTargetAssignment& assignment) {
    float pathDistance = assignment.activationPathDistance > 0.f ?
        assignment.activationPathDistance :
        (assignment.validatedPathDistance > 0.f ?
            assignment.validatedPathDistance : assignment.latestPathDistance);

    if (pathDistance <= 0.f)
        return static_cast<uint64>(minerIntelligentTargetingMovementArrivalTimeoutSeconds);

    float timeoutSeconds =
        static_cast<float>(minerIntelligentTargetingMovementArrivalTimeoutMinSeconds) +
        pathDistance * minerIntelligentTargetingMovementArrivalSecondsPerMeter;

    timeoutSeconds = Math::max(
        static_cast<float>(minerIntelligentTargetingMovementArrivalTimeoutMinSeconds),
        Math::min(timeoutSeconds,
            static_cast<float>(minerIntelligentTargetingMovementArrivalTimeoutMaxSeconds)));

    return static_cast<uint64>(timeoutSeconds);
}

String SimPlayerManager::getMinerIntelligentAssignmentTimeoutReason(
        MinerIntelligentTargetAssignment& assignment, uint64 nowMs,
        uint64& ageSeconds, uint64& timeoutSeconds, bool logNormalTtlSkip) {
    ageSeconds = 0;
    timeoutSeconds = 0;

    if (assignment.createdAtMs == 0)
        return "";

    if (assignment.status == "candidate" || assignment.status.isEmpty()) {
        timeoutSeconds =
            static_cast<uint64>(minerIntelligentTargetingCandidateAssignmentTtlSeconds);
        ageSeconds = nowMs > assignment.createdAtMs ?
            (nowMs - assignment.createdAtMs) / 1000 : 0;

        return ageSeconds > timeoutSeconds ? String("expired") : String("");
    }

    if (assignment.status == "validated") {
        timeoutSeconds =
            static_cast<uint64>(minerIntelligentTargetingValidatedAssignmentTtlSeconds);
        uint64 baseMs = assignment.validatedAtMs > 0 ?
            assignment.validatedAtMs : assignment.createdAtMs;
        ageSeconds = nowMs > baseMs ? (nowMs - baseMs) / 1000 : 0;

        return ageSeconds > timeoutSeconds ? String("expired") : String("");
    }

    bool normalTtlElapsed =
        isMinerIntelligentAssignmentNormalTtlElapsed(assignment, nowMs);
    bool activeMovement =
        assignment.status == "queued" ||
        assignment.status == "activation_started" ||
        assignment.status == "sample_started" ||
        assignment.status == "stationed";

    if (logNormalTtlSkip &&
            normalTtlElapsed && activeMovement &&
            minerIntelligentTargetingPreventNormalTtlForActiveMovement &&
            !assignment.normalTtlSkippedForActiveMovement) {
        assignment.normalTtlSkippedForActiveMovement = true;

        {
            Locker healthLocker(&minerIntelligentTargetingHealthMutex);
            minerIntelligentActivationHealthExpiredWhileActivePrevented++;
            minerIntelligentActivationHealthNormalTtlSkippedForActiveMovement++;
        }

        if (minerIntelligentTargetingAssignmentLogLifecycle) {
            info(String("MinerIntelligentTargetAssignment miner=") +
                 String::valueOf(assignment.minerID) +
                 " action=retained" +
                 " clearReason=none" +
                 " reason=normalTtlSkippedForActiveMovement" +
                 " lifecycleStatus=" + assignment.status +
                 " assignmentGenerationId=" +
                    String::valueOf(assignment.assignmentGenerationId) +
                 " targetHash=" +
                    (assignment.targetHash.isEmpty() ?
                        String("none") : assignment.targetHash) +
                 " activationSnapshotId=" +
                    String::valueOf(assignment.activationSnapshotId) +
                 " mode=" + minerIntelligentTargetingMode, true);
        }
    }

    if (assignment.status == "queued") {
        uint64 baseMs = assignment.queuedAtMs > 0 ?
            assignment.queuedAtMs : assignment.createdAtMs;
        timeoutSeconds =
            static_cast<uint64>(minerIntelligentTargetingQueuedActivationTtlSeconds);
        ageSeconds = nowMs > baseMs ? (nowMs - baseMs) / 1000 : 0;

        return ageSeconds > timeoutSeconds ?
            String("queuedActivationTimeout") : String("");
    }

    if (assignment.status == "activation_started") {
        uint64 baseMs = assignment.activatedAtMs > 0 ?
            assignment.activatedAtMs :
            (assignment.queuedAtMs > 0 ? assignment.queuedAtMs : assignment.createdAtMs);
        timeoutSeconds = getMinerIntelligentMovementArrivalTimeoutSeconds(assignment);
        ageSeconds = nowMs > baseMs ? (nowMs - baseMs) / 1000 : 0;

        return ageSeconds > timeoutSeconds ?
            String("movementArrivalTimeout") : String("");
    }

    if (assignment.status == "sample_started") {
        uint64 baseMs = assignment.sampleStartedAtMs > 0 ?
            assignment.sampleStartedAtMs : assignment.createdAtMs;
        timeoutSeconds =
            static_cast<uint64>(minerIntelligentTargetingSampleStartedTimeoutSeconds);
        ageSeconds = nowMs > baseMs ? (nowMs - baseMs) / 1000 : 0;

        return ageSeconds > timeoutSeconds ? String("sampleTimeout") : String("");
    }

    if (assignment.status == "stationed" &&
            stationedMinerLifecycleEnabled &&
            stationedMinerMaxDurationSeconds > 0) {
        uint64 baseMs = assignment.stationedAtMs > 0 ?
            assignment.stationedAtMs : assignment.createdAtMs;
        timeoutSeconds =
            static_cast<uint64>(stationedMinerMaxDurationSeconds);
        ageSeconds = nowMs > baseMs ? (nowMs - baseMs) / 1000 : 0;

        return ageSeconds > timeoutSeconds ?
            String("maxStationDurationReached") : String("");
    }

    return "";
}

int SimPlayerManager::countActiveMinerIntelligentAssignments() {
    int activeCount = 0;
    uint64 now = System::getMiliTime();

    Locker locker(&minerIntelligentTargetingAssignmentMutex);

    for (int i = 0; i < minerIntelligentTargetAssignments.size(); ++i) {
        MinerIntelligentTargetAssignment assignment =
            minerIntelligentTargetAssignments.elementAt(i).getValue();
        uint64 timeoutAgeSeconds = 0;
        uint64 timeoutSeconds = 0;

        if (!getMinerIntelligentAssignmentTimeoutReason(
                assignment, now, timeoutAgeSeconds, timeoutSeconds, false).isEmpty())
            continue;

        if (isMinerIntelligentAssignmentActive(assignment))
            activeCount++;
    }

    return activeCount;
}

bool SimPlayerManager::isMinerIntelligentActivationOnCooldown(
        uint64 minerID, uint64 nowMs) {
    if (minerID == 0 || minerIntelligentTargetingLimitedCooldownSecondsPerMiner <= 0)
        return false;

    Locker locker(&minerIntelligentTargetingCooldownMutex);

    if (!minerIntelligentTargetingLastActivationMs.contains(minerID))
        return false;

    uint64 lastActivationMs = minerIntelligentTargetingLastActivationMs.get(minerID);
    uint64 cooldownMs =
        static_cast<uint64>(minerIntelligentTargetingLimitedCooldownSecondsPerMiner) * 1000;

    return lastActivationMs > 0 && nowMs < lastActivationMs + cooldownMs;
}

void SimPlayerManager::rememberMinerIntelligentActivation(uint64 minerID, uint64 nowMs) {
    if (minerID == 0)
        return;

    Locker locker(&minerIntelligentTargetingCooldownMutex);
    minerIntelligentTargetingLastActivationMs.put(minerID, nowMs);
}

void SimPlayerManager::recordMinerIntelligentActivationHealthEvent(
        const String& eventName) {
    Locker locker(&minerIntelligentTargetingHealthMutex);

    if (eventName == "attempted") {
        minerIntelligentActivationHealthAttempts++;
    } else if (eventName == "started") {
        minerIntelligentActivationHealthStarted++;
    } else if (eventName == "arrival") {
        minerIntelligentActivationHealthArrivals++;
    } else if (eventName == "sampleFinished") {
        minerIntelligentActivationHealthSamplesCompleted++;
    } else if (eventName == "pathFailed") {
        minerIntelligentActivationHealthPathFailures++;
    } else if (eventName == "expired") {
        minerIntelligentActivationHealthExpired++;
    } else if (eventName == "candidateExpired") {
        minerIntelligentActivationHealthCandidateExpired++;
    } else if (eventName == "validatedExpired") {
        minerIntelligentActivationHealthValidatedExpired++;
    } else if (eventName == "queuedActivationTimeout") {
        minerIntelligentActivationHealthQueuedActivationTimeout++;
    } else if (eventName == "movementArrivalTimeout") {
        minerIntelligentActivationHealthMovementArrivalTimeout++;
    } else if (eventName == "sampleTimeout") {
        minerIntelligentActivationHealthSampleTimeout++;
    } else if (eventName == "cooldownSkip") {
        minerIntelligentActivationHealthCooldownSkips++;
    } else if (eventName == "activeCapSkip") {
        minerIntelligentActivationHealthActiveCapSkips++;
    } else if (eventName == "zoneSkip") {
        minerIntelligentActivationHealthZoneSkips++;
    }
}

static void updateReachabilityBucketMetric(
        MinerReachabilityCalibrationBucket& bucket,
        const String& metric,
        float distance,
        bool includeDistance) {
    if (metric == "candidateGenerated") {
        bucket.candidatesGenerated++;
        bucket.densityTargetsChosen++;
    } else if (metric == "candidateValidated") {
        bucket.candidatesValidated++;
        bucket.densityTargetsValidated++;
    } else if (metric == "candidateRejected") {
        bucket.candidatesRejected++;
    } else if (metric == "activated") {
        bucket.densityTargetsActivated++;
    } else if (metric == "sampleComplete") {
        bucket.densityTargetsSampleCompleted++;
    } else if (metric == "coverageRetained") {
        bucket.coverageRetainedCount++;
        bucket.stationedSampleCount++;
    }

    if (includeDistance && distance >= 0.f) {
        bucket.distanceTotal += distance;
        bucket.distanceSamples++;
    }
}

static void updateReachabilityBucketMap(
        VectorMap<String, MinerReachabilityCalibrationBucket>& buckets,
        const String& key,
        const String& metric,
        float distance,
        bool includeDistance) {
    String bucketKey = key.isEmpty() ? String("unknown") : key;
    MinerReachabilityCalibrationBucket bucket;

    if (buckets.contains(bucketKey))
        bucket = buckets.get(bucketKey);

    updateReachabilityBucketMetric(bucket, metric, distance, includeDistance);
    buckets.put(bucketKey, bucket);
}

String SimPlayerManager::getReachabilityDistanceBand(float distance) const {
    if (distance < 0.f)
        return "unknown";

    if (distance < 128.f)
        return "0-128m";

    if (distance < 256.f)
        return "128-256m";

    if (distance < 512.f)
        return "256-512m";

    return "512m+";
}

String SimPlayerManager::getReachabilityResourceClass(
        const String& resourceType) const {
    if (resourceType.isEmpty())
        return "unknown";

    int separator = resourceType.indexOf("_");

    if (separator <= 0)
        return resourceType;

    return resourceType.subString(0, separator);
}

String SimPlayerManager::getReachabilityValidationOutcome(
        const MinerPathValidationSnapshot& snapshot) const {
    if (snapshot.pathFound && snapshot.pathTrustStatus == "verifiedPath")
        return snapshot.directFallback ? "directFallbackVerified" : "verifiedPath";

    if (snapshot.directFallback ||
            snapshot.rejectReason == "directFallbackUnverified" ||
            snapshot.pathTrustStatus == "directFallbackUnverified")
        return "directFallbackUnverified";

    if (snapshot.rejectReason == "pathException" ||
            snapshot.rejectReason == "noPath")
        return "pathGenerationFailed";

    if (snapshot.rejectReason == "pathTooLong" ||
            snapshot.rejectReason == "tooManyPathNodes" ||
            snapshot.pathTrustStatus == "pathTooLong" ||
            snapshot.pathTrustStatus == "tooManyPathNodes")
        return "pathRejected";

    if (!snapshot.pathFound)
        return "pathRejected";

    return "pathRejected";
}

String SimPlayerManager::getReachabilityFailureReason(
        const MinerPathValidationSnapshot& snapshot) const {
    if (snapshot.pathFound && snapshot.pathTrustStatus == "verifiedPath")
        return "none";

    if (snapshot.rejectReason == "pathTooLong" ||
            snapshot.pathTrustStatus == "pathTooLong")
        return "validationDistanceExceeded";

    if (snapshot.rejectReason == "directFallbackUnverified" ||
            snapshot.pathTrustStatus == "directFallbackUnverified" ||
            snapshot.directFallback)
        return "trustInsufficient";

    if (snapshot.rejectReason == "pathException" ||
            snapshot.rejectReason == "noPath")
        return "pathGenerationFailed";

    if (!snapshot.rejectReason.isEmpty() && snapshot.rejectReason != "none")
        return snapshot.rejectReason;

    if (!snapshot.pathTrustStatus.isEmpty() &&
            snapshot.pathTrustStatus != "verifiedPath")
        return snapshot.pathTrustStatus;

    return "pathRejected";
}

void SimPlayerManager::recordReachabilityCandidateGenerated(
        const MinerIntelligentTargetAssignment& assignment) {
    if (assignment.minerID == 0)
        return;

    float distance = assignment.targetDirectDistance;
    String distanceBand = getReachabilityDistanceBand(distance);
    String resourceClass =
        getReachabilityResourceClass(assignment.targetResourceType);

    Locker locker(&minerReachabilityCalibrationMutex);
    updateReachabilityBucketMetric(
        minerReachabilityTotals, "candidateGenerated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByPlanet, assignment.targetZoneName,
        "candidateGenerated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByResourceClass, resourceClass,
        "candidateGenerated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDensitySource, assignment.targetSource,
        "candidateGenerated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDistanceBand, distanceBand,
        "candidateGenerated", distance, true);
}

void SimPlayerManager::recordReachabilityAssignmentValidated(
        const MinerIntelligentTargetAssignment& assignment) {
    if (assignment.minerID == 0)
        return;

    float distance = assignment.targetDirectDistance;

    if (distance <= 0.f)
        distance = assignment.latestPathDistance;

    String distanceBand = getReachabilityDistanceBand(distance);
    String resourceClass =
        getReachabilityResourceClass(assignment.targetResourceType);

    Locker locker(&minerReachabilityCalibrationMutex);
    updateReachabilityBucketMetric(
        minerReachabilityTotals, "candidateValidated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByPlanet, assignment.targetZoneName,
        "candidateValidated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByResourceClass, resourceClass,
        "candidateValidated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDensitySource, assignment.targetSource,
        "candidateValidated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDistanceBand, distanceBand,
        "candidateValidated", distance, true);
}

void SimPlayerManager::recordReachabilityCandidateRejected(
        const MinerIntelligentTargetAssignment& assignment,
        const String& reason) {
    if (assignment.minerID == 0)
        return;

    float distance = assignment.targetDirectDistance;

    if (distance <= 0.f)
        distance = assignment.latestPathDistance;

    String distanceBand = getReachabilityDistanceBand(distance);
    String resourceClass =
        getReachabilityResourceClass(assignment.targetResourceType);
    String failureReason = reason.isEmpty() ? String("pathRejected") : reason;

    Locker locker(&minerReachabilityCalibrationMutex);
    updateReachabilityBucketMetric(
        minerReachabilityTotals, "candidateRejected", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByPlanet, assignment.targetZoneName,
        "candidateRejected", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByResourceClass, resourceClass,
        "candidateRejected", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDensitySource, assignment.targetSource,
        "candidateRejected", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDistanceBand, distanceBand,
        "candidateRejected", distance, true);

    if (failureReason == "candidateExpiredBeforeValidation") {
        int count = minerReachabilityFailureReasons.contains(failureReason) ?
            minerReachabilityFailureReasons.get(failureReason) : 0;
        minerReachabilityFailureReasons.put(failureReason, count + 1);
    }
}

void SimPlayerManager::recordReachabilityAssignmentActivated(
        const MinerIntelligentTargetAssignment& assignment) {
    if (assignment.minerID == 0)
        return;

    if (reachabilityMemoryEnabled) {
        updateReachabilityMemoryFromAssignment(
            assignment,
            "activation",
            reachabilityBucketSizeMeters,
            reachabilityMemoryTtlSeconds,
            reachabilityMaxMemoryRows);
    }

    float distance = assignment.targetDirectDistance;

    if (distance <= 0.f)
        distance = assignment.activationPathDistance;

    String distanceBand = getReachabilityDistanceBand(distance);
    String resourceClass =
        getReachabilityResourceClass(assignment.targetResourceType);

    Locker locker(&minerReachabilityCalibrationMutex);
    updateReachabilityBucketMetric(
        minerReachabilityTotals, "activated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByPlanet, assignment.targetZoneName,
        "activated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByResourceClass, resourceClass,
        "activated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDensitySource, assignment.targetSource,
        "activated", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDistanceBand, distanceBand,
        "activated", distance, true);
}

void SimPlayerManager::recordReachabilitySampleCompleted(
        const MinerIntelligentTargetAssignment& assignment) {
    if (assignment.minerID == 0)
        return;

    if (reachabilityMemoryEnabled) {
        updateReachabilityMemoryFromAssignment(
            assignment,
            "sampleComplete",
            reachabilityBucketSizeMeters,
            reachabilityMemoryTtlSeconds,
            reachabilityMaxMemoryRows);
    }

    float distance = assignment.targetDirectDistance;

    if (distance <= 0.f)
        distance = assignment.activationPathDistance;

    String distanceBand = getReachabilityDistanceBand(distance);
    String resourceClass =
        getReachabilityResourceClass(assignment.targetResourceType);

    Locker locker(&minerReachabilityCalibrationMutex);
    updateReachabilityBucketMetric(
        minerReachabilityTotals, "sampleComplete", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByPlanet, assignment.targetZoneName,
        "sampleComplete", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByResourceClass, resourceClass,
        "sampleComplete", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDensitySource, assignment.targetSource,
        "sampleComplete", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDistanceBand, distanceBand,
        "sampleComplete", distance, true);
}

void SimPlayerManager::recordReachabilityStationedCoverage(
        const MinerIntelligentTargetAssignment& assignment) {
    if (assignment.minerID == 0)
        return;

    if (reachabilityMemoryEnabled) {
        updateReachabilityMemoryFromAssignment(
            assignment,
            "coverageRetained",
            reachabilityBucketSizeMeters,
            reachabilityMemoryTtlSeconds,
            reachabilityMaxMemoryRows);
    }

    float distance = assignment.targetDirectDistance;

    if (distance <= 0.f)
        distance = assignment.activationPathDistance;

    String distanceBand = getReachabilityDistanceBand(distance);
    String resourceClass =
        getReachabilityResourceClass(assignment.targetResourceType);

    Locker locker(&minerReachabilityCalibrationMutex);
    updateReachabilityBucketMetric(
        minerReachabilityTotals, "coverageRetained", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByPlanet, assignment.targetZoneName,
        "coverageRetained", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByResourceClass, resourceClass,
        "coverageRetained", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDensitySource, assignment.targetSource,
        "coverageRetained", distance, true);
    updateReachabilityBucketMap(
        minerReachabilityByDistanceBand, distanceBand,
        "coverageRetained", distance, true);
}

void SimPlayerManager::recordReachabilityValidationSnapshot(
        const MinerPathValidationSnapshot& snapshot) {
    String outcome = getReachabilityValidationOutcome(snapshot);
    String failureReason = getReachabilityFailureReason(snapshot);
    float distance = snapshot.directDistance >= 0.f ?
        snapshot.directDistance : snapshot.pathDistance;

    if (reachabilityMemoryEnabled) {
        String eventName = "validation";

        if (snapshot.pathFound && snapshot.pathTrustStatus == "verifiedPath")
            eventName = "verifiedPath";
        else if (snapshot.directFallback ||
                snapshot.pathTrustStatus == "directFallbackUnverified" ||
                snapshot.rejectReason == "directFallbackUnverified")
            eventName = "directFallbackUnverified";

        String key = buildReachabilityMemoryKey(
            snapshot.zoneName,
            snapshot.resourceName,
            snapshot.resourceType,
            snapshot.profileKey,
            snapshot.targetSource,
            snapshot.targetX,
            snapshot.targetY,
            reachabilityBucketSizeMeters);
        float pathDistance = snapshot.pathDistance > 0.f ?
            snapshot.pathDistance : distance;

        updateReachabilityMemoryBucket(
            key,
            snapshot.zoneName,
            snapshot.resourceName,
            snapshot.resourceType,
            snapshot.profileKey,
            snapshot.targetSource,
            snapshot.targetX,
            snapshot.targetY,
            snapshot.density,
            pathDistance,
            eventName,
            reachabilityBucketSizeMeters,
            reachabilityMemoryTtlSeconds,
            reachabilityMaxMemoryRows);
    }

    Locker locker(&minerReachabilityCalibrationMutex);
    MinerReachabilityValidationOutcome outcomeStats;

    if (minerReachabilityValidationOutcomes.contains(outcome))
        outcomeStats = minerReachabilityValidationOutcomes.get(outcome);

    outcomeStats.count++;

    if (distance >= 0.f) {
        outcomeStats.distanceTotal += distance;
        outcomeStats.distanceSamples++;
    }

    minerReachabilityValidationOutcomes.put(outcome, outcomeStats);

    if (failureReason != "none") {
        int count = minerReachabilityFailureReasons.contains(failureReason) ?
            minerReachabilityFailureReasons.get(failureReason) : 0;
        minerReachabilityFailureReasons.put(failureReason, count + 1);
    }
}

static float reachabilityPercent(int numerator, int denominator) {
    if (denominator <= 0)
        return 0.f;

    return Math::getPrecision(
        (static_cast<float>(numerator) * 100.f) /
            static_cast<float>(denominator),
        1);
}

static float reachabilityAverageDistance(float total, int samples) {
    if (samples <= 0)
        return 0.f;

    return Math::getPrecision(total / static_cast<float>(samples), 1);
}

static JSONSerializationType buildReachabilityFunnelJSON(
        const MinerReachabilityCalibrationBucket& bucket) {
    JSONSerializationType json = JSONSerializationType::object();
    json["candidatesGenerated"] = bucket.candidatesGenerated;
    json["candidatesValidated"] = bucket.candidatesValidated;
    json["candidatesRejected"] = bucket.candidatesRejected;
    json["validationSuccessPercent"] =
        reachabilityPercent(
            bucket.candidatesValidated, bucket.candidatesGenerated);
    json["rejectionPercent"] =
        reachabilityPercent(
            bucket.candidatesRejected, bucket.candidatesGenerated);
    json["averageDistance"] =
        reachabilityAverageDistance(
            bucket.distanceTotal, bucket.distanceSamples);
    return json;
}

static JSONSerializationType buildReachabilityDensityJSON(
        const MinerReachabilityCalibrationBucket& bucket) {
    JSONSerializationType json = JSONSerializationType::object();
    json["densityTargetsChosen"] = bucket.densityTargetsChosen;
    json["densityTargetsValidated"] = bucket.densityTargetsValidated;
    json["densityTargetsActivated"] = bucket.densityTargetsActivated;
    json["densityTargetsSampleCompleted"] =
        bucket.densityTargetsSampleCompleted;
    json["chosenToValidatedPercent"] =
        reachabilityPercent(
            bucket.densityTargetsValidated, bucket.densityTargetsChosen);
    json["validatedToActivatedPercent"] =
        reachabilityPercent(
            bucket.densityTargetsActivated, bucket.densityTargetsValidated);
    json["activatedToSampleCompletePercent"] =
        reachabilityPercent(
            bucket.densityTargetsSampleCompleted,
            bucket.densityTargetsActivated);
    json["chosenToSampleCompletePercent"] =
        reachabilityPercent(
            bucket.densityTargetsSampleCompleted, bucket.densityTargetsChosen);
    return json;
}

static JSONSerializationType buildReachabilityBucketRowJSON(
        const String& keyField,
        const String& key,
        const MinerReachabilityCalibrationBucket& bucket) {
    JSONSerializationType row = JSONSerializationType::object();
    row[keyField] = key.isEmpty() ? String("unknown") : key;
    row["candidates"] = bucket.candidatesGenerated;
    row["candidatesGenerated"] = bucket.candidatesGenerated;
    row["validated"] = bucket.candidatesValidated;
    row["rejected"] = bucket.candidatesRejected;
    row["activated"] = bucket.densityTargetsActivated;
    row["sampleComplete"] = bucket.densityTargetsSampleCompleted;
    row["densityTargetsChosen"] = bucket.densityTargetsChosen;
    row["densityTargetsValidated"] = bucket.densityTargetsValidated;
    row["densityTargetsActivated"] = bucket.densityTargetsActivated;
    row["densityTargetsSampleCompleted"] =
        bucket.densityTargetsSampleCompleted;
    row["validationSuccessPercent"] =
        reachabilityPercent(
            bucket.candidatesValidated, bucket.candidatesGenerated);
    row["activationSuccessPercent"] =
        reachabilityPercent(
            bucket.densityTargetsActivated, bucket.densityTargetsValidated);
    row["completionSuccessPercent"] =
        reachabilityPercent(
            bucket.densityTargetsSampleCompleted,
            bucket.densityTargetsActivated);
    row["chosenToSampleCompletePercent"] =
        reachabilityPercent(
            bucket.densityTargetsSampleCompleted,
            bucket.densityTargetsChosen);
    row["averageDistance"] =
        reachabilityAverageDistance(
            bucket.distanceTotal, bucket.distanceSamples);
    return row;
}

static JSONSerializationType buildReachabilityBucketRowsJSON(
        VectorMap<String, MinerReachabilityCalibrationBucket>& buckets,
        const String& keyField) {
    JSONSerializationType rows = JSONSerializationType::array();

    for (int i = 0; i < buckets.size(); ++i) {
        rows.push_back(
            buildReachabilityBucketRowJSON(
                keyField,
                buckets.elementAt(i).getKey(),
                buckets.elementAt(i).getValue()));
    }

    return rows;
}

static JSONSerializationType buildReachabilityOutcomeRowsJSON(
        VectorMap<String, MinerReachabilityValidationOutcome>& outcomes) {
    JSONSerializationType rows = JSONSerializationType::array();
    int total = 0;

    for (int i = 0; i < outcomes.size(); ++i)
        total += outcomes.elementAt(i).getValue().count;

    for (int i = 0; i < outcomes.size(); ++i) {
        MinerReachabilityValidationOutcome outcome =
            outcomes.elementAt(i).getValue();
        JSONSerializationType row = JSONSerializationType::object();
        row["outcome"] = outcomes.elementAt(i).getKey();
        row["count"] = outcome.count;
        row["percent"] = reachabilityPercent(outcome.count, total);
        row["averageDistance"] =
            reachabilityAverageDistance(
                outcome.distanceTotal, outcome.distanceSamples);
        rows.push_back(row);
    }

    return rows;
}

static JSONSerializationType buildReachabilityFailureRowsJSON(
        VectorMap<String, int>& reasons) {
    JSONSerializationType rows = JSONSerializationType::array();
    int total = 0;

    for (int i = 0; i < reasons.size(); ++i)
        total += reasons.elementAt(i).getValue();

    for (int i = 0; i < reasons.size(); ++i) {
        JSONSerializationType row = JSONSerializationType::object();
        row["reason"] = reasons.elementAt(i).getKey();
        row["count"] = reasons.elementAt(i).getValue();
        row["percent"] =
            reachabilityPercent(reasons.elementAt(i).getValue(), total);
        rows.push_back(row);
    }

    return rows;
}

void SimPlayerManager::snapshotAndResetMinerIntelligentActivationHealth(
        int& attempts, int& started, int& arrivals, int& samplesCompleted,
        int& pathFailures, int& expired, int& cooldownSkips,
        int& activeCapSkips, int& zoneSkips) {
    Locker locker(&minerIntelligentTargetingHealthMutex);

    attempts = minerIntelligentActivationHealthAttempts;
    started = minerIntelligentActivationHealthStarted;
    arrivals = minerIntelligentActivationHealthArrivals;
    samplesCompleted = minerIntelligentActivationHealthSamplesCompleted;
    pathFailures = minerIntelligentActivationHealthPathFailures;
    expired = minerIntelligentActivationHealthExpired;
    cooldownSkips = minerIntelligentActivationHealthCooldownSkips;
    activeCapSkips = minerIntelligentActivationHealthActiveCapSkips;
    zoneSkips = minerIntelligentActivationHealthZoneSkips;

    minerIntelligentActivationHealthAttempts = 0;
    minerIntelligentActivationHealthStarted = 0;
    minerIntelligentActivationHealthArrivals = 0;
    minerIntelligentActivationHealthSamplesCompleted = 0;
    minerIntelligentActivationHealthPathFailures = 0;
    minerIntelligentActivationHealthExpired = 0;
    minerIntelligentActivationHealthCandidateExpired = 0;
    minerIntelligentActivationHealthValidatedExpired = 0;
    minerIntelligentActivationHealthQueuedActivationTimeout = 0;
    minerIntelligentActivationHealthMovementArrivalTimeout = 0;
    minerIntelligentActivationHealthSampleTimeout = 0;
    minerIntelligentActivationHealthExpiredWhileActivePrevented = 0;
    minerIntelligentActivationHealthNormalTtlSkippedForActiveMovement = 0;
    minerIntelligentActivationHealthCooldownSkips = 0;
    minerIntelligentActivationHealthActiveCapSkips = 0;
    minerIntelligentActivationHealthZoneSkips = 0;
}

void SimPlayerManager::recordMinerIntelligentTargetAssignmentLifecycle(
        uint64 minerID, const String& eventName, const String& detail) {
    if (minerID == 0 || !minerIntelligentTargetingAssignmentEnabled)
        return;

    uint64 now = System::getMiliTime();

    Locker locker(&minerIntelligentTargetingAssignmentMutex);

    if (!minerIntelligentTargetAssignments.contains(minerID))
        return;

    MinerIntelligentTargetAssignment assignment =
        minerIntelligentTargetAssignments.get(minerID);
    String previousStatus = assignment.status;
    assignment.updatedAtMs = now;

    if (eventName == "queued") {
        if (assignment.queuedAtMs == 0)
            assignment.queuedAtMs = now;

        assignment.status = "queued";
        assignment.lastActivationResult =
            detail.isEmpty() ? String("queued") : detail;
    } else if (eventName == "activationStarted") {
        if (assignment.activatedAtMs == 0)
            assignment.activatedAtMs = now;

        assignment.status = "activation_started";
        assignment.lastActivationResult =
            detail.isEmpty() ? String("started") : detail;
        if (previousStatus != "activation_started")
            recordMinerIntelligentActivationHealthEvent("started");
    } else if (eventName == "sampleStarted") {
        if (assignment.sampleStartedAtMs == 0)
            assignment.sampleStartedAtMs = now;

        assignment.status = "sample_started";
        if (previousStatus != "sample_started")
            recordMinerIntelligentActivationHealthEvent("arrival");
    } else if (eventName == "sampleFinished") {
        if (assignment.sampleFinishedAtMs == 0)
            assignment.sampleFinishedAtMs = now;

        assignment.status = "sample_complete";
        if (previousStatus != "sample_complete")
            recordMinerIntelligentActivationHealthEvent("sampleFinished");
        if (!assignment.reachabilitySampleCompletedRecorded) {
            assignment.reachabilitySampleCompletedRecorded = true;
            recordReachabilitySampleCompleted(assignment);
        }
    } else if (eventName == "stationed") {
        if (assignment.stationedAtMs == 0)
            assignment.stationedAtMs = now;

        assignment.status = "stationed";
        assignment.rebalanceReason =
            detail.isEmpty() ? String("coverageRetained") : detail;
    } else if (eventName == "failed") {
        assignment.status = "failed";
        assignment.lastFailureReason =
            detail.isEmpty() ? String("unknown") : detail;

        if (detail == "pathFailed")
            recordMinerIntelligentActivationHealthEvent("pathFailed");
    }

    minerIntelligentTargetAssignments.put(minerID, assignment);
}

void SimPlayerManager::clearMinerIntelligentTargetAssignment(
        uint64 minerID, const String& reason, const String& mode) {
    if (minerID == 0)
        return;

    MinerIntelligentTargetAssignment previous;
    bool hadAssignment = false;

    {
        Locker locker(&minerIntelligentTargetingAssignmentMutex);

        if (minerIntelligentTargetAssignments.contains(minerID)) {
            previous = minerIntelligentTargetAssignments.get(minerID);
            minerIntelligentTargetAssignments.drop(minerID);
            hadAssignment = true;
        }
    }

    if (hadAssignment && (reason == "expired" || reason == "assignmentExpired")) {
        recordMinerIntelligentActivationHealthEvent("expired");
        if (previous.status == "validated")
            recordMinerIntelligentActivationHealthEvent("validatedExpired");
        else
            recordMinerIntelligentActivationHealthEvent("candidateExpired");

        if (previous.validatedAtMs == 0 &&
                previous.latestValidationSnapshotId == 0)
            recordReachabilityCandidateRejected(
                previous, "candidateExpiredBeforeValidation");
    } else if (hadAssignment && reason == "queuedActivationTimeout") {
        recordMinerIntelligentActivationHealthEvent("queuedActivationTimeout");
    } else if (hadAssignment && reason == "movementArrivalTimeout") {
        recordMinerIntelligentActivationHealthEvent("movementArrivalTimeout");
    } else if (hadAssignment && reason == "sampleTimeout") {
        recordMinerIntelligentActivationHealthEvent("sampleTimeout");
    }

    if (hadAssignment) {
        uint64 now = System::getMiliTime();
        MinerAssignmentHistorySnapshot history;
        history.minerID = minerID;
        history.assignmentGenerationId = previous.assignmentGenerationId;
        history.recordedAtMs = now;
        history.createdAtMs = previous.createdAtMs;
        history.validatedAtMs = previous.validatedAtMs;
        history.queuedAtMs = previous.queuedAtMs;
        history.activatedAtMs = previous.activatedAtMs;
        history.sampleStartedAtMs = previous.sampleStartedAtMs;
        history.sampleFinishedAtMs = previous.sampleFinishedAtMs;
        history.stationedAtMs = previous.stationedAtMs;
        history.lastStationSampleAtMs = previous.lastStationSampleAtMs;
        history.expiresAtMs = previous.expiresAtMs;
        history.normalTtlSkippedForActiveMovement =
            previous.normalTtlSkippedForActiveMovement;
        history.latestValidationSnapshotId = previous.latestValidationSnapshotId;
        history.validatedSnapshotId = previous.validatedSnapshotId;
        history.activationSnapshotId = previous.activationSnapshotId;
        history.targetHash = previous.targetHash;
        history.latestValidationTargetHash = previous.latestValidationTargetHash;
        history.validatedTargetHash = previous.validatedTargetHash;
        history.activationTargetHash = previous.activationTargetHash;
        history.selectedProfileKey = previous.selectedProfileKey;
        history.targetResourceName = previous.targetResourceName;
        history.targetResourceType = previous.targetResourceType;
        history.targetZoneName = previous.targetZoneName;
        history.status = previous.status;
        history.clearReason = reason;
        history.latestValidationStatus = previous.pathValidationStatus;
        history.latestPathTrustStatus = previous.pathValidationTrustStatus;
        history.activationValidationStatus = previous.activationPathValidationStatus;
	        history.activationPathTrustStatus = previous.activationPathTrustStatus;
	        history.latestPathDistance = previous.latestPathDistance;
	        history.activationPathDistance = previous.activationPathDistance;
	        history.validationMismatchReason = previous.latestValidationMismatchReason;
        history.lifecycleDowngradePrevented = previous.lifecycleDowngradePrevented;
        history.rebalanceReason = previous.rebalanceReason;
        history.stationSampleCount = previous.stationSampleCount;
        history.stationYieldQuantity = previous.stationYieldQuantity;
        history.stationDurationSeconds = previous.stationDurationSeconds;
        history.yielded = reason == "sampleComplete" ||
            previous.status == "sample_complete" ||
            previous.sampleFinishedAtMs > 0;
	        uint64 timeoutAgeSeconds = 0;
	        uint64 timeoutSeconds = 0;
	        MinerIntelligentTargetAssignment timeoutPrevious = previous;
	        getMinerIntelligentAssignmentTimeoutReason(
	            timeoutPrevious, now, timeoutAgeSeconds, timeoutSeconds, false);
	        if (reason == "movementArrivalTimeout" ||
	                previous.status == "activation_started") {
	            history.movementAgeSeconds = timeoutAgeSeconds;
	            history.movementTimeoutSeconds = timeoutSeconds;
	        } else if (reason == "sampleTimeout" ||
	                previous.status == "sample_started") {
	            history.sampleAgeSeconds = timeoutAgeSeconds;
	            history.sampleTimeoutSeconds = timeoutSeconds;
	        }

	        Locker historyLocker(&recentMinerAssignmentHistoryMutex);
        recentMinerAssignmentHistory.add(history);

        while (recentMinerAssignmentHistory.size() > 32)
            recentMinerAssignmentHistory.remove(0);
    }

	    if (hadAssignment && minerIntelligentTargetingAssignmentLogLifecycle) {
	        uint64 now = System::getMiliTime();
	        uint64 ageSeconds = previous.createdAtMs > 0 && now > previous.createdAtMs ?
	            (now - previous.createdAtMs) / 1000 : 0;
	        uint64 timeoutAgeSeconds = 0;
	        uint64 timeoutSeconds = 0;
	        MinerIntelligentTargetAssignment timeoutPrevious = previous;
	        getMinerIntelligentAssignmentTimeoutReason(
	            timeoutPrevious, now, timeoutAgeSeconds, timeoutSeconds, false);
	        String validatedAge = "not_set";
        String queuedAge = "not_set";
        String activatedAge = "not_set";
        String sampleStartedAge = "not_set";
        String sampleFinishedAge = "not_set";

        if (previous.createdAtMs > 0) {
            if (previous.validatedAtMs > previous.createdAtMs)
                validatedAge =
                    String::valueOf((previous.validatedAtMs - previous.createdAtMs) / 1000);

            if (previous.queuedAtMs > previous.createdAtMs)
                queuedAge =
                    String::valueOf((previous.queuedAtMs - previous.createdAtMs) / 1000);

            if (previous.activatedAtMs > previous.createdAtMs)
                activatedAge =
                    String::valueOf((previous.activatedAtMs - previous.createdAtMs) / 1000);

            if (previous.sampleStartedAtMs > previous.createdAtMs)
                sampleStartedAge =
                    String::valueOf((previous.sampleStartedAtMs - previous.createdAtMs) / 1000);

            if (previous.sampleFinishedAtMs > previous.createdAtMs)
                sampleFinishedAge =
                    String::valueOf((previous.sampleFinishedAtMs - previous.createdAtMs) / 1000);
        }

        info(String("MinerIntelligentTargetAssignment miner=") +
             String::valueOf(minerID) +
             " action=cleared" +
             " clearReason=" + reason +
             " assignmentGenerationId=" +
                String::valueOf(previous.assignmentGenerationId) +
             " targetHash=" +
                (previous.targetHash.isEmpty() ?
                    String("none") : previous.targetHash) +
             " latestValidationSnapshotId=" +
                String::valueOf(previous.latestValidationSnapshotId) +
             " validatedSnapshotId=" +
                String::valueOf(previous.validatedSnapshotId) +
             " activationSnapshotId=" +
                String::valueOf(previous.activationSnapshotId) +
             " lifecycleStatus=" +
                (previous.status.isEmpty() ?
                    String("none") : previous.status) +
             " selectedProfile=" +
                (previous.selectedProfileKey.isEmpty() ?
                    String("none") : previous.selectedProfileKey) +
             " targetResource=" +
                (previous.targetResourceName.isEmpty() ?
                    String("none") : previous.targetResourceName) +
	             " ageSeconds=" + String::valueOf(ageSeconds) +
	             " lifecycleTimeoutAgeSeconds=" +
	                String::valueOf(timeoutAgeSeconds) +
	             " lifecycleTimeoutSeconds=" +
	                String::valueOf(timeoutSeconds) +
	             " normalTtlSkippedForActiveMovement=" +
	                (previous.normalTtlSkippedForActiveMovement ?
	                    String("true") : String("false")) +
	             " activationPathDistance=" +
	                String::valueOf(Math::getPrecision(
	                    previous.activationPathDistance, 1)) +
	             " validatedAgeSeconds=" + validatedAge +
             " queuedAgeSeconds=" + queuedAge +
             " activatedAgeSeconds=" + activatedAge +
             " sampleStartedAgeSeconds=" + sampleStartedAge +
             " sampleFinishedAgeSeconds=" + sampleFinishedAge +
             " pathValidationStatus=" +
                (previous.pathValidationStatus.isEmpty() ?
                    String("none") : previous.pathValidationStatus) +
             " pathTrustStatus=" +
                (previous.pathValidationTrustStatus.isEmpty() ?
                    String("none") : previous.pathValidationTrustStatus) +
             " activationValidationStatus=" +
                (previous.activationPathValidationStatus.isEmpty() ?
                    String("none") : previous.activationPathValidationStatus) +
             " activationPathTrustStatus=" +
                (previous.activationPathTrustStatus.isEmpty() ?
                    String("none") : previous.activationPathTrustStatus) +
             " latestValidationMismatchReason=" +
                (previous.latestValidationMismatchReason.isEmpty() ?
                    String("none") : previous.latestValidationMismatchReason) +
             " lastActivationResult=" +
                (previous.lastActivationResult.isEmpty() ?
                    String("none") : previous.lastActivationResult) +
             " lastFailureReason=" +
                (previous.lastFailureReason.isEmpty() ?
                    String("none") : previous.lastFailureReason) +
             " mode=" + mode, true);
    }
}

void SimPlayerManager::logMinerIntelligentTargetingDecisions() {
    Vector<MinerIntelligentTargetingMinerSnapshot> miners;
    int controllerCount = controllers.size();

    for (int controllerIndex = 0; controllerIndex < controllerCount; ++controllerIndex) {
        uint64 controllerKey = controllers.getKey(controllerIndex);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr || dynamic_cast<SimMinerController*>(ctrl.get()) == nullptr)
            continue;

        ManagedReference<AiAgent*> agent = ctrl->getAgent();

        if (agent == nullptr)
            continue;

        MinerIntelligentTargetingMinerSnapshot miner;

        {
            Locker agentLocker(agent);
            miner.zone = agent->getZone();

            if (miner.zone != nullptr) {
                miner.objectID = agent->getObjectID();
                miner.zoneName = miner.zone->getZoneName();
                miner.position = agent->getWorldPosition();
                miner.inNavmesh = agent->isInNavMesh();
                miner.dead = agent->isDead();
                miner.incapacitated = agent->isIncapacitated();
                miner.inCombat = agent->isInCombat();
            }
        }

        if (miner.isValid())
            miners.add(miner);
    }

    if (miners.size() == 0)
        return;

    for (int i = 0; i < miners.size(); ++i) {
        for (int j = i + 1; j < miners.size(); ++j) {
            if (miners.get(j).objectID >= miners.get(i).objectID)
                continue;

            MinerIntelligentTargetingMinerSnapshot swap = miners.get(i);
            miners.set(i, miners.get(j));
            miners.set(j, swap);
        }
    }

    resetNavAreaDensitySelectionDiagnostics(
        navAreaDensitySelectionEnabled,
        navAreaDensitySelectionShadowMode,
        navAreaMaxSampleAttemptsPerCycle,
        navAreaMaxPathValidationsPerCycle);

    int evaluatedLimit = miners.size();

    if (evaluatedLimit > minerIntelligentTargetingMaxActiveMiners)
        evaluatedLimit = minerIntelligentTargetingMaxActiveMiners;

    Vector<String> conceptualResourceNames;
    Vector<uint64> conceptualAmounts;
    collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

    Vector<ResourceIntelligenceEntry> entries;
    String snapshotError;
    bool resourceSnapshotAvailable =
        collectResourceIntelligenceSnapshot(entries, snapshotError);
    Vector<DemandProfileDefinition> profiles = createDemandProfileDefinitions();
    VectorMap<String, uint64> marketQuantities;

    if (demandWeightedMinerPlanSimulationIncludeMarketSupply) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            String profileKey = profiles.get(profileIndex).key;

            if (marketSupplyProfileQuantities.contains(profileKey))
                marketQuantities.put(
                    profileKey, marketSupplyProfileQuantities.get(profileKey));
        }
    }

    Vector<DemandStateSimulationResult> pressureResults;
    int profilesDisabled = 0;
    int profilesInactivePhase = 0;
    int profilesBelowPressure = 0;
    int profilesNoEligibleResource = 0;

    if (resourceSnapshotAvailable && entries.size() > 0 &&
            demandWeightedMinerPlanSimulationEnabled) {
        buildDemandWeightedPressureResultsForMiners(
            entries,
            profiles,
            conceptualResourceNames,
            conceptualAmounts,
            marketQuantities,
            demandWeightedMinerPlanSimulationProfileEnabled,
            demandWeightedMinerPlanSimulationDesiredReserve,
            demandWeightedMinerPlanSimulationLowStockThreshold,
            demandWeightedMinerPlanSimulationCriticalStockThreshold,
            demandWeightedMinerPlanSimulationServerPhase,
            demandWeightedMinerPlanSimulationShortageWeight,
            demandWeightedMinerPlanSimulationActiveOpportunityWeight,
            demandWeightedMinerPlanSimulationSurplusDampening,
            demandWeightedMinerPlanSimulationMinimumPressureThreshold,
            pressureResults,
            profilesDisabled,
            profilesInactivePhase,
            profilesBelowPressure,
            profilesNoEligibleResource);
    }

    VectorMap<String, int> assignmentsByProfile;
    int wouldActivateCount = 0;
    int fallbackCount = 0;
    int noPlanCount = 0;
    int noDensityTargetCount = 0;
    int pathRejectedCount = 0;
    int rollbackHeldCount = 0;
	int actualActivationCount = 0;
	int activationFallbackCount = 0;
	int cappedCount = miners.size() - evaluatedLimit;
	int activationControlledSkipCount = 0;
	int assignmentMismatchCount = 0;
	int pathTrustRejectedCount = 0;
	int activeIntelligentMinerCount = countActiveMinerIntelligentAssignments();
	bool activationDisabledForInterval = false;

    for (int minerIndex = 0; minerIndex < evaluatedLimit; ++minerIndex) {
        MinerIntelligentTargetingMinerSnapshot miner = miners.get(minerIndex);
        DemandWeightedMinerCandidate selected;
        ResourceIntelligenceEntry selectedResource;
        DemandStateSimulationResult selectedResult;
        String selectedProfileKey = "none";
        String demandState = "none";
        String targetResourceName = "none";
        String targetResourceType = "none";
        String targetSource = "none";
        String fallbackReason = "none";
        String densityTargetStatus = "not_checked";
        String pathValidationStatus = "not_checked";
        String pathRejectReason = "none";
        String pathTrustStatus = "not_checked";
        String assignmentReason = "none";
        String assignmentStatus = "none";
        String assignmentClearReason = "none";
        uint64 assignmentAgeSeconds = 0;
        bool assignmentMatchesValidation = false;
        bool selectedSamePlanet = false;
        bool acceptedDensityTarget = false;
        bool pathValidationValid = false;
        bool wouldActivate = false;
		bool actualActivation = false;
		bool hasSelectedPlan = false;
		bool usedCachedAssignment = false;
		bool activationControlledSkip = false;
		bool activationFailedOrFallback = false;
		String activationResult = "not_attempted";
        MinerDensityTargetCandidate selectedDensityTarget;
        MinerIntelligentTargetAssignment cachedAssignment;
        uint64 nowMs = System::getMiliTime();

	        if (minerIntelligentTargetingAssignmentEnabled &&
	                getMinerIntelligentTargetAssignment(
	                    miner.objectID, cachedAssignment)) {
	            uint64 timeoutAgeSeconds = 0;
	            uint64 timeoutSeconds = 0;
	            String timeoutReason =
	                getMinerIntelligentAssignmentTimeoutReason(
	                    cachedAssignment, nowMs, timeoutAgeSeconds,
	                    timeoutSeconds, true);

	            if (!timeoutReason.isEmpty()) {
	                assignmentClearReason = timeoutReason;
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID,
	                    assignmentClearReason,
	                    minerIntelligentTargetingMode);
	            } else {
	                if (cachedAssignment.normalTtlSkippedForActiveMovement)
	                    putMinerIntelligentTargetAssignment(cachedAssignment);

	                if (minerIntelligentTargetingAssignmentClearOnZoneChange &&
		                    cachedAssignment.targetZoneName != miner.zoneName) {
	                    assignmentClearReason = "zoneChanged";
	                    clearMinerIntelligentTargetAssignment(
	                        miner.objectID,
	                        assignmentClearReason,
	                        minerIntelligentTargetingMode);
	                } else if (minerIntelligentTargetingAssignmentClearOnIncapOrDeath &&
	                        (miner.dead || miner.incapacitated)) {
	                    assignmentClearReason = miner.dead ? "dead" : "incapacitated";
	                    clearMinerIntelligentTargetAssignment(
	                        miner.objectID,
	                        assignmentClearReason,
	                        minerIntelligentTargetingMode);
	                } else if (minerIntelligentTargetingAssignmentClearOnCombat &&
	                        miner.inCombat) {
	                    assignmentClearReason = "combat";
	                    clearMinerIntelligentTargetAssignment(
	                        miner.objectID,
	                        assignmentClearReason,
	                        minerIntelligentTargetingMode);
	                } else {
	                    usedCachedAssignment =
	                        minerIntelligentTargetingAssignmentReplaceOnlyWhenExpiredOrInvalid;
	                }
	            }
	        }

        if (usedCachedAssignment) {
            bool repairedAssignmentIdentity = false;

            if (cachedAssignment.assignmentGenerationId == 0) {
                Locker assignmentIdLocker(&minerIntelligentTargetingAssignmentMutex);
                cachedAssignment.assignmentGenerationId =
                    nextMinerAssignmentGenerationId++;
                repairedAssignmentIdentity = true;
            }

            if (cachedAssignment.targetHash.isEmpty()) {
                cachedAssignment.targetHash =
                    buildMinerAssignmentTargetHash(cachedAssignment);
                repairedAssignmentIdentity = true;
            }

            if (repairedAssignmentIdentity)
                putMinerIntelligentTargetAssignment(cachedAssignment);

			hasSelectedPlan = true;
			selectedProfileKey = cachedAssignment.selectedProfileKey;
			assignmentReason = cachedAssignment.assignmentReason.isEmpty() ?
				String("retainedAssignment") :
				cachedAssignment.assignmentReason;
            demandState = cachedAssignment.demandState;
            targetResourceName = cachedAssignment.targetResourceName;
            targetResourceType = cachedAssignment.targetResourceType;
            targetSource = cachedAssignment.targetSource;
            selectedSamePlanet = cachedAssignment.targetZoneName == miner.zoneName;
            acceptedDensityTarget =
                cachedAssignment.densityTargetStatus == "accepted";
            densityTargetStatus = cachedAssignment.densityTargetStatus;
            pathTrustStatus = cachedAssignment.pathValidationTrustStatus.isEmpty() ?
                String("not_checked") : cachedAssignment.pathValidationTrustStatus;
            assignmentStatus = cachedAssignment.status.isEmpty() ?
                String("candidate") : cachedAssignment.status;
            assignmentAgeSeconds =
                cachedAssignment.createdAtMs > 0 && nowMs > cachedAssignment.createdAtMs ?
                (nowMs - cachedAssignment.createdAtMs) / 1000 : 0;
            selectedResult.profileKey = cachedAssignment.selectedProfileKey;
            selectedResult.state = cachedAssignment.demandState;
            selectedResult.pressureScore = cachedAssignment.pressureScore;
            selectedDensityTarget.x = cachedAssignment.targetX;
            selectedDensityTarget.y = cachedAssignment.targetY;
            selectedDensityTarget.z = cachedAssignment.targetZ;
            selectedDensityTarget.density = cachedAssignment.targetDensity;
            selectedDensityTarget.searchRadius = 1;
        } else if (!demandWeightedMinerPlanSimulationEnabled) {
            fallbackReason = "noDemandWeightedPlan";
        } else if (!resourceSnapshotAvailable) {
            fallbackReason = "resourceSnapshotUnavailable";
        } else if (entries.size() == 0) {
            fallbackReason = "noActiveResourceSnapshot";
        } else if (pressureResults.size() == 0) {
            if (profilesBelowPressure > 0) {
                fallbackReason = "belowMinimumPressure";
            } else if (profilesNoEligibleResource > 0) {
                fallbackReason = "noEligibleActiveResource";
            } else if (profilesInactivePhase > 0) {
                fallbackReason = "inactiveServerPhase";
            } else if (profilesDisabled > 0) {
                fallbackReason = "disabledProfile";
            } else {
                fallbackReason = "noDemandWeightedPlan";
            }
        } else {
            DemandWeightedPlanSelection demandSelection =
                selectDemandWeightedMinerPlanForValidation(
                    entries,
                    profiles,
                    pressureResults,
                    assignmentsByProfile,
                    miner.zoneName,
                    demandWeightedMinerPlanSimulationSamePlanetBonus,
                    demandWeightedMinerPlanSimulationTravelPenalty,
                    demandWeightedMinerPlanSimulationMaxMinersPerProfile,
                    demandWeightedMinerPlanSimulationStrongPressureRatio);

            if (!demandSelection.valid) {
                fallbackReason = "allCandidatesCappedWithoutStrongPressure";
            } else {
                selected = demandSelection.selected;
                selectedResult = demandSelection.selectedResult;
                selectedResource = demandSelection.selectedResource;
                hasSelectedPlan = true;
                selectedProfileKey = selectedResult.profileKey;
                demandState = selectedResult.state;
                targetResourceName = selectedResource.name;
                targetResourceType = selectedResource.type;
                selectedSamePlanet = selected.target.samePlanet;
                targetSource = "demand_weighted_plan";
                assignmentReason = demandSelection.assignmentReason;
            }
        }

        if (hasSelectedPlan) {
            if (!selectedSamePlanet) {
                densityTargetStatus = "wrongPlanet";
            } else if (!usedCachedAssignment) {
                MinerDensityTargetCandidate densityTarget;
                MinerDensityTargetDiagnostics densityDiagnostics;
                acceptedDensityTarget = findMinerDensityTarget(
                    miner.objectID,
                    selectedProfileKey,
                    targetSource,
                    selectedResource,
                    miner.zone,
                    miner.position,
                    minerDensityTargetSimulationSearchRadii,
                    minerDensityTargetSimulationSamplesPerRadius,
                    minerDensityTargetSimulationMinAcceptableDensity,
                    minerDensityTargetSimulationRequireNavmesh,
                    miner.inNavmesh,
                    minerDensityTargetSimulationMaxPathCheckAttempts,
                    minerDensityTargetSimulationDistancePenaltyPerMeter,
                    reachabilityMemoryEnabled,
                    reachabilityCandidatePreferenceEnabled,
                    reachabilityBucketSizeMeters,
                    reachabilityMinAttemptsBeforePenalty,
                    reachabilityVerifiedPathScoreBonus,
                    reachabilitySampleCompleteScoreBonus,
                    reachabilityRepeatedFailurePenalty,
                    reachabilityLongDistancePenalty512Plus,
                    reachabilityMemoryTtlSeconds,
                    reachabilityMaxMemoryRows,
                    densityTarget,
                    densityDiagnostics);

                MinerDensityTargetCandidate navAreaTarget;
                String navAreaSelectionMode;
                String navAreaReason;
                String navAreaName;
                String navAreaRole;
                bool navAreaFound = evaluateNavAreaDensitySelection(
                    miner.objectID,
                    selectedProfileKey,
                    selectedResource,
                    miner.zone,
                    miner.position,
                    navAreaDensitySelectionEnabled,
                    navAreaDensitySelectionShadowMode,
                    navAreaSampleCacheTtlSeconds,
                    navAreaMaxSamplesPerArea,
                    navAreaMaxSampleAttemptsPerCycle,
                    navAreaMaxPathValidationsPerCycle,
                    navAreaAvoidGenericInteriors,
                    navAreaPreferCityAndPoiRegions,
                    minerDensityTargetSimulationMinAcceptableDensity,
                    minerDensityTargetSimulationDistancePenaltyPerMeter,
                    navAreaTarget,
                    navAreaSelectionMode,
                    navAreaReason,
                    navAreaName,
                    navAreaRole);

                if (navAreaFound) {
                    bool activeNavAreaSelection =
                        navAreaDensitySelectionEnabled &&
                        !navAreaDensitySelectionShadowMode;
                    bool wouldSelectDifferent =
                        !densityTarget.isValid() ||
                        Vector3(
                            densityTarget.x,
                            densityTarget.y,
                            densityTarget.z).distanceTo(
                                Vector3(
                                    navAreaTarget.x,
                                    navAreaTarget.y,
                                    navAreaTarget.z)) > 5.f;

                    info(String("NavAreaDensitySelection miner=") +
                        String::valueOf(miner.objectID) +
                        " zone=" + miner.zoneName +
                        " profile=" + selectedProfileKey +
                        " resource=" + selectedResource.name +
                        " type=" + selectedResource.type +
                        " sourceArea=" + navAreaName +
                        " sourceRole=" + navAreaRole +
                        " densitySelectionMode=" + navAreaSelectionMode +
                        " activeSelection=" +
                            (activeNavAreaSelection ?
                                String("true") : String("false")) +
                        " wouldSelectDifferent=" +
                            (wouldSelectDifferent ?
                                String("true") : String("false")) +
                        " navTarget=(x:" +
                            String::valueOf(
                                Math::getPrecision(navAreaTarget.x, 1)) +
                        ",y:" +
                            String::valueOf(
                                Math::getPrecision(navAreaTarget.y, 1)) +
                        ",z:" +
                            String::valueOf(
                                Math::getPrecision(navAreaTarget.z, 1)) +
                        ")" +
                        " navDensity=" +
                            String::valueOf(
                                Math::getPrecision(navAreaTarget.density, 3)) +
                        " navScore=" +
                            String::valueOf(
                                Math::getPrecision(
                                    navAreaTarget.adjustedScore, 1)) +
                        " reason=" + navAreaReason +
                        " mode=" +
                            (activeNavAreaSelection ?
                                String("active") : String("shadow-only")),
                        true);

                    if (activeNavAreaSelection) {
                        densityTarget = navAreaTarget;
                        acceptedDensityTarget = true;
                    }
                }

                densityTargetStatus = acceptedDensityTarget ?
                    String("accepted") : densityDiagnostics.rejectReason;

                if (acceptedDensityTarget)
                    selectedDensityTarget = densityTarget;
            }

            if (!usedCachedAssignment && acceptedDensityTarget &&
                    minerIntelligentTargetingAssignmentEnabled &&
                    targetSource == "demand_weighted_plan") {
                MinerIntelligentTargetAssignment assignment;
                assignment.minerID = miner.objectID;
                {
                    Locker assignmentIdLocker(&minerIntelligentTargetingAssignmentMutex);
                    assignment.assignmentGenerationId =
                        nextMinerAssignmentGenerationId++;
                }
                assignment.createdAtMs = nowMs;
	                assignment.updatedAtMs = nowMs;
	                assignment.expiresAtMs = nowMs +
	                    static_cast<uint64>(
	                        minerIntelligentTargetingCandidateAssignmentTtlSeconds) * 1000;
                assignment.targetSource = targetSource;
                assignment.selectedProfileKey = selectedProfileKey;
                assignment.assignmentReason = assignmentReason;
                assignment.demandState = demandState;
                assignment.pressureScore = selectedResult.pressureScore;
                assignment.targetResourceName = targetResourceName;
                assignment.targetResourceType = targetResourceType;
                assignment.targetZoneName = miner.zoneName;
                assignment.targetX = selectedDensityTarget.x;
                assignment.targetY = selectedDensityTarget.y;
                assignment.targetZ = selectedDensityTarget.z;
                assignment.targetDensity = selectedDensityTarget.density;
                assignment.targetDirectDistance =
                    miner.position.distanceTo(
                        Vector3(
                            selectedDensityTarget.x,
                            selectedDensityTarget.y,
                            selectedDensityTarget.z));
                assignment.targetHash = buildMinerAssignmentTargetHash(assignment);
                assignment.densityTargetStatus = densityTargetStatus;
                assignment.pathValidationStatus = "not_checked";
                assignment.pathValidationTrustStatus = "not_checked";
                assignment.currentPathValidationStatus = "not_checked";
                assignment.currentPathTrustStatus = "not_checked";
                assignment.latestValidationMismatchReason = "not_checked";
                assignment.validatedPathValidationStatus = "not_checked";
                assignment.validatedPathTrustStatus = "not_checked";
                assignment.activationPathValidationStatus = "not_checked";
                assignment.activationPathTrustStatus = "not_checked";
                assignment.pathValidationMatched = false;
                assignment.status = "candidate";
                putMinerIntelligentTargetAssignment(assignment);
                recordReachabilityCandidateGenerated(assignment);

                cachedAssignment = assignment;
                usedCachedAssignment = true;
                assignmentStatus = "candidate";
                assignmentAgeSeconds = 0;

                if (minerIntelligentTargetingAssignmentLogLifecycle) {
                    info(String("MinerIntelligentTargetAssignment miner=") +
                         String::valueOf(miner.objectID) +
                         " action=created" +
                         " targetSource=" + targetSource +
                         " assignmentGenerationId=" +
                            String::valueOf(assignment.assignmentGenerationId) +
                         " targetHash=" + assignment.targetHash +
                         " selectedProfile=" + selectedProfileKey +
                         " targetResource=" + targetResourceName +
                         " targetType=" + targetResourceType +
                         " targetZone=" + miner.zoneName +
                         " x=" +
                            String::valueOf(Math::getPrecision(
                                selectedDensityTarget.x, 1)) +
                         " y=" +
                            String::valueOf(Math::getPrecision(
                                selectedDensityTarget.y, 1)) +
                         " z=" +
                            String::valueOf(Math::getPrecision(
                                selectedDensityTarget.z, 1)) +
                         " densityTargetStatus=" + densityTargetStatus +
                         " pathValidationStatus=not_checked" +
                         " pathTrustStatus=not_checked" +
	                         " ttlSeconds=" +
	                            String::valueOf(
	                                minerIntelligentTargetingCandidateAssignmentTtlSeconds) +
	                         " mode=" + minerIntelligentTargetingMode, true);
                }
			} else if (usedCachedAssignment &&
					minerIntelligentTargetingAssignmentLogLifecycle) {
					uint64 timeoutAgeSeconds = 0;
					uint64 timeoutSeconds = 0;
					MinerIntelligentTargetAssignment retainedTimeoutAssignment =
						cachedAssignment;
					getMinerIntelligentAssignmentTimeoutReason(
						retainedTimeoutAssignment, nowMs, timeoutAgeSeconds,
						timeoutSeconds, false);
					uint64 remainingSeconds =
						timeoutSeconds > timeoutAgeSeconds ?
						timeoutSeconds - timeoutAgeSeconds : 0;
				uint64 retainedNearExpirySeconds =
					static_cast<uint64>(
						minerIntelligentTargetingIntervalSeconds > 0 ?
						minerIntelligentTargetingIntervalSeconds : 60);
				bool retainedNearExpiry =
					remainingSeconds <= retainedNearExpirySeconds;

				if (minerIntelligentTargetingAssignmentLogRetained ||
						retainedNearExpiry) {
					info(String("MinerIntelligentTargetAssignment miner=") +
						 String::valueOf(miner.objectID) +
						 " action=retained" +
                         " assignmentGenerationId=" +
                            String::valueOf(cachedAssignment.assignmentGenerationId) +
                         " targetHash=" + cachedAssignment.targetHash +
						 " selectedProfile=" + selectedProfileKey +
						 " targetResource=" + targetResourceName +
						 " ageSeconds=" + String::valueOf(assignmentAgeSeconds) +
						 " remainingSeconds=" +
							String::valueOf(remainingSeconds) +
						 " nearExpiry=" +
							(retainedNearExpiry ? String("true") : String("false")) +
						 " pathValidationStatus=" +
							(cachedAssignment.pathValidationStatus.isEmpty() ?
								String("not_checked") :
								cachedAssignment.pathValidationStatus) +
						 " pathTrustStatus=" +
							(cachedAssignment.pathValidationTrustStatus.isEmpty() ?
								String("not_checked") :
								cachedAssignment.pathValidationTrustStatus) +
                         " activationSnapshotId=" +
                            String::valueOf(cachedAssignment.activationSnapshotId) +
                         " activationValidationStatus=" +
                            (cachedAssignment.activationPathValidationStatus.isEmpty() ?
                                String("none") :
                                cachedAssignment.activationPathValidationStatus) +
						 " mode=" + minerIntelligentTargetingMode, true);
				}
			}

            MinerPathValidationSnapshot pathSnapshot;

            if (!minerIntelligentTargetingRequireValidPath) {
                pathValidationStatus = "not_required";
                pathTrustStatus = "not_required";
                pathValidationValid = true;
            } else if (getMinerPathValidationSnapshot(
                    miner.objectID, pathSnapshot)) {
                if (pathSnapshot.targetHash.isEmpty())
                    pathSnapshot.targetHash = buildMinerAssignmentTargetHash(pathSnapshot);

                pathTrustStatus = pathSnapshot.pathTrustStatus.isEmpty() ?
                    (pathSnapshot.pathFound ? String("verifiedPath") :
                        (pathSnapshot.rejectReason.isEmpty() ?
                            String("untrusted") : pathSnapshot.rejectReason)) :
                    pathSnapshot.pathTrustStatus;
                bool snapshotMatches = usedCachedAssignment ?
                    minerValidationSnapshotMatchesAssignment(
                        cachedAssignment, pathSnapshot) :
                    (pathSnapshot.targetSource == "demand_weighted_plan" &&
                        pathSnapshot.zoneName == miner.zoneName &&
                        pathSnapshot.profileKey == selectedProfileKey &&
                        pathSnapshot.resourceName == targetResourceName &&
                        pathSnapshot.resourceType == targetResourceType);
                bool snapshotCoordinateMatches = false;

                if (snapshotMatches && selectedDensityTarget.isValid()) {
                    float dx = pathSnapshot.targetX - selectedDensityTarget.x;
                    float dy = pathSnapshot.targetY - selectedDensityTarget.y;
                    float dz = pathSnapshot.targetZ - selectedDensityTarget.z;
                    snapshotCoordinateMatches = (dx * dx + dy * dy + dz * dz) <= 4.f;
                }

                uint64 now = System::getMiliTime();
                uint64 maxAgeMs =
                    static_cast<uint64>(
                        minerPathValidationSimulationIntervalSeconds > 0 ?
                        minerPathValidationSimulationIntervalSeconds : 300) *
                    3000;

                if (!snapshotMatches) {
                    pathValidationStatus = "target_mismatch";
                    if (usedCachedAssignment &&
                            cachedAssignment.assignmentGenerationId > 0 &&
                            pathSnapshot.assignmentGenerationId > 0 &&
                            cachedAssignment.assignmentGenerationId !=
                                pathSnapshot.assignmentGenerationId) {
                        pathRejectReason = "assignmentGenerationMismatch";
                    } else if (usedCachedAssignment &&
                            !cachedAssignment.targetHash.isEmpty() &&
                            !pathSnapshot.targetHash.isEmpty() &&
                            cachedAssignment.targetHash != pathSnapshot.targetHash) {
                        pathRejectReason = "targetHashMismatch";
                    } else {
                        pathRejectReason = pathSnapshot.targetSource != "demand_weighted_plan" ?
                            String("targetSourceMismatch") :
                            (pathSnapshot.zoneName != miner.zoneName ?
                                String("zoneMismatch") :
                                String("profileResourceMismatch"));
                    }
                    pathTrustStatus = pathRejectReason;
                } else if (!snapshotCoordinateMatches) {
                    pathValidationStatus = "target_mismatch";
                    pathRejectReason = "densityTargetCoordinateMismatch";
                    pathTrustStatus = pathRejectReason;
                } else if (pathSnapshot.recordedAtMs > 0 &&
                        now > pathSnapshot.recordedAtMs + maxAgeMs) {
                    pathValidationStatus = "stale";
                    pathRejectReason = "stalePathValidation";
                    pathTrustStatus = pathRejectReason;
                } else if (pathSnapshot.pathFound) {
                    pathValidationStatus = "valid";
                    pathValidationValid = true;
                } else {
                    pathValidationStatus = "failed";
                    pathRejectReason = pathSnapshot.rejectReason;
                    pathTrustStatus = pathSnapshot.pathTrustStatus.isEmpty() ?
                        pathRejectReason : pathSnapshot.pathTrustStatus;
                }
            } else {
                pathValidationStatus = "not_available";
                pathTrustStatus = "not_available";
                pathRejectReason = minerPathValidationSimulationEnabled ?
                    String("noMatchingPathValidationSnapshot") :
                    String("pathValidationSimulationDisabled");
            }

            assignmentMatchesValidation =
                pathValidationStatus == "valid" && pathValidationValid &&
                pathTrustStatus == "verifiedPath";

			if (usedCachedAssignment &&
					minerIntelligentTargetingAssignmentEnabled &&
					cachedAssignment.isValid()) {
				String previousAssignmentStatus =
					cachedAssignment.status.isEmpty() ?
					String("candidate") : cachedAssignment.status;
				String previousPathValidationStatus =
					cachedAssignment.pathValidationStatus.isEmpty() ?
					String("not_checked") :
					cachedAssignment.pathValidationStatus;
				String previousPathTrustStatus =
					cachedAssignment.pathValidationTrustStatus.isEmpty() ?
					String("not_checked") :
					cachedAssignment.pathValidationTrustStatus;
				bool previousPathValidationMatched =
					cachedAssignment.pathValidationMatched;
                bool firstVerifiedValidation =
                    assignmentMatchesValidation &&
                    !cachedAssignment.reachabilityValidatedRecorded;
                bool firstObservedRejection =
                    !assignmentMatchesValidation &&
                    cachedAssignment.validatedAtMs == 0 &&
                    !cachedAssignment.reachabilityRejectedRecorded;
				cachedAssignment.updatedAtMs = nowMs;
	                cachedAssignment.latestValidationSnapshotId =
	                    pathSnapshot.validationSnapshotId;
	                cachedAssignment.latestValidationTargetHash =
	                    pathSnapshot.targetHash;
	                cachedAssignment.latestPathDistance =
	                    pathSnapshot.pathDistance;
	                cachedAssignment.latestValidationMismatchReason =
	                    pathRejectReason.isEmpty() ? String("none") : pathRejectReason;
				cachedAssignment.pathValidationStatus = pathValidationStatus;
				cachedAssignment.pathValidationTrustStatus = pathTrustStatus;
                cachedAssignment.currentPathValidationStatus = pathValidationStatus;
                cachedAssignment.currentPathTrustStatus = pathTrustStatus;
				cachedAssignment.pathValidationMatched = assignmentMatchesValidation;
                String nextValidationLifecycleStatus = assignmentMatchesValidation ?
					String("validated") : String("candidate");
                if (isMinerAssignmentLifecycleActiveStatus(cachedAssignment.status)) {
                    if (cachedAssignment.status != nextValidationLifecycleStatus)
                        cachedAssignment.lifecycleDowngradePrevented = true;
                } else {
				    cachedAssignment.status = nextValidationLifecycleStatus;
                }
				if (assignmentMatchesValidation && cachedAssignment.validatedAtMs == 0)
					cachedAssignment.validatedAtMs = nowMs;
                if (assignmentMatchesValidation) {
                    cachedAssignment.validatedSnapshotId =
                        pathSnapshot.validationSnapshotId;
                    cachedAssignment.validatedTargetHash =
                        pathSnapshot.targetHash;
	                    cachedAssignment.validatedPathValidationStatus = "valid";
	                    cachedAssignment.validatedPathTrustStatus = pathTrustStatus;
	                    cachedAssignment.validatedPathDistance =
	                        pathSnapshot.pathDistance;
	                }
                if (firstVerifiedValidation) {
                    cachedAssignment.reachabilityValidatedRecorded = true;
                } else if (firstObservedRejection) {
                    cachedAssignment.reachabilityRejectedRecorded = true;
                }
				putMinerIntelligentTargetAssignment(cachedAssignment);
                if (firstVerifiedValidation) {
                    recordReachabilityAssignmentValidated(cachedAssignment);
                } else if (firstObservedRejection) {
                    recordReachabilityCandidateRejected(
                        cachedAssignment,
                        pathRejectReason.isEmpty() ?
                            pathValidationStatus : pathRejectReason);
                }
				assignmentStatus = cachedAssignment.status;

				bool assignmentStatusChanged =
					previousAssignmentStatus != cachedAssignment.status;
				bool pathValidationStatusChanged =
					previousPathValidationStatus != cachedAssignment.pathValidationStatus;
				bool pathTrustStatusChanged =
					previousPathTrustStatus != cachedAssignment.pathValidationTrustStatus;
				bool pathValidationMatchChanged =
					previousPathValidationMatched !=
					cachedAssignment.pathValidationMatched;

				if (minerIntelligentTargetingAssignmentLogLifecycle &&
						(assignmentStatusChanged ||
						 pathValidationStatusChanged ||
						 pathTrustStatusChanged ||
						 pathValidationMatchChanged)) {
					info(String("MinerIntelligentTargetAssignment miner=") +
						 String::valueOf(miner.objectID) +
						 " action=updated" +
                         " assignmentGenerationId=" +
                            String::valueOf(cachedAssignment.assignmentGenerationId) +
                         " targetHash=" + cachedAssignment.targetHash +
                         " latestValidationSnapshotId=" +
                            String::valueOf(
                                cachedAssignment.latestValidationSnapshotId) +
                         " validatedSnapshotId=" +
                            String::valueOf(cachedAssignment.validatedSnapshotId) +
                         " activationSnapshotId=" +
                            String::valueOf(cachedAssignment.activationSnapshotId) +
						 " selectedProfile=" + selectedProfileKey +
						 " targetResource=" + targetResourceName +
						 " previousStatus=" + previousAssignmentStatus +
						 " status=" + cachedAssignment.status +
						 " previousPathValidationStatus=" +
							previousPathValidationStatus +
						 " pathValidationStatus=" +
							cachedAssignment.pathValidationStatus +
						 " previousPathTrustStatus=" +
							previousPathTrustStatus +
						 " pathTrustStatus=" +
							cachedAssignment.pathValidationTrustStatus +
						 " assignmentMatchesValidation=" +
							(cachedAssignment.pathValidationMatched ?
								String("true") : String("false")) +
                         " lifecycleDowngradePrevented=" +
                            (cachedAssignment.lifecycleDowngradePrevented ?
                                String("true") : String("false")) +
						 " mode=" + minerIntelligentTargetingMode, true);
				}
			}

            wouldActivate = true;

            if (minerIntelligentTargetingRequireDemandWeightedPlan &&
                    !hasSelectedPlan) {
                wouldActivate = false;
                fallbackReason = "noDemandWeightedPlan";
            }

            if (minerIntelligentTargetingRequireAcceptedDensityTarget &&
                    !acceptedDensityTarget) {
                wouldActivate = false;

                if (densityTargetStatus == "wrongPlanet") {
                    fallbackReason = "wrongPlanet";
                } else {
                    fallbackReason = "noAcceptedDensityTarget";
                }
            }

            if (minerIntelligentTargetingRequireValidPath &&
                    !pathValidationValid) {
                wouldActivate = false;

                if (pathValidationStatus == "failed") {
                    fallbackReason = "pathValidationFailed";
                } else if (pathValidationStatus == "not_available") {
                    fallbackReason = "pathValidationUnavailable";
                } else if (pathValidationStatus == "target_mismatch") {
                    fallbackReason = "pathValidationTargetMismatch";
                } else if (pathValidationStatus == "stale") {
                    fallbackReason = "pathValidationStale";
                } else {
                    fallbackReason = "pathValidationRejected";
                }
            } else if (minerIntelligentTargetingRequireValidPath &&
                    pathTrustStatus != "verifiedPath") {
                wouldActivate = false;
                fallbackReason = "pathValidationNotTrusted";
            }
        }

        if (!hasSelectedPlan) {
            noPlanCount++;
            wouldActivate = false;
        }

        int failureCount = 0;
        bool rollbackHeld = false;

        {
            Locker failureLocker(&minerIntelligentTargetingFailureMutex);

            if (wouldActivate) {
                minerIntelligentTargetingFailureCounts.put(miner.objectID, 0);
            } else {
                failureCount =
                    minerIntelligentTargetingFailureCounts.contains(miner.objectID) ?
                    minerIntelligentTargetingFailureCounts.get(miner.objectID) : 0;
                failureCount++;
                minerIntelligentTargetingFailureCounts.put(
                    miner.objectID, failureCount);
            }
        }

        if (!wouldActivate &&
                failureCount >= minerIntelligentTargetingRollbackOnFailureCount) {
            rollbackHeld = true;
            rollbackHeldCount++;
        }

        if (wouldActivate && rollbackHeld) {
            wouldActivate = false;
            fallbackReason = "rollbackHeld";
        }

		if (wouldActivate &&
				minerIntelligentTargetingMode == "limited" &&
				minerIntelligentTargetingLimitedActivationEnabled) {
			bool activationAllowed = true;
			bool activationAttempted = false;
            bool assignmentAlreadyActive =
                usedCachedAssignment && isMinerIntelligentAssignmentActive(cachedAssignment);

            if (minerIntelligentTargetingLimitedEmergencyDisabled) {
                activationAllowed = false;
                activationControlledSkip = true;
                activationResult = "activationEmergencyDisabled";
            } else if (!isMinerIntelligentTargetZoneAllowed(miner.zoneName)) {
                activationAllowed = false;
                activationControlledSkip = true;
                activationResult = "zoneNotAllowed";
                recordMinerIntelligentActivationHealthEvent("zoneSkip");
            } else if (activationDisabledForInterval) {
                activationAllowed = false;
                activationControlledSkip = true;
                activationResult = "activationDisabledForInterval";
            } else if (actualActivationCount >=
                    minerIntelligentTargetingLimitedMaxActivationsPerInterval) {
                activationAllowed = false;
                activationControlledSkip = true;
                activationResult = "activationCapReached";
            } else if (!assignmentAlreadyActive &&
                    activeIntelligentMinerCount >=
                    minerIntelligentTargetingLimitedMaxActiveIntelligentMiners) {
                activationAllowed = false;
                activationControlledSkip = true;
                activationResult = "activeIntelligentMinerCapReached";
                recordMinerIntelligentActivationHealthEvent("activeCapSkip");
            } else if (!assignmentAlreadyActive &&
                    isMinerIntelligentActivationOnCooldown(miner.objectID, nowMs)) {
                activationAllowed = false;
                activationControlledSkip = true;
                activationResult = "activationCooldown";
                recordMinerIntelligentActivationHealthEvent("cooldownSkip");
            } else if (!usedCachedAssignment || !cachedAssignment.isValid()) {
                activationAllowed = false;
                activationResult = "missingValidatedAssignment";
            } else if (cachedAssignment.validatedSnapshotId == 0 ||
                    cachedAssignment.validatedTargetHash !=
                        cachedAssignment.targetHash ||
                    cachedAssignment.validatedPathValidationStatus != "valid" ||
                    cachedAssignment.validatedPathTrustStatus != "verifiedPath") {
                activationAllowed = false;
                activationResult = "activationValidationUnavailable";
            } else if ((assignmentStatus != "validated" &&
                    assignmentStatus != "queued" &&
                    assignmentStatus != "activation_started" &&
                    assignmentStatus != "sample_started") ||
                    !assignmentMatchesValidation) {
                activationAllowed = false;
                activationResult = "assignmentNotValidated";
            } else if (cachedAssignment.pathValidationStatus != "valid" ||
                    !cachedAssignment.pathValidationMatched) {
                activationAllowed = false;
                activationResult = "pathValidationNotMatched";
            } else if (cachedAssignment.pathValidationTrustStatus != "verifiedPath") {
                activationAllowed = false;
                activationResult = "pathValidationNotTrusted";
            } else if (minerIntelligentTargetingLimitedRequireSamePlanet &&
                    cachedAssignment.targetZoneName != miner.zoneName) {
                activationAllowed = false;
                activationResult = "wrongPlanet";
            } else if (miner.dead || miner.incapacitated || miner.inCombat) {
                activationAllowed = false;
                activationResult = "controllerStateNotSafe";
            }

            if (activationAllowed) {
                Reference<SimPlayerController*> ctrl;

                if (controllers.contains(miner.objectID))
                    ctrl = controllers.get(miner.objectID);

                SimMinerController* minerController =
                    ctrl == nullptr ? nullptr :
                    dynamic_cast<SimMinerController*>(ctrl.get());

                if (minerController == nullptr) {
                    activationAllowed = false;
                    activationResult = "controllerUnavailable";
                } else {
                    String controllerResult;
                    Vector3 activationTarget(
                        cachedAssignment.targetX,
                            cachedAssignment.targetY,
                            cachedAssignment.targetZ);

                    activationAttempted = true;
                    recordMinerIntelligentActivationHealthEvent("attempted");
                    cachedAssignment.activationSnapshotId =
                        cachedAssignment.validatedSnapshotId;
                    cachedAssignment.activationTargetHash =
                        cachedAssignment.validatedTargetHash;
	                    cachedAssignment.activationPathValidationStatus =
	                        cachedAssignment.validatedPathValidationStatus;
	                    cachedAssignment.activationPathTrustStatus =
	                        cachedAssignment.validatedPathTrustStatus;
	                    cachedAssignment.activationPathDistance =
	                        cachedAssignment.validatedPathDistance > 0.f ?
	                            cachedAssignment.validatedPathDistance :
	                            cachedAssignment.latestPathDistance;
	                    actualActivation =
	                        minerController->requestIntelligentTargetAssignment(
                            cachedAssignment.selectedProfileKey,
                            cachedAssignment.targetResourceName,
                            cachedAssignment.targetResourceType,
                            cachedAssignment.targetZoneName,
                            activationTarget,
                            cachedAssignment.targetDensity,
                            cachedAssignment.expiresAtMs,
                            cachedAssignment.assignmentGenerationId,
                            cachedAssignment.targetHash,
                            cachedAssignment.activationSnapshotId,
                            cachedAssignment.activationPathValidationStatus,
                            cachedAssignment.activationPathTrustStatus,
                            minerIntelligentTargetingLimitedLogActivationLifecycle,
                            controllerResult);
                    activationResult = controllerResult;

                    if (actualActivation) {
                        if (controllerResult != "alreadyActive") {
                            actualActivationCount++;
                            activeIntelligentMinerCount++;
                            rememberMinerIntelligentActivation(miner.objectID, nowMs);
                            cachedAssignment.status = "queued";
                            if (cachedAssignment.queuedAtMs == 0)
                                cachedAssignment.queuedAtMs = nowMs;
                            if (!cachedAssignment.reachabilityActivatedRecorded) {
                                cachedAssignment.reachabilityActivatedRecorded = true;
                                recordReachabilityAssignmentActivated(cachedAssignment);
                            }
                        } else {
                            MinerIntelligentTargetAssignment refreshedAssignment;

                            if (getMinerIntelligentTargetAssignment(
                                    miner.objectID, refreshedAssignment)) {
                                cachedAssignment = refreshedAssignment;
                            }
                        }

                        cachedAssignment.updatedAtMs = nowMs;
                        cachedAssignment.lastActivationResult = activationResult;
                        putMinerIntelligentTargetAssignment(cachedAssignment);
                        assignmentStatus = cachedAssignment.status;
                    }
                }
            }

			if (!actualActivation) {
				if (activationControlledSkip) {
					activationControlledSkipCount++;
				} else {
					activationFallbackCount++;
					activationFailedOrFallback = true;
				}
                fallbackReason = activationResult;

                if (!activationControlledSkip &&
                        minerIntelligentTargetingLimitedDisableOnActivationFailure)
                    minerIntelligentTargetingLimitedEmergencyDisabled = true;

                if (!activationControlledSkip &&
                        minerIntelligentTargetingLimitedDisableOnFirstFailure)
                    activationDisabledForInterval = true;

                if (minerIntelligentTargetingLimitedLogActivationLifecycle) {
                    info(String("MinerIntelligentTargetActivation miner=") +
                         String::valueOf(miner.objectID) +
                         " action=" +
                            (activationControlledSkip ?
                                String("skipped") : String("fallback")) +
                         " fallbackReason=" + activationResult +
                         " assignmentGenerationId=" +
                            (usedCachedAssignment ?
                                String::valueOf(
                                    cachedAssignment.assignmentGenerationId) :
                                String("0")) +
                         " targetHash=" +
                            (usedCachedAssignment &&
                                !cachedAssignment.targetHash.isEmpty() ?
                                cachedAssignment.targetHash : String("none")) +
                         " activationSnapshotId=" +
                            (usedCachedAssignment ?
                                String::valueOf(
                                    cachedAssignment.activationSnapshotId) :
                                String("0")) +
                         " lifecycleStatus=" + assignmentStatus +
                         " latestValidationStatus=" + pathValidationStatus +
                         " activationValidationStatus=" +
                            (usedCachedAssignment &&
                                !cachedAssignment.activationPathValidationStatus.isEmpty() ?
                                cachedAssignment.activationPathValidationStatus :
                                String("none")) +
                         " selectedProfile=" + selectedProfileKey +
                         " targetResource=" + targetResourceName +
                         " targetType=" + targetResourceType +
                         " targetZone=" +
                            (usedCachedAssignment ?
                                cachedAssignment.targetZoneName :
                                miner.zoneName) +
                         " attempted=" +
                            (activationAttempted ?
                                String("true") : String("false")) +
                         " controlledSkip=" +
                            (activationControlledSkip ?
                                String("true") : String("false")) +
                         " mode=limited", true);
                }
            }
        }

        if (wouldActivate) {
            wouldActivateCount++;
        } else {
            fallbackCount++;
        }

        if (hasSelectedPlan &&
                minerIntelligentTargetingRequireAcceptedDensityTarget &&
                !acceptedDensityTarget) {
            noDensityTargetCount++;
        }

		if (hasSelectedPlan &&
				minerIntelligentTargetingRequireValidPath &&
				!pathValidationValid) {
			pathRejectedCount++;
		}

		bool assignmentMismatch =
			hasSelectedPlan &&
			minerIntelligentTargetingRequireValidPath &&
			pathValidationStatus == "target_mismatch";

		if (assignmentMismatch)
			assignmentMismatchCount++;

		bool pathTrustRejected =
			hasSelectedPlan &&
			minerIntelligentTargetingRequireValidPath &&
			pathValidationStatus == "valid" &&
			pathTrustStatus != "verifiedPath";

		if (pathTrustRejected)
			pathTrustRejectedCount++;

		bool shouldLogSwitchDecision =
			minerIntelligentTargetingLogVerboseSwitchDecisions ||
			wouldActivate ||
			actualActivation ||
			activationFailedOrFallback ||
			assignmentMismatch ||
			pathTrustRejected ||
			(hasSelectedPlan && minerIntelligentTargetingRequireValidPath &&
			 pathValidationStatus == "failed") ||
			rollbackHeld ||
			(minerIntelligentTargetingMode == "shadow" &&
			 !minerIntelligentTargetingLogDecisionSummary);

		if (shouldLogSwitchDecision) {
			info(String("MinerTargetingSwitchDecision miner=") +
				 String::valueOf(miner.objectID) +
				 " zone=" + miner.zoneName +
				 " mode=" + minerIntelligentTargetingMode +
				 " selectedProfile=" + selectedProfileKey +
				 " demandState=" + demandState +
				 " pressureScore=" +
					(hasSelectedPlan ?
						String::valueOf(Math::getPrecision(
							selectedResult.pressureScore, 1)) :
						String("0")) +
				 " targetResource=" + targetResourceName +
				 " targetType=" + targetResourceType +
				 " targetSource=" + targetSource +
				 " samePlanet=" +
					(selectedSamePlanet ? String("true") : String("false")) +
				 " travelRequired=" +
					(selectedSamePlanet ? String("false") : String("true")) +
				 " densityTargetStatus=" + densityTargetStatus +
				 " pathValidationStatus=" + pathValidationStatus +
				 " pathTrustStatus=" + pathTrustStatus +
				 " pathRejectReason=" + pathRejectReason +
				 " assignmentStatus=" + assignmentStatus +
                 " lifecycleStatus=" + assignmentStatus +
                 " assignmentGenerationId=" +
                    (usedCachedAssignment ?
                        String::valueOf(cachedAssignment.assignmentGenerationId) :
                        String("0")) +
                 " targetHash=" +
                    (usedCachedAssignment && !cachedAssignment.targetHash.isEmpty() ?
                        cachedAssignment.targetHash : String("none")) +
                 " latestValidationSnapshotId=" +
                    (usedCachedAssignment ?
                        String::valueOf(
                            cachedAssignment.latestValidationSnapshotId) :
                        String("0")) +
                 " validatedSnapshotId=" +
                    (usedCachedAssignment ?
                        String::valueOf(cachedAssignment.validatedSnapshotId) :
                        String("0")) +
                 " activationSnapshotId=" +
                    (usedCachedAssignment ?
                        String::valueOf(cachedAssignment.activationSnapshotId) :
                        String("0")) +
                 " activationValidationStatus=" +
                    (usedCachedAssignment &&
                        !cachedAssignment.activationPathValidationStatus.isEmpty() ?
                        cachedAssignment.activationPathValidationStatus :
                        String("none")) +
                 " activationPathTrustStatus=" +
                    (usedCachedAssignment &&
                        !cachedAssignment.activationPathTrustStatus.isEmpty() ?
                        cachedAssignment.activationPathTrustStatus :
                        String("none")) +
                 " lifecycleDowngradePrevented=" +
                    (usedCachedAssignment &&
                        cachedAssignment.lifecycleDowngradePrevented ?
                        String("true") : String("false")) +
				 " assignmentAgeSeconds=" + String::valueOf(assignmentAgeSeconds) +
				 " assignmentMatchesValidation=" +
					(assignmentMatchesValidation ?
						String("true") : String("false")) +
				 " assignmentTargetResource=" +
					(usedCachedAssignment ?
						cachedAssignment.targetResourceName : String("none")) +
				 " assignmentTargetType=" +
					(usedCachedAssignment ?
						cachedAssignment.targetResourceType : String("none")) +
				 " assignmentTargetZone=" +
					(usedCachedAssignment ?
						cachedAssignment.targetZoneName : String("none")) +
				 " assignmentPathTrustStatus=" +
					(usedCachedAssignment ?
						(cachedAssignment.pathValidationTrustStatus.isEmpty() ?
							String("none") :
							cachedAssignment.pathValidationTrustStatus) :
						String("none")) +
				 " assignmentClearReason=" +
					(assignmentClearReason.isEmpty() ?
						String("none") : assignmentClearReason) +
				 " wouldActivate=" +
					(wouldActivate ? String("true") : String("false")) +
				 " actualActivation=" +
					(actualActivation ? String("true") : String("false")) +
				 " activationResult=" + activationResult +
				 " fallbackReason=" +
					(wouldActivate ?
						(actualActivation ?
							String("none") :
							(minerIntelligentTargetingMode == "limited" ?
								(minerIntelligentTargetingLimitedActivationEnabled ?
									fallbackReason :
									String("limitedActivationDisabled")) :
								String("shadowMode"))) :
						fallbackReason) +
				 " fallbackToConceptualLoop=" +
					(minerIntelligentTargetingFallbackToConceptualLoop ?
						String("true") : String("false")) +
				 " rollbackHeld=" +
					(rollbackHeld ? String("true") : String("false")) +
				 " failureCount=" + String::valueOf(failureCount) +
				 " limitedActivationEnabled=" +
					(minerIntelligentTargetingLimitedActivationEnabled ?
						String("true") : String("false")) +
				 " assignmentReason=\"" + assignmentReason + "\"" +
				 " decisionBasis=demandWeightedMinerPlanSimulation" +
				 " diagnosticOnly=" +
					(actualActivation ? String("false") : String("true")) +
				 " mode=" +
					(actualActivation ? String("limited") : String("diagnostic-only")),
				 true);
		}
	}

    if (minerIntelligentTargetingLogDecisionSummary) {
        info(String("MinerTargetingSwitchDecisionSummary activeMiners=") +
             String::valueOf(miners.size()) +
             " evaluated=" + String::valueOf(evaluatedLimit) +
             " wouldActivate=" + String::valueOf(wouldActivateCount) +
             " fallback=" + String::valueOf(fallbackCount) +
             " capped=" + String::valueOf(cappedCount) +
             " noPlan=" + String::valueOf(noPlanCount) +
			 " noDensityTarget=" + String::valueOf(noDensityTargetCount) +
			 " pathRejected=" + String::valueOf(pathRejectedCount) +
			 " assignmentMismatches=" + String::valueOf(assignmentMismatchCount) +
			 " pathTrustRejected=" + String::valueOf(pathTrustRejectedCount) +
			 " rollbackHeld=" + String::valueOf(rollbackHeldCount) +
			 " actualActivations=" + String::valueOf(actualActivationCount) +
			 " controlledSkips=" + String::valueOf(activationControlledSkipCount) +
			 " activationFallbacks=" + String::valueOf(activationFallbackCount) +
			 " mode=" + minerIntelligentTargetingMode +
             " diagnosticOnly=" +
                (actualActivationCount > 0 ? String("false") : String("true")),
             true);
    }

    if (minerIntelligentTargetingLimitedLogHealthSummary &&
            minerIntelligentTargetingMode == "limited") {
        int healthAttempts = 0;
        int healthStarted = 0;
        int healthArrivals = 0;
        int healthSamplesCompleted = 0;
        int healthPathFailures = 0;
        int healthExpired = 0;
        int healthCooldownSkips = 0;
        int healthActiveCapSkips = 0;
        int healthZoneSkips = 0;

        snapshotAndResetMinerIntelligentActivationHealth(
            healthAttempts,
            healthStarted,
            healthArrivals,
            healthSamplesCompleted,
            healthPathFailures,
            healthExpired,
            healthCooldownSkips,
            healthActiveCapSkips,
            healthZoneSkips);

        info(String("MinerIntelligentActivationHealth active=") +
             String::valueOf(countActiveMinerIntelligentAssignments()) +
             " attempted=" + String::valueOf(healthAttempts) +
             " started=" + String::valueOf(healthStarted) +
             " arrivals=" + String::valueOf(healthArrivals) +
             " sampleFinished=" + String::valueOf(healthSamplesCompleted) +
             " pathFailed=" + String::valueOf(healthPathFailures) +
             " expired=" + String::valueOf(healthExpired) +
			 " rollbackHeld=" + String::valueOf(rollbackHeldCount) +
			 " controlledSkips=" + String::valueOf(activationControlledSkipCount) +
			 " assignmentMismatches=" + String::valueOf(assignmentMismatchCount) +
			 " pathTrustRejected=" + String::valueOf(pathTrustRejectedCount) +
			 " cooldownSkips=" + String::valueOf(healthCooldownSkips) +
             " activeCapSkips=" + String::valueOf(healthActiveCapSkips) +
             " zoneSkips=" + String::valueOf(healthZoneSkips) +
             " activationFallbacks=" + String::valueOf(activationFallbackCount) +
             " maxActive=" +
                String::valueOf(
                    minerIntelligentTargetingLimitedMaxActiveIntelligentMiners) +
             " maxActivationsPerInterval=" +
                String::valueOf(
                    minerIntelligentTargetingLimitedMaxActivationsPerInterval) +
             " cooldownSeconds=" +
                String::valueOf(
                    minerIntelligentTargetingLimitedCooldownSecondsPerMiner) +
             " emergencyDisabled=" +
                (minerIntelligentTargetingLimitedEmergencyDisabled ?
                    String("true") : String("false")) +
             " mode=" + minerIntelligentTargetingMode, true);
    }
}

void SimPlayerManager::applyDemandStateSimulationConfig(LuaObject& demandStateConfig) {
    demandStateSimulationEnabled = demandStateConfig.getBooleanField(
        "enabled", demandStateSimulationEnabled);
    demandStateSimulationIntervalSeconds = clampMinerInt(
        demandStateConfig.getIntField("intervalSeconds"),
        demandStateSimulationIntervalSeconds, 30, 3600);
    demandStateSimulationLogTopN = clampMinerInt(
        demandStateConfig.getIntField("logTopN"),
        demandStateSimulationLogTopN, 1, 20);
    demandStateSimulationActiveOpportunityWeight = clampFloatRange(
        demandStateConfig.getFloatField(
            "activeOpportunityWeight", demandStateSimulationActiveOpportunityWeight),
        0.f, 10.f);
    demandStateSimulationShortageWeight = clampFloatRange(
        demandStateConfig.getFloatField("shortageWeight", demandStateSimulationShortageWeight),
        0.f, 10.f);
    demandStateSimulationSurplusDampening = clampFloatRange(
        demandStateConfig.getFloatField(
            "surplusDampening", demandStateSimulationSurplusDampening),
        0.f, 1.f);

    String supplyMode = demandStateConfig.getStringField("supplyMode");

    if (supplyMode == "conceptual_totals")
        demandStateSimulationSupplyMode = supplyMode;

    const char* profileKeys[] = {
        "composite_armor_supply",
        "master_weaponsmith_staples",
        "high_damage_weapon_components",
        "chef_buff_foods",
        "chef_high_value_consumables",
        "production_infrastructure"
    };

    LuaObject profiles = demandStateConfig.getObjectField("profiles");
    if (profiles.isValidTable()) {
        for (int i = 0; i < 6; ++i) {
            String profileKey = profileKeys[i];
            LuaObject profile = profiles.getObjectField(profileKey);

            if (profile.isValidTable()) {
                int currentEnabled = demandStateSimulationProfileEnabled.contains(profileKey) ?
                    demandStateSimulationProfileEnabled.get(profileKey) : 1;
                int currentReserve = demandStateSimulationDesiredReserve.contains(profileKey) ?
                    demandStateSimulationDesiredReserve.get(profileKey) : 0;
                float currentLowThreshold =
                    demandStateSimulationLowStockThreshold.contains(profileKey) ?
                    demandStateSimulationLowStockThreshold.get(profileKey) : 0.35f;
                float currentCriticalThreshold =
                    demandStateSimulationCriticalStockThreshold.contains(profileKey) ?
                    demandStateSimulationCriticalStockThreshold.get(profileKey) : 0.10f;

                int desiredReserve = clampIntRange(
                    static_cast<int>(profile.getFloatField(
                        "desiredReserve", static_cast<float>(currentReserve))),
                    0, 100000000);
                float lowThreshold = clampFloatRange(
                    profile.getFloatField("lowStockThreshold", currentLowThreshold),
                    0.f, 1.f);
                float criticalThreshold = clampFloatRange(
                    profile.getFloatField(
                        "criticalStockThreshold", currentCriticalThreshold),
                    0.f, 1.f);

                if (criticalThreshold > lowThreshold)
                    criticalThreshold = lowThreshold;

                demandStateSimulationProfileEnabled.put(
                    profileKey,
                    profile.getBooleanField("enabled", currentEnabled != 0) ? 1 : 0);
                demandStateSimulationDesiredReserve.put(profileKey, desiredReserve);
                demandStateSimulationLowStockThreshold.put(profileKey, lowThreshold);
                demandStateSimulationCriticalStockThreshold.put(profileKey, criticalThreshold);
            }

            profile.pop();
        }
    }

    profiles.pop();
}

void SimPlayerManager::scheduleDemandStateSimulationTask() {
    if (!enabled || !demandStateSimulationEnabled || demandStateSimulationTaskScheduled)
        return;

    demandStateSimulationTaskScheduled = true;

    Reference<DemandStateSimulationTask*> task = new DemandStateSimulationTask();
    task->schedule(demandStateSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runDemandStateSimulationTask() {
    demandStateSimulationTaskScheduled = false;

    if (!enabled)
        return;

    refreshDemandStateSimulationConfig();

    if (!demandStateSimulationEnabled)
        return;

    logDemandStateSimulations();
    scheduleDemandStateSimulationTask();
}

void SimPlayerManager::refreshDemandStateSimulationConfig() {
    Lua configLua;
    configLua.init();

    try {
        configLua.runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        info(String("DemandStateSimulation configReloadFailed=true reason=\"") +
             e.getMessage() + "\" retainingPreviousConfig=true mode=log-only", true);
        return;
    }

    LuaObject managerConfig = configLua.getGlobalObject("SimPlayerManagerConfig");

    if (!managerConfig.isValidTable()) {
        info("DemandStateSimulation configReloadFailed=true reason=missingManagerConfig retainingPreviousConfig=true mode=log-only", true);
        managerConfig.pop();
        return;
    }

    LuaObject demandStateConfig = managerConfig.getObjectField("demandStateSimulationConfig");

    if (!demandStateConfig.isValidTable()) {
        info("DemandStateSimulation configReloadFailed=true reason=missingDemandStateConfig retainingPreviousConfig=true mode=log-only", true);
        demandStateConfig.pop();
        managerConfig.pop();
        return;
    }

    applyDemandStateSimulationConfig(demandStateConfig);

    demandStateConfig.pop();

    LuaObject persistentStockpileDemandConfig =
        managerConfig.getObjectField("persistentStockpileDemandConfig");

    if (persistentStockpileDemandConfig.isValidTable()) {
        applyPersistentStockpileDemandConfig(
            persistentStockpileDemandConfig);
    } else {
        persistentStockpileDemandEnabled = false;
    }

    persistentStockpileDemandConfig.pop();
    managerConfig.pop();
}

void SimPlayerManager::logDemandStateSimulations() {
    Vector<String> conceptualResourceNames;
    Vector<uint64> conceptualAmounts;
    collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

    Vector<ResourceIntelligenceEntry> entries;
    String snapshotError;
    bool activeSnapshotAvailable = collectResourceIntelligenceSnapshot(entries, snapshotError);

    if (!activeSnapshotAvailable) {
        info(String("DemandStateSimulation activeSnapshotAvailable=false reason=\"") +
             snapshotError + "\" continuingWithSupplyOnly=true mode=log-only", true);
    }

    Vector<DemandProfileDefinition> profiles = createDemandProfileDefinitions();
    VectorMap<String, uint64> marketQuantities;
    VectorMap<String, int> marketListings;
    VectorMap<String, float> marketCheapestPrices;
    VectorMap<String, float> marketMedianPrices;
    VectorMap<String, String> marketConfidences;
    VectorMap<String, String> marketTopResources;
    VectorMap<String, String> marketTopTypes;

    if (marketSupplyObservationEnabled) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            String profileKey = profiles.get(profileIndex).key;

            if (marketSupplyProfileQuantities.contains(profileKey))
                marketQuantities.put(
                    profileKey, marketSupplyProfileQuantities.get(profileKey));
            if (marketSupplyProfileListings.contains(profileKey))
                marketListings.put(
                    profileKey, marketSupplyProfileListings.get(profileKey));
            if (marketSupplyProfileCheapestPricePerUnit.contains(profileKey))
                marketCheapestPrices.put(
                    profileKey,
                    marketSupplyProfileCheapestPricePerUnit.get(profileKey));
            if (marketSupplyProfileMedianPricePerUnit.contains(profileKey))
                marketMedianPrices.put(
                    profileKey,
                    marketSupplyProfileMedianPricePerUnit.get(profileKey));
            if (marketSupplyProfileConfidence.contains(profileKey))
                marketConfidences.put(
                    profileKey, marketSupplyProfileConfidence.get(profileKey));
            if (marketSupplyProfileTopResource.contains(profileKey))
                marketTopResources.put(
                    profileKey, marketSupplyProfileTopResource.get(profileKey));
            if (marketSupplyProfileTopType.contains(profileKey))
                marketTopTypes.put(
                    profileKey, marketSupplyProfileTopType.get(profileKey));
        }
    }

    VectorMap<String, uint64> persistentConceptualTotals;
    int persistentConceptualLots = 0;
    uint64 persistentConceptualQuantity = 0;
    String persistentStockpileStatus = "disabled";
    String persistentStockpileMode = "disabled";

    if (persistentStockpileDemandEnabled) {
        persistentStockpileMode = persistentStockpileDemandIncludeConceptualMinerLots ?
            String("startup_baseline_only") : String("disabled");

        if (persistentStockpileDemandIncludeConceptualMinerLots) {
            bool snapshotReady =
                AiEconomyManager::instance()->
                    snapshotPersistentConceptualMinerSupplyForDemand(
                        persistentConceptualTotals,
                        persistentConceptualLots,
                        persistentConceptualQuantity,
                        persistentStockpileStatus);

            if (!snapshotReady)
                persistentConceptualTotals.removeAll();
        }

        if (persistentStockpileDemandLogSummary) {
            String persistentLabels = "none";
            int loggedLabels = 0;

            for (int labelIndex = 0;
                    labelIndex < persistentConceptualTotals.size();
                    ++labelIndex) {
                if (loggedLabels >= 12)
                    break;

                if (persistentLabels == "none")
                    persistentLabels = "";
                else
                    persistentLabels += ",";

                persistentLabels +=
                    persistentConceptualTotals.elementAt(labelIndex).getKey() +
                    "=" +
                    String::valueOf(persistentConceptualTotals.get(labelIndex));
                loggedLabels++;
            }

            if (persistentConceptualTotals.size() > loggedLabels) {
                if (persistentLabels == "none")
                    persistentLabels = "";
                else
                    persistentLabels += ",";

                persistentLabels += "truncated=true";
            }

            info(String("PersistentStockpileDemandSnapshot enabled=true conceptualMinerLots=") +
                String::valueOf(persistentConceptualLots) +
                " baselineQuantity=" +
                    String::valueOf(persistentConceptualQuantity) +
                " labels=" + persistentLabels +
                " status=" + persistentStockpileStatus +
                " mode=read-only", true);
        }
    }

    Vector<DemandStateSimulationResult> results;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        DemandProfileDefinition profile = profiles.get(profileIndex);
        bool profileEnabled = !demandStateSimulationProfileEnabled.contains(profile.key) ||
            demandStateSimulationProfileEnabled.get(profile.key) != 0;

        if (!profileEnabled)
            continue;

        DemandStateSimulationResult result;
        result.profileKey = profile.key;
        result.desiredReserve = demandStateSimulationDesiredReserve.contains(profile.key) ?
            static_cast<uint64>(demandStateSimulationDesiredReserve.get(profile.key)) : 0;
        result.aiConceptualSupply = estimateConceptualDemandStateSupply(
            profile.key,
            conceptualResourceNames,
            conceptualAmounts,
            result.supplyConfidence,
            result.supplyLabels);
        result.marketObservedSupply = marketQuantities.contains(profile.key) ?
            marketQuantities.get(profile.key) : 0;
        result.marketListingsMatched = marketListings.contains(profile.key) ?
            marketListings.get(profile.key) : 0;
        result.marketCheapestPricePerUnit =
            marketCheapestPrices.contains(profile.key) ?
            marketCheapestPrices.get(profile.key) : -1.f;
        result.marketMedianPricePerUnit =
            marketMedianPrices.contains(profile.key) ?
            marketMedianPrices.get(profile.key) : -1.f;
        result.marketSupplyConfidence = marketConfidences.contains(profile.key) ?
            marketConfidences.get(profile.key) : "none";
        result.marketTopResource = marketTopResources.contains(profile.key) ?
            marketTopResources.get(profile.key) : "";
        result.marketTopType = marketTopTypes.contains(profile.key) ?
            marketTopTypes.get(profile.key) : "";
        result.supplyConfidence = combineSupplyConfidence(
            result.supplyConfidence, result.marketSupplyConfidence);

        if (result.marketObservedSupply > 0) {
            String marketLabel = String("market=") +
                (result.marketTopType.isEmpty() ?
                    String("eligible_resource") : result.marketTopType) +
                "=" + String::valueOf(result.marketObservedSupply);

            if (result.supplyLabels == "none")
                result.supplyLabels = marketLabel;
            else
                result.supplyLabels += ";" + marketLabel;
        }

        if (persistentStockpileDemandEnabled) {
            result.persistentStockpileMode = persistentStockpileMode;
            result.persistentStockpileStatus = persistentStockpileStatus;

            if (persistentStockpileDemandIncludeConceptualMinerLots &&
                    persistentStockpileStatus == "ready") {
                result.persistentStockpileSupply =
                    estimatePersistentConceptualDemandStateSupply(
                        profile.key,
                        persistentConceptualTotals,
                        result.persistentStockpileLotsMatched,
                        result.persistentStockpileConfidence,
                        result.persistentStockpileLabels);
                result.persistentStockpileQuantityMatched =
                    result.persistentStockpileSupply;
                result.supplyConfidence = combineSupplyConfidence(
                    result.supplyConfidence,
                    result.persistentStockpileConfidence);

                if (result.persistentStockpileSupply > 0) {
                    String persistentLabel =
                        String("persistent=") +
                        result.persistentStockpileLabels;

                    if (result.supplyLabels == "none")
                        result.supplyLabels = persistentLabel;
                    else
                        result.supplyLabels += ";" + persistentLabel;
                }
            }
        }

        float lowThreshold =
            demandStateSimulationLowStockThreshold.contains(profile.key) ?
            demandStateSimulationLowStockThreshold.get(profile.key) : 0.35f;
        float criticalThreshold =
            demandStateSimulationCriticalStockThreshold.contains(profile.key) ?
            demandStateSimulationCriticalStockThreshold.get(profile.key) : 0.10f;

        result.activeProfileAvailableForPhase =
            demandProfileActiveForPhase(profile, demandProfileSimulationServerPhase);

        if (activeSnapshotAvailable && result.activeProfileAvailableForPhase) {
            for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                ResourceIntelligenceEntry entry = entries.get(entryIndex);
                DemandProfileMatch match =
                    evaluateDemandProfileResource(entry, profile, 1.f, 100);

                if (!match.eligible ||
                        (result.hasActiveOpportunity &&
                         match.demandScore <= result.activeMatch.demandScore)) {
                    continue;
                }

                result.hasActiveOpportunity = true;
                result.activeResource = entry;
                result.activeMatch = match;
            }
        }

        calculateDemandStatePressure(
            result,
            lowThreshold,
            criticalThreshold,
            demandStateSimulationShortageWeight,
            demandStateSimulationActiveOpportunityWeight,
            demandStateSimulationSurplusDampening);

        results.add(result);
    }

    if (results.size() == 0) {
        info("DemandStateSimulation skipped=true reason=noEnabledProfiles mode=log-only", true);
        return;
    }

    for (int i = 0; i < results.size(); ++i) {
        for (int j = i + 1; j < results.size(); ++j) {
            if (results.get(j).pressureScore <= results.get(i).pressureScore)
                continue;

            DemandStateSimulationResult swap = results.get(i);
            results.set(i, results.get(j));
            results.set(j, swap);
        }
    }

    int logCount = Math::min(demandStateSimulationLogTopN, results.size());

    for (int i = 0; i < logCount; ++i) {
        DemandStateSimulationResult result = results.get(i);
        DemandProfileDefinition profile;
        findDemandProfileDefinition(profiles, result.profileKey, profile);

        String reason = result.state + " reserve";

        if (result.state == "surplus") {
            if (result.marketObservedSupply > 0 &&
                    result.persistentStockpileSupply > 0) {
                reason =
                    "reserve met by known supply including observed market and persistent AI stockpile supply; active opportunity dampened";
            } else if (result.marketObservedSupply > 0) {
                reason =
                    "reserve met by known supply including observed market supply; active opportunity dampened";
            } else if (result.persistentStockpileSupply > 0) {
                reason =
                    "reserve met by known supply including persistent AI stockpile supply; active opportunity dampened";
            } else {
                reason = "reserve met; active opportunity dampened";
            }
        }
        else if (result.state == "disabledReserve")
            reason = "desired reserve disabled";

        if (result.hasActiveOpportunity) {
            reason += String("; active ") +
                formatDemandStateOpportunityReason(
                    result.activeMatch, result.activeResource, profile);
        } else if (!activeSnapshotAvailable) {
            reason += "; active resource snapshot unavailable";
        } else if (!result.activeProfileAvailableForPhase) {
            reason += "; profile inactive for server phase";
        } else {
            reason += "; no eligible active resource";
        }

        String line = String("DemandStateSimulation profile=") + result.profileKey +
            " state=" + result.state +
            " desiredReserve=" + String::valueOf(result.desiredReserve) +
            " aiConceptualSupply=" + String::valueOf(result.aiConceptualSupply) +
            " marketObservedSupply=" + String::valueOf(result.marketObservedSupply) +
            " marketListingsMatched=" +
                String::valueOf(result.marketListingsMatched) +
            " marketQuantityMatched=" +
                String::valueOf(result.marketObservedSupply) +
            " marketSupplyConfidence=" + result.marketSupplyConfidence +
            " persistentStockpileSupply=" +
                String::valueOf(result.persistentStockpileSupply) +
            " totalKnownSupply=" + String::valueOf(result.totalKnownSupply) +
            " knownSupply=" + String::valueOf(result.totalKnownSupply) +
            " supplyMode=" + demandStateSimulationSupplyMode +
            " supplyConfidence=" + result.supplyConfidence +
            " supplyLabels=" + result.supplyLabels +
            " reserveRatio=" +
                String::valueOf(Math::getPrecision(result.reserveRatio, 3)) +
            " shortageUnits=" + String::valueOf(result.shortageUnits) +
            " surplusUnits=" + String::valueOf(result.surplusUnits) +
            " shortagePressure=" +
                String::valueOf(Math::getPrecision(result.shortagePressure, 1)) +
            " opportunityPressure=" +
                String::valueOf(Math::getPrecision(result.opportunityPressure, 1)) +
            " activeOpportunityScore=" +
                String::valueOf(result.activeMatch.demandScore) +
            " pressureScore=" +
                String::valueOf(Math::getPrecision(result.pressureScore, 1));

        if (persistentStockpileDemandEnabled) {
            line += " persistentStockpileLotsMatched=" +
                String::valueOf(result.persistentStockpileLotsMatched) +
                " persistentStockpileQuantityMatched=" +
                    String::valueOf(
                        result.persistentStockpileQuantityMatched) +
                " persistentStockpileConfidence=" +
                    result.persistentStockpileConfidence +
                " persistentStockpileLabels=" +
                    result.persistentStockpileLabels +
                " persistentStockpileMode=" +
                    result.persistentStockpileMode +
                " persistentStockpileStatus=" +
                    result.persistentStockpileStatus;
        }

        if (result.marketCheapestPricePerUnit >= 0.f) {
            line += " marketCheapestPricePerUnit=" +
                String::valueOf(Math::getPrecision(
                    result.marketCheapestPricePerUnit, 3));
        } else {
            line += " marketCheapestPricePerUnit=unavailable";
        }

        if (result.marketMedianPricePerUnit >= 0.f) {
            line += " marketMedianPricePerUnit=" +
                String::valueOf(Math::getPrecision(
                    result.marketMedianPricePerUnit, 3));
        } else {
            line += " marketMedianPricePerUnit=unavailable";
        }

        line += " marketTopResource=" +
            (result.marketTopResource.isEmpty() ?
                String("none") : result.marketTopResource) +
            " marketTopType=" +
            (result.marketTopType.isEmpty() ?
                String("none") : result.marketTopType);

        if (result.hasActiveOpportunity) {
            line += " activeResource=" + result.activeResource.name +
                " type=" + result.activeResource.type +
                " zones=" +
                    (result.activeResource.zones.isEmpty() ?
                        String("unknown") : result.activeResource.zones) +
                " activeBaseScore=" +
                    String::valueOf(result.activeMatch.baseScore) +
                " premiumQuality=" +
                    (result.activeMatch.premium ? String("true") : String("false")) +
                " bulkEligible=" +
                    (result.activeMatch.bulk ? String("true") : String("false"));
        } else {
            line += " activeResource=none";
        }

        line += " reason=\"" + reason + "\" mode=log-only";
        info(line, true);
    }
}

void SimPlayerManager::scheduleMinerTargetRecommendationTask() {
    if (!enabled || !minerTargetRecommendationsEnabled || minerTargetRecommendationTaskScheduled)
        return;

    minerTargetRecommendationTaskScheduled = true;

    Reference<MinerTargetRecommendationTask*> task = new MinerTargetRecommendationTask();
    task->schedule(minerTargetRecommendationIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerTargetRecommendationTask() {
    minerTargetRecommendationTaskScheduled = false;

    if (!enabled || !minerTargetRecommendationsEnabled)
        return;

    logMinerTargetRecommendations();
    scheduleMinerTargetRecommendationTask();
}

void SimPlayerManager::logMinerTargetRecommendations() {
    Vector<ResourceIntelligenceEntry> entries;
    String errorMessage;

    if (!collectResourceIntelligenceSnapshot(entries, errorMessage)) {
        info(String("MinerTargetRecommendation: ") + errorMessage + "; skipping log-only recommendations", true);
        return;
    }

    calculateResourceIntelligenceScores(entries);

    if (entries.size() == 0)
        return;

    Vector<ResourceScoringProfile> profiles = createCuratedResourceScoringProfiles();
    int loggedMinerCount = 0;
    int controllerCount = controllers.size();

    for (int controllerIndex = 0; controllerIndex < controllerCount; ++controllerIndex) {
        uint64 controllerKey = controllers.getKey(controllerIndex);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr || dynamic_cast<SimMinerController*>(ctrl.get()) == nullptr)
            continue;

        ManagedReference<AiAgent*> agent = ctrl->getAgent();

        if (agent == nullptr)
            continue;

        uint64 minerID = agent->getObjectID();
        String zoneName = "unknown";

        Zone* zone = agent->getZone();

        if (zone != nullptr)
            zoneName = zone->getZoneName();

        for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
            ResourceScoringProfile profile = profiles.get(profileIndex);

            if (!configuredProfileKeyEnabled(minerTargetRecommendationProfileKeys, profile.key))
                continue;

            Vector<int> usedIndexes;
            int loggedForProfile = 0;
            bool samePlanetMatchFound = false;

            while (loggedForProfile < minerTargetRecommendationTopN) {
                int bestIndex = -1;
                int bestScore = 0;
                String bestMatchedType;

                for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                    if (resourceIntelligenceIndexUsed(usedIndexes, entryIndex))
                        continue;

                    ResourceIntelligenceEntry entry = entries.get(entryIndex);

                    if (!resourceAvailableInZone(entry, zoneName))
                        continue;

                    String matchedType = getBestMatchedResourceType(entry, profile);

                    if (matchedType.isEmpty())
                        continue;

                    int score = calculateProfileScore(entry, profile);

                    if (score > bestScore) {
                        bestScore = score;
                        bestIndex = entryIndex;
                        bestMatchedType = matchedType;
                    }
                }

                if (bestIndex < 0 || bestScore <= 0)
                    break;

                samePlanetMatchFound = true;
                usedIndexes.add(bestIndex);
                ++loggedForProfile;

                info(formatMinerTargetRecommendationLine(minerID, zoneName, profile, entries.get(bestIndex), bestScore, bestMatchedType, false), true);
            }

            if (samePlanetMatchFound)
                continue;

            usedIndexes.removeAll();
            loggedForProfile = 0;

            while (loggedForProfile < minerTargetRecommendationTopN) {
                int bestIndex = -1;
                int bestScore = 0;
                String bestMatchedType;

                for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                    if (resourceIntelligenceIndexUsed(usedIndexes, entryIndex))
                        continue;

                    ResourceIntelligenceEntry entry = entries.get(entryIndex);
                    String matchedType = getBestMatchedResourceType(entry, profile);

                    if (matchedType.isEmpty())
                        continue;

                    int score = calculateProfileScore(entry, profile);

                    if (score > bestScore) {
                        bestScore = score;
                        bestIndex = entryIndex;
                        bestMatchedType = matchedType;
                    }
                }

                if (bestIndex < 0 || bestScore <= 0)
                    break;

                usedIndexes.add(bestIndex);
                ++loggedForProfile;

                info(formatMinerTargetRecommendationLine(minerID, zoneName, profile, entries.get(bestIndex), bestScore, bestMatchedType, true), true);
            }

            if (loggedForProfile == 0) {
                info(String("MinerTargetRecommendation miner=") + String::valueOf(minerID) +
                     " zone=" + zoneName +
                     " profile=" + profile.key +
                     " category=" + profile.category +
                     " noEligibleTarget=true mode=log-only", true);
            }
        }

        ++loggedMinerCount;

        if (!minerTargetRecommendationIncludeAllActiveMiners)
            break;
    }

    if (loggedMinerCount == 0)
        return;
}

void SimPlayerManager::scheduleMinerTargetSimulationTask() {
    if (!enabled || !minerTargetSimulationEnabled || minerTargetSimulationTaskScheduled)
        return;

    minerTargetSimulationTaskScheduled = true;

    Reference<MinerTargetSimulationTask*> task = new MinerTargetSimulationTask();
    task->schedule(minerTargetSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerTargetSimulationTask() {
    minerTargetSimulationTaskScheduled = false;

    if (!enabled || !minerTargetSimulationEnabled)
        return;

    logMinerTargetSimulations();
    scheduleMinerTargetSimulationTask();
}

void SimPlayerManager::logMinerTargetSimulations() {
    if (countActiveMiners() == 0)
        return;

    Vector<ResourceIntelligenceEntry> entries;
    String errorMessage;

    if (!collectResourceIntelligenceSnapshot(entries, errorMessage)) {
        info(String("MinerTargetSimulation: ") + errorMessage + "; skipping simulation-only plans", true);
        return;
    }

    if (entries.size() == 0)
        return;

    Vector<ResourceScoringProfile> profiles = createCuratedResourceScoringProfiles();
    Vector<int> enabledProfileIndexes;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        ResourceScoringProfile profile = profiles.get(profileIndex);
        float profileWeight = getMinerTargetSimulationProfileWeight(minerTargetSimulationProfileWeights, profile.key);

        if (profileWeight > 0.f)
            enabledProfileIndexes.add(profileIndex);
    }

    if (enabledProfileIndexes.size() == 0) {
        info("MinerTargetSimulation skipped=true reason=noEnabledProfiles mode=simulation-only", true);
        return;
    }

    int minerOrdinal = 0;
    int controllerCount = controllers.size();

    for (int controllerIndex = 0; controllerIndex < controllerCount; ++controllerIndex) {
        uint64 controllerKey = controllers.getKey(controllerIndex);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr || dynamic_cast<SimMinerController*>(ctrl.get()) == nullptr)
            continue;

        ManagedReference<AiAgent*> agent = ctrl->getAgent();

        if (agent == nullptr)
            continue;

        uint64 minerID = agent->getObjectID();
        String zoneName = "unknown";
        Zone* zone = agent->getZone();

        if (zone != nullptr)
            zoneName = zone->getZoneName();

        int assignedProfileIndex = -1;
        MinerTargetSimulationPlan selectedPlan = selectAssignedMinerTargetSimulationPlan(
            entries,
            profiles,
            enabledProfileIndexes,
            minerTargetSimulationProfileWeights,
            minerOrdinal,
            zoneName,
            minerTargetSimulationPreferSamePlanet,
            minerTargetSimulationSamePlanetBonus,
            minerTargetSimulationTravelPenalty,
            assignedProfileIndex);
        ResourceScoringProfile assignedProfile = profiles.get(assignedProfileIndex);

        if (selectedPlan.isValid()) {
            ResourceScoringProfile selectedProfile = profiles.get(selectedPlan.profileIndex);
            ResourceIntelligenceEntry selectedResource = entries.get(selectedPlan.resourceIndex);

            info(formatMinerTargetSimulationLine(
                minerID,
                zoneName,
                selectedProfile,
                selectedResource,
                selectedPlan,
                minerTargetSimulationAssignmentMode), true);
        } else {
            info(String("MinerTargetSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " assignedProfile=" + assignedProfile.key +
                " noEligibleTarget=true assignmentMode=" + minerTargetSimulationAssignmentMode +
                " mode=simulation-only", true);
        }

        ++minerOrdinal;
    }
}

void SimPlayerManager::scheduleMinerDensityTargetSimulationTask() {
    if (!enabled || !minerDensityTargetSimulationEnabled || minerDensityTargetSimulationTaskScheduled)
        return;

    minerDensityTargetSimulationTaskScheduled = true;

    Reference<MinerDensityTargetSimulationTask*> task = new MinerDensityTargetSimulationTask();
    task->schedule(minerDensityTargetSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerDensityTargetSimulationTask() {
    minerDensityTargetSimulationTaskScheduled = false;

    if (!enabled || !minerDensityTargetSimulationEnabled)
        return;

    logMinerDensityTargetSimulations();
    scheduleMinerDensityTargetSimulationTask();
}

void SimPlayerManager::logMinerDensityTargetSimulations() {
    if (countActiveMiners() == 0)
        return;

    Vector<ResourceIntelligenceEntry> entries;
    String errorMessage;

    if (!collectResourceIntelligenceSnapshot(entries, errorMessage)) {
        info(String("MinerDensityTargetSimulation: ") + errorMessage + "; skipping simulation-only density targets", true);
        return;
    }

    if (entries.size() == 0)
        return;

    Vector<ResourceScoringProfile> roundRobinProfiles = createCuratedResourceScoringProfiles();
    Vector<int> enabledProfileIndexes;

    for (int profileIndex = 0; profileIndex < roundRobinProfiles.size(); ++profileIndex) {
        ResourceScoringProfile profile = roundRobinProfiles.get(profileIndex);
        float profileWeight = getMinerTargetSimulationProfileWeight(minerTargetSimulationProfileWeights, profile.key);

        if (profileWeight > 0.f)
            enabledProfileIndexes.add(profileIndex);
    }

    Vector<String> conceptualResourceNames;
    Vector<uint64> conceptualAmounts;
    collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

    Vector<DemandProfileDefinition> demandProfiles = createDemandProfileDefinitions();
    VectorMap<String, uint64> marketQuantities;

    if (demandWeightedMinerPlanSimulationIncludeMarketSupply) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int profileIndex = 0; profileIndex < demandProfiles.size(); ++profileIndex) {
            String profileKey = demandProfiles.get(profileIndex).key;

            if (marketSupplyProfileQuantities.contains(profileKey))
                marketQuantities.put(profileKey, marketSupplyProfileQuantities.get(profileKey));
        }
    }

    Vector<DemandStateSimulationResult> pressureResults;
    int profilesDisabled = 0;
    int profilesInactivePhase = 0;
    int profilesBelowPressure = 0;
    int profilesNoEligibleResource = 0;

    if (demandWeightedMinerPlanSimulationEnabled) {
        buildDemandWeightedPressureResultsForMiners(
            entries,
            demandProfiles,
            conceptualResourceNames,
            conceptualAmounts,
            marketQuantities,
            demandWeightedMinerPlanSimulationProfileEnabled,
            demandWeightedMinerPlanSimulationDesiredReserve,
            demandWeightedMinerPlanSimulationLowStockThreshold,
            demandWeightedMinerPlanSimulationCriticalStockThreshold,
            demandWeightedMinerPlanSimulationServerPhase,
            demandWeightedMinerPlanSimulationShortageWeight,
            demandWeightedMinerPlanSimulationActiveOpportunityWeight,
            demandWeightedMinerPlanSimulationSurplusDampening,
            demandWeightedMinerPlanSimulationMinimumPressureThreshold,
            pressureResults,
            profilesDisabled,
            profilesInactivePhase,
            profilesBelowPressure,
            profilesNoEligibleResource);
    }

    if (pressureResults.size() == 0 && enabledProfileIndexes.size() == 0) {
        info("MinerDensityTargetSimulation skipped=true reason=noEnabledProfiles mode=simulation-only", true);
        return;
    }

    Vector<MinerIntelligentTargetingMinerSnapshot> miners;
    int controllerCount = controllers.size();

    for (int controllerIndex = 0; controllerIndex < controllerCount; ++controllerIndex) {
        uint64 controllerKey = controllers.getKey(controllerIndex);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr || dynamic_cast<SimMinerController*>(ctrl.get()) == nullptr)
            continue;

        ManagedReference<AiAgent*> agent = ctrl->getAgent();

        if (agent == nullptr)
            continue;

        MinerIntelligentTargetingMinerSnapshot miner;

        {
            Locker agentLocker(agent);
            miner.zone = agent->getZone();

            if (miner.zone != nullptr) {
                miner.objectID = agent->getObjectID();
                miner.zoneName = miner.zone->getZoneName();
                miner.position = agent->getWorldPosition();
                miner.inNavmesh = agent->isInNavMesh();
                miner.dead = agent->isDead();
                miner.incapacitated = agent->isIncapacitated();
                miner.inCombat = agent->isInCombat();
            }
        }

        if (miner.isValid())
            miners.add(miner);
    }

    for (int i = 0; i < miners.size(); ++i) {
        for (int j = i + 1; j < miners.size(); ++j) {
            if (miners.get(j).objectID >= miners.get(i).objectID)
                continue;

            MinerIntelligentTargetingMinerSnapshot swap = miners.get(i);
            miners.set(i, miners.get(j));
            miners.set(j, swap);
        }
    }

    resetNavAreaDensitySelectionDiagnostics(
        navAreaDensitySelectionEnabled,
        navAreaDensitySelectionShadowMode,
        navAreaMaxSampleAttemptsPerCycle,
        navAreaMaxPathValidationsPerCycle);

    int minerOrdinal = 0;
    VectorMap<String, int> demandAssignmentsByProfile;

    for (int minerIndex = 0; minerIndex < miners.size(); ++minerIndex) {
        MinerIntelligentTargetingMinerSnapshot miner = miners.get(minerIndex);
        String targetSource = "none";
        String selectedProfileKey = "none";
        ResourceIntelligenceEntry selectedResource;
        bool samePlanet = false;
        bool hasTarget = false;
        bool usingAssignment = false;
        MinerDensityTargetCandidate densityTarget;
        MinerIntelligentTargetAssignment assignment;
        uint64 nowMs = System::getMiliTime();

        if (minerIntelligentTargetingEnabled &&
                (minerIntelligentTargetingMode == "shadow" ||
                 minerIntelligentTargetingMode == "limited") &&
                minerIntelligentTargetingAssignmentEnabled &&
                getMinerIntelligentTargetAssignment(
                    miner.objectID, assignment)) {
	        uint64 timeoutAgeSeconds = 0;
	        uint64 timeoutSeconds = 0;
	        String timeoutReason = getMinerIntelligentAssignmentTimeoutReason(
	            assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, true);

	        if (!timeoutReason.isEmpty()) {
	            clearMinerIntelligentTargetAssignment(
	                miner.objectID, timeoutReason, minerIntelligentTargetingMode);
	        } else {
	            if (assignment.normalTtlSkippedForActiveMovement)
	                putMinerIntelligentTargetAssignment(assignment);

	            if (assignment.targetZoneName != miner.zoneName) {
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID, "zoneChanged", minerIntelligentTargetingMode);
	            } else if (minerIntelligentTargetingAssignmentClearOnIncapOrDeath &&
	                    (miner.dead || miner.incapacitated)) {
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID,
	                    miner.dead ? String("dead") : String("incapacitated"),
	                    minerIntelligentTargetingMode);
	            } else if (minerIntelligentTargetingAssignmentClearOnCombat &&
	                    miner.inCombat) {
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID, "combat", minerIntelligentTargetingMode);
	            } else if (assignment.densityTargetStatus == "accepted") {
	                targetSource = assignment.targetSource;
	                selectedProfileKey = assignment.selectedProfileKey;
	                selectedResource.name = assignment.targetResourceName;
	                selectedResource.type = assignment.targetResourceType;
	                samePlanet = true;
	                hasTarget = true;
	                usingAssignment = true;
	                densityTarget.x = assignment.targetX;
	                densityTarget.y = assignment.targetY;
	                densityTarget.z = assignment.targetZ;
	                densityTarget.density = assignment.targetDensity;
	                densityTarget.distance =
	                    miner.position.distanceTo(
	                        Vector3(assignment.targetX, assignment.targetY, assignment.targetZ));
	                densityTarget.searchRadius = 1;
	            }
	        }
        }

        if (!hasTarget && pressureResults.size() > 0) {
            DemandWeightedPlanSelection demandSelection =
                selectDemandWeightedMinerPlanForValidation(
                    entries,
                    demandProfiles,
                    pressureResults,
                    demandAssignmentsByProfile,
                    miner.zoneName,
                    demandWeightedMinerPlanSimulationSamePlanetBonus,
                    demandWeightedMinerPlanSimulationTravelPenalty,
                    demandWeightedMinerPlanSimulationMaxMinersPerProfile,
                    demandWeightedMinerPlanSimulationStrongPressureRatio);

            if (demandSelection.valid) {
                targetSource = "demand_weighted_plan";
                selectedProfileKey = demandSelection.selectedResult.profileKey;
                selectedResource = demandSelection.selectedResource;
                samePlanet = demandSelection.selected.target.samePlanet;
                hasTarget = true;
            }
        }

        int assignedProfileIndex = -1;

        if (!hasTarget) {
            MinerTargetSimulationPlan plan = selectAssignedMinerTargetSimulationPlan(
                entries,
                roundRobinProfiles,
                enabledProfileIndexes,
                minerTargetSimulationProfileWeights,
                minerOrdinal,
                miner.zoneName,
                minerTargetSimulationPreferSamePlanet,
                minerTargetSimulationSamePlanetBonus,
                minerTargetSimulationTravelPenalty,
                assignedProfileIndex);

            if (plan.isValid()) {
                ResourceScoringProfile selectedProfile = roundRobinProfiles.get(plan.profileIndex);
                targetSource = "round_robin_plan";
                selectedProfileKey = selectedProfile.key;
                selectedResource = entries.get(plan.resourceIndex);
                samePlanet = plan.samePlanet;
                hasTarget = true;
            }
        }

        if (!hasTarget) {
            info(String("MinerDensityTargetSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " targetSource=none noEligibleTarget=true noDensityTarget=true" +
                " minAcceptableDensity=" +
                String::valueOf(Math::getPrecision(minerDensityTargetSimulationMinAcceptableDensity, 3)) +
                " candidateCount=0 rejectReason=noValidCandidate mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        if (!samePlanet) {
            info(String("MinerDensityTargetSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " profile=" + selectedProfileKey +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " targetSource=" + targetSource +
                " zones=" + (selectedResource.zones.isEmpty() ? String("unknown") : selectedResource.zones) +
                " travelRequired=true noSamePlanetDensityTarget=true noDensityTarget=true" +
                " minAcceptableDensity=" +
                String::valueOf(Math::getPrecision(minerDensityTargetSimulationMinAcceptableDensity, 3)) +
                " candidateCount=0 rejectReason=wrongPlanet mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        MinerDensityTargetDiagnostics densityDiagnostics;
        bool foundTarget = usingAssignment;

        if (!usingAssignment) {
            foundTarget = findMinerDensityTarget(
                miner.objectID,
                selectedProfileKey,
                targetSource,
                selectedResource,
                miner.zone,
                miner.position,
                minerDensityTargetSimulationSearchRadii,
                minerDensityTargetSimulationSamplesPerRadius,
                minerDensityTargetSimulationMinAcceptableDensity,
                minerDensityTargetSimulationRequireNavmesh,
                miner.inNavmesh,
                minerDensityTargetSimulationMaxPathCheckAttempts,
                minerDensityTargetSimulationDistancePenaltyPerMeter,
                reachabilityMemoryEnabled,
                reachabilityCandidatePreferenceEnabled,
                reachabilityBucketSizeMeters,
                reachabilityMinAttemptsBeforePenalty,
                reachabilityVerifiedPathScoreBonus,
                reachabilitySampleCompleteScoreBonus,
                reachabilityRepeatedFailurePenalty,
                reachabilityLongDistancePenalty512Plus,
                reachabilityMemoryTtlSeconds,
                reachabilityMaxMemoryRows,
                densityTarget,
                densityDiagnostics);
        }

        if (!usingAssignment) {
            MinerDensityTargetCandidate navAreaTarget;
            String navAreaSelectionMode;
            String navAreaReason;
            String navAreaName;
            String navAreaRole;
            bool navAreaFound = evaluateNavAreaDensitySelection(
                miner.objectID,
                selectedProfileKey,
                selectedResource,
                miner.zone,
                miner.position,
                navAreaDensitySelectionEnabled,
                navAreaDensitySelectionShadowMode,
                navAreaSampleCacheTtlSeconds,
                navAreaMaxSamplesPerArea,
                navAreaMaxSampleAttemptsPerCycle,
                navAreaMaxPathValidationsPerCycle,
                navAreaAvoidGenericInteriors,
                navAreaPreferCityAndPoiRegions,
                minerDensityTargetSimulationMinAcceptableDensity,
                minerDensityTargetSimulationDistancePenaltyPerMeter,
                navAreaTarget,
                navAreaSelectionMode,
                navAreaReason,
                navAreaName,
                navAreaRole);

            if (navAreaFound) {
                bool activeNavAreaSelection =
                    navAreaDensitySelectionEnabled &&
                    !navAreaDensitySelectionShadowMode;
                bool wouldSelectDifferent =
                    !densityTarget.isValid() ||
                    Vector3(densityTarget.x, densityTarget.y, densityTarget.z).
                        distanceTo(Vector3(
                            navAreaTarget.x,
                            navAreaTarget.y,
                            navAreaTarget.z)) > 5.f;

                info(String("NavAreaDensitySelection miner=") +
                    String::valueOf(miner.objectID) +
                    " zone=" + miner.zoneName +
                    " profile=" + selectedProfileKey +
                    " resource=" + selectedResource.name +
                    " type=" + selectedResource.type +
                    " sourceArea=" + navAreaName +
                    " sourceRole=" + navAreaRole +
                    " densitySelectionMode=" + navAreaSelectionMode +
                    " activeSelection=" +
                        (activeNavAreaSelection ? String("true") : String("false")) +
                    " wouldSelectDifferent=" +
                        (wouldSelectDifferent ? String("true") : String("false")) +
                    " navTarget=(x:" +
                        String::valueOf(Math::getPrecision(navAreaTarget.x, 1)) +
                    ",y:" +
                        String::valueOf(Math::getPrecision(navAreaTarget.y, 1)) +
                    ",z:" +
                        String::valueOf(Math::getPrecision(navAreaTarget.z, 1)) +
                    ")" +
                    " navDensity=" +
                        String::valueOf(Math::getPrecision(navAreaTarget.density, 3)) +
                    " navScore=" +
                        String::valueOf(Math::getPrecision(navAreaTarget.adjustedScore, 1)) +
                    " reason=" + navAreaReason +
                    " mode=" +
                        (activeNavAreaSelection ?
                            String("active") : String("shadow-only")), true);

                if (activeNavAreaSelection) {
                    densityTarget = navAreaTarget;
                    foundTarget = true;
                }
            }
        }

        if (foundTarget) {
            String reason = usingAssignment ? String("retained assignment target") :
                (densityTarget.density >= minerDensityTargetSimulationPreferredDensity ?
                    String("nearest preferred pocket") :
                    String("nearest acceptable pocket"));

            info(String("MinerDensityTargetSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " profile=" + selectedProfileKey +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " targetSource=" + targetSource +
                " assignmentStatus=" +
                    (usingAssignment ?
                        (assignment.status.isEmpty() ?
                            String("candidate") : assignment.status) :
                        String("none")) +
                " target=(x:" + String::valueOf(Math::getPrecision(densityTarget.x, 1)) +
                ",y:" + String::valueOf(Math::getPrecision(densityTarget.y, 1)) +
                ",z:" + String::valueOf(Math::getPrecision(densityTarget.z, 1)) + ")" +
                " density=" + String::valueOf(Math::getPrecision(densityTarget.density, 3)) +
                " distance=" + String::valueOf(Math::getPrecision(densityTarget.distance, 1)) +
                " searchRadius=" + String::valueOf(densityTarget.searchRadius) +
                " samplesChecked=" + String::valueOf(densityTarget.samplesChecked) +
                " candidateCount=" + String::valueOf(densityDiagnostics.candidateCount) +
                " acceptedCandidateRank=" + String::valueOf(densityDiagnostics.acceptedCandidateRank) +
                " minAcceptableDensity=" +
                String::valueOf(Math::getPrecision(minerDensityTargetSimulationMinAcceptableDensity, 3)) +
                " navmeshChecked=" + (densityTarget.navmeshChecked ? String("true") : String("false")) +
                " mode=simulation-only reason=" + reason, true);
        } else {
            String rejectionLine = String("MinerDensityTargetSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " profile=" + selectedProfileKey +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " targetSource=" + targetSource +
                " noDensityTarget=true bestDensity=" +
                String::valueOf(Math::getPrecision(densityDiagnostics.bestObservedCandidate.density, 3)) +
                " minAcceptableDensity=" +
                String::valueOf(Math::getPrecision(minerDensityTargetSimulationMinAcceptableDensity, 3)) +
                " candidateCount=" + String::valueOf(densityDiagnostics.candidateCount) +
                " searchedRadii=" + densityDiagnostics.searchedRadii +
                " rejectReason=" + densityDiagnostics.rejectReason;

            if (densityDiagnostics.hasBestRejectedCandidate()) {
                rejectionLine += String(" bestRejectedDensity=") +
                    String::valueOf(Math::getPrecision(densityDiagnostics.bestRejectedCandidate.density, 3)) +
                    " bestRejectedDistance=" +
                    String::valueOf(Math::getPrecision(densityDiagnostics.bestRejectedCandidate.distance, 1)) +
                    " bestRejectedReason=" + densityDiagnostics.bestRejectedReason;
            }

            rejectionLine += " mode=simulation-only";
            info(rejectionLine, true);
        }

        ++minerOrdinal;
    }
}

void SimPlayerManager::scheduleMinerPathValidationSimulationTask() {
    if (!enabled || !minerPathValidationSimulationEnabled || minerPathValidationSimulationTaskScheduled)
        return;

    minerPathValidationSimulationTaskScheduled = true;

    Reference<MinerPathValidationSimulationTask*> task = new MinerPathValidationSimulationTask();
    task->schedule(minerPathValidationSimulationIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerPathValidationSimulationTask() {
    minerPathValidationSimulationTaskScheduled = false;

    if (!enabled || !minerPathValidationSimulationEnabled)
        return;

    logMinerPathValidationSimulations();
    scheduleMinerPathValidationSimulationTask();
}

void SimPlayerManager::logMinerPathValidationSimulations() {
    if (countActiveMiners() == 0)
        return;

    Vector<ResourceIntelligenceEntry> entries;
    String errorMessage;

    if (!collectResourceIntelligenceSnapshot(entries, errorMessage)) {
        info(String("MinerPathValidationSimulation skipped=true reason=resourceSnapshotUnavailable detail=") +
            errorMessage + " mode=simulation-only", true);
        return;
    }

    if (entries.size() == 0)
        return;

    Vector<ResourceScoringProfile> roundRobinProfiles = createCuratedResourceScoringProfiles();
    Vector<int> enabledProfileIndexes;

    for (int profileIndex = 0; profileIndex < roundRobinProfiles.size(); ++profileIndex) {
        ResourceScoringProfile profile = roundRobinProfiles.get(profileIndex);
        float profileWeight = getMinerTargetSimulationProfileWeight(minerTargetSimulationProfileWeights, profile.key);

        if (profileWeight > 0.f)
            enabledProfileIndexes.add(profileIndex);
    }

    Vector<String> conceptualResourceNames;
    Vector<uint64> conceptualAmounts;
    collectConceptualMinerTotals(conceptualResourceNames, conceptualAmounts);

    Vector<DemandProfileDefinition> demandProfiles = createDemandProfileDefinitions();
    VectorMap<String, uint64> marketQuantities;

    if (demandWeightedMinerPlanSimulationIncludeMarketSupply) {
        Locker marketSnapshotLocker(&marketSupplyObservationMutex);

        for (int profileIndex = 0; profileIndex < demandProfiles.size(); ++profileIndex) {
            String profileKey = demandProfiles.get(profileIndex).key;

            if (marketSupplyProfileQuantities.contains(profileKey))
                marketQuantities.put(profileKey, marketSupplyProfileQuantities.get(profileKey));
        }
    }

    Vector<DemandStateSimulationResult> pressureResults;
    int profilesDisabled = 0;
    int profilesInactivePhase = 0;
    int profilesBelowPressure = 0;
    int profilesNoEligibleResource = 0;

    if (demandWeightedMinerPlanSimulationEnabled) {
        buildDemandWeightedPressureResultsForMiners(
            entries,
            demandProfiles,
            conceptualResourceNames,
            conceptualAmounts,
            marketQuantities,
            demandWeightedMinerPlanSimulationProfileEnabled,
            demandWeightedMinerPlanSimulationDesiredReserve,
            demandWeightedMinerPlanSimulationLowStockThreshold,
            demandWeightedMinerPlanSimulationCriticalStockThreshold,
            demandWeightedMinerPlanSimulationServerPhase,
            demandWeightedMinerPlanSimulationShortageWeight,
            demandWeightedMinerPlanSimulationActiveOpportunityWeight,
            demandWeightedMinerPlanSimulationSurplusDampening,
            demandWeightedMinerPlanSimulationMinimumPressureThreshold,
            pressureResults,
            profilesDisabled,
            profilesInactivePhase,
            profilesBelowPressure,
            profilesNoEligibleResource);
    }

    if (pressureResults.size() == 0 && enabledProfileIndexes.size() == 0) {
        info("MinerPathValidationSimulation skipped=true reason=noEnabledProfiles mode=simulation-only", true);
        return;
    }

    Vector<MinerIntelligentTargetingMinerSnapshot> miners;
    int controllerCount = controllers.size();

    for (int controllerIndex = 0; controllerIndex < controllerCount; ++controllerIndex) {
        uint64 controllerKey = controllers.getKey(controllerIndex);
        Reference<SimPlayerController*> ctrl = controllers.get(controllerKey);

        if (ctrl == nullptr || dynamic_cast<SimMinerController*>(ctrl.get()) == nullptr)
            continue;

        ManagedReference<AiAgent*> agent = ctrl->getAgent();

        if (agent == nullptr)
            continue;

        MinerIntelligentTargetingMinerSnapshot miner;

        {
            Locker agentLocker(agent);
            miner.zone = agent->getZone();

            if (miner.zone != nullptr) {
                miner.objectID = agent->getObjectID();
                miner.zoneName = miner.zone->getZoneName();
                miner.position = agent->getWorldPosition();
                miner.inNavmesh = agent->isInNavMesh();
                miner.dead = agent->isDead();
                miner.incapacitated = agent->isIncapacitated();
                miner.inCombat = agent->isInCombat();
            }
        }

        if (miner.isValid())
            miners.add(miner);
    }

    for (int i = 0; i < miners.size(); ++i) {
        for (int j = i + 1; j < miners.size(); ++j) {
            if (miners.get(j).objectID >= miners.get(i).objectID)
                continue;

            MinerIntelligentTargetingMinerSnapshot swap = miners.get(i);
            miners.set(i, miners.get(j));
            miners.set(j, swap);
        }
    }

    resetNavAreaDensitySelectionDiagnostics(
        navAreaDensitySelectionEnabled,
        navAreaDensitySelectionShadowMode,
        navAreaMaxSampleAttemptsPerCycle,
        navAreaMaxPathValidationsPerCycle);

    int minerOrdinal = 0;
    VectorMap<String, int> demandAssignmentsByProfile;

    for (int minerIndex = 0; minerIndex < miners.size(); ++minerIndex) {
        MinerIntelligentTargetingMinerSnapshot miner = miners.get(minerIndex);
        String targetSource = "none";
        String selectedProfileKey = "none";
        ResourceIntelligenceEntry selectedResource;
        bool samePlanet = false;
        bool hasTarget = false;
        bool usingAssignment = false;
        MinerDensityTargetCandidate densityTarget;
        MinerIntelligentTargetAssignment assignment;
        uint64 nowMs = System::getMiliTime();

        if (minerIntelligentTargetingEnabled &&
                (minerIntelligentTargetingMode == "shadow" ||
                 minerIntelligentTargetingMode == "limited") &&
                minerIntelligentTargetingAssignmentEnabled &&
                getMinerIntelligentTargetAssignment(
                    miner.objectID, assignment)) {
	        uint64 timeoutAgeSeconds = 0;
	        uint64 timeoutSeconds = 0;
	        String timeoutReason = getMinerIntelligentAssignmentTimeoutReason(
	            assignment, nowMs, timeoutAgeSeconds, timeoutSeconds, true);

	        if (!timeoutReason.isEmpty()) {
	            clearMinerIntelligentTargetAssignment(
	                miner.objectID, timeoutReason, minerIntelligentTargetingMode);
	        } else {
	            if (assignment.normalTtlSkippedForActiveMovement)
	                putMinerIntelligentTargetAssignment(assignment);

	            if (assignment.targetZoneName != miner.zoneName) {
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID, "zoneChanged", minerIntelligentTargetingMode);
	            } else if (minerIntelligentTargetingAssignmentClearOnIncapOrDeath &&
	                    (miner.dead || miner.incapacitated)) {
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID,
	                    miner.dead ? String("dead") : String("incapacitated"),
	                    minerIntelligentTargetingMode);
	            } else if (minerIntelligentTargetingAssignmentClearOnCombat &&
	                    miner.inCombat) {
	                clearMinerIntelligentTargetAssignment(
	                    miner.objectID, "combat", minerIntelligentTargetingMode);
	            } else if (assignment.densityTargetStatus == "accepted") {
	                targetSource = assignment.targetSource;
	                selectedProfileKey = assignment.selectedProfileKey;
	                selectedResource.name = assignment.targetResourceName;
	                selectedResource.type = assignment.targetResourceType;
	                samePlanet = true;
	                hasTarget = true;
	                usingAssignment = true;
	                densityTarget.x = assignment.targetX;
	                densityTarget.y = assignment.targetY;
	                densityTarget.z = assignment.targetZ;
	                densityTarget.density = assignment.targetDensity;
	                densityTarget.distance =
	                    miner.position.distanceTo(
	                        Vector3(assignment.targetX, assignment.targetY, assignment.targetZ));
	                densityTarget.searchRadius = 1;
	            }
	        }
        }

        if (!hasTarget && pressureResults.size() > 0) {
            DemandWeightedPlanSelection demandSelection =
                selectDemandWeightedMinerPlanForValidation(
                    entries,
                    demandProfiles,
                    pressureResults,
                    demandAssignmentsByProfile,
                    miner.zoneName,
                    demandWeightedMinerPlanSimulationSamePlanetBonus,
                    demandWeightedMinerPlanSimulationTravelPenalty,
                    demandWeightedMinerPlanSimulationMaxMinersPerProfile,
                    demandWeightedMinerPlanSimulationStrongPressureRatio);

            if (demandSelection.valid) {
                targetSource = "demand_weighted_plan";
                selectedProfileKey = demandSelection.selectedResult.profileKey;
                selectedResource = demandSelection.selectedResource;
                samePlanet = demandSelection.selected.target.samePlanet;
                hasTarget = true;
            }
        }

        int assignedProfileIndex = -1;

        if (!hasTarget) {
            MinerTargetSimulationPlan plan = selectAssignedMinerTargetSimulationPlan(
                entries,
                roundRobinProfiles,
                enabledProfileIndexes,
                minerTargetSimulationProfileWeights,
                minerOrdinal,
                miner.zoneName,
                minerTargetSimulationPreferSamePlanet,
                minerTargetSimulationSamePlanetBonus,
                minerTargetSimulationTravelPenalty,
                assignedProfileIndex);

            if (plan.isValid()) {
                ResourceScoringProfile selectedProfile =
                    roundRobinProfiles.get(plan.profileIndex);
                targetSource = "round_robin_plan";
                selectedProfileKey = selectedProfile.key;
                selectedResource = entries.get(plan.resourceIndex);
                samePlanet = plan.samePlanet;
                hasTarget = true;
            }
        }

        if (!hasTarget) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " targetSource=none skipped=true reason=noTargetPlan mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        if (!samePlanet) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " profile=" + selectedProfileKey +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " targetSource=" + targetSource +
                " skipped=true reason=wrongPlanet mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        MinerDensityTargetDiagnostics densityDiagnostics;
        bool acceptedDensityTarget = usingAssignment;

        if (!usingAssignment) {
            acceptedDensityTarget = findMinerDensityTarget(
                miner.objectID,
                selectedProfileKey,
                targetSource,
                selectedResource,
                miner.zone,
                miner.position,
                minerDensityTargetSimulationSearchRadii,
                minerDensityTargetSimulationSamplesPerRadius,
                minerDensityTargetSimulationMinAcceptableDensity,
                minerDensityTargetSimulationRequireNavmesh,
                miner.inNavmesh,
                minerDensityTargetSimulationMaxPathCheckAttempts,
                minerDensityTargetSimulationDistancePenaltyPerMeter,
                reachabilityMemoryEnabled,
                reachabilityCandidatePreferenceEnabled,
                reachabilityBucketSizeMeters,
                reachabilityMinAttemptsBeforePenalty,
                reachabilityVerifiedPathScoreBonus,
                reachabilitySampleCompleteScoreBonus,
                reachabilityRepeatedFailurePenalty,
                reachabilityLongDistancePenalty512Plus,
                reachabilityMemoryTtlSeconds,
                reachabilityMaxMemoryRows,
                densityTarget,
                densityDiagnostics);
        }

        if (!usingAssignment) {
            MinerDensityTargetCandidate navAreaTarget;
            String navAreaSelectionMode;
            String navAreaReason;
            String navAreaName;
            String navAreaRole;
            bool navAreaFound = evaluateNavAreaDensitySelection(
                miner.objectID,
                selectedProfileKey,
                selectedResource,
                miner.zone,
                miner.position,
                navAreaDensitySelectionEnabled,
                navAreaDensitySelectionShadowMode,
                navAreaSampleCacheTtlSeconds,
                navAreaMaxSamplesPerArea,
                navAreaMaxSampleAttemptsPerCycle,
                navAreaMaxPathValidationsPerCycle,
                navAreaAvoidGenericInteriors,
                navAreaPreferCityAndPoiRegions,
                minerDensityTargetSimulationMinAcceptableDensity,
                minerDensityTargetSimulationDistancePenaltyPerMeter,
                navAreaTarget,
                navAreaSelectionMode,
                navAreaReason,
                navAreaName,
                navAreaRole);

            if (navAreaFound) {
                bool activeNavAreaSelection =
                    navAreaDensitySelectionEnabled &&
                    !navAreaDensitySelectionShadowMode;
                bool wouldSelectDifferent =
                    !densityTarget.isValid() ||
                    Vector3(
                        densityTarget.x,
                        densityTarget.y,
                        densityTarget.z).distanceTo(
                            Vector3(
                                navAreaTarget.x,
                                navAreaTarget.y,
                                navAreaTarget.z)) > 5.f;

                info(String("NavAreaDensitySelection miner=") +
                    String::valueOf(miner.objectID) +
                    " zone=" + miner.zoneName +
                    " profile=" + selectedProfileKey +
                    " resource=" + selectedResource.name +
                    " type=" + selectedResource.type +
                    " sourceArea=" + navAreaName +
                    " sourceRole=" + navAreaRole +
                    " densitySelectionMode=" + navAreaSelectionMode +
                    " activeSelection=" +
                        (activeNavAreaSelection ?
                            String("true") : String("false")) +
                    " wouldSelectDifferent=" +
                        (wouldSelectDifferent ?
                            String("true") : String("false")) +
                    " navTarget=(x:" +
                        String::valueOf(
                            Math::getPrecision(navAreaTarget.x, 1)) +
                    ",y:" +
                        String::valueOf(
                            Math::getPrecision(navAreaTarget.y, 1)) +
                    ",z:" +
                        String::valueOf(
                            Math::getPrecision(navAreaTarget.z, 1)) +
                    ")" +
                    " navDensity=" +
                        String::valueOf(
                            Math::getPrecision(navAreaTarget.density, 3)) +
                    " navScore=" +
                        String::valueOf(
                            Math::getPrecision(
                                navAreaTarget.adjustedScore, 1)) +
                    " reason=" + navAreaReason +
                    " mode=" +
                        (activeNavAreaSelection ?
                            String("active") : String("shadow-only")),
                    true);

                if (activeNavAreaSelection) {
                    densityTarget = navAreaTarget;
                    acceptedDensityTarget = true;
                }
            }
        }

        if (!acceptedDensityTarget && !minerPathValidationOnlyAcceptedDensityTargets &&
                densityDiagnostics.hasBestRejectedCandidate()) {
            densityTarget = densityDiagnostics.bestRejectedCandidate;
            densityTarget.z = miner.zone->getHeight(densityTarget.x, densityTarget.y);
        }

        if (!densityTarget.isValid()) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " profile=" + selectedProfileKey +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " targetSource=" + targetSource +
                " assignmentStatus=" +
                    (usingAssignment ?
                        (assignment.status.isEmpty() ?
                            String("candidate") : assignment.status) :
                        String("none")) +
                " skipped=true reason=noAcceptedDensityTarget" +
                " densityRejectReason=" + densityDiagnostics.rejectReason +
                " mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        Vector3 targetPosition(densityTarget.x, densityTarget.y, densityTarget.z);
        float directDistance = miner.position.distanceTo(targetPosition);

        if (directDistance > static_cast<float>(minerPathValidationMaxPathDistance)) {
            bool targetNavmeshChecked = miner.zone != nullptr;
            bool targetInNavmesh = targetNavmeshChecked ?
                isPointInAnyNavmesh(
                    miner.zone,
                    targetPosition.getX(),
                    targetPosition.getY()) :
                false;
            bool targetTerrainHeightKnown = miner.zone != nullptr;
            float targetTerrainHeight = targetTerrainHeightKnown ?
                miner.zone->getHeight(
                    targetPosition.getX(),
                    targetPosition.getY()) : 0.f;

            MinerPathValidationSnapshot snapshot;
            snapshot.assignmentGenerationId =
                usingAssignment ? assignment.assignmentGenerationId : 0;
            snapshot.targetHash = usingAssignment && !assignment.targetHash.isEmpty() ?
                assignment.targetHash :
                buildMinerAssignmentTargetHash(
                    targetSource,
                    selectedProfileKey,
                    selectedResource.name,
                    selectedResource.type,
                    miner.zoneName,
                    targetPosition.getX(),
                    targetPosition.getY(),
                    targetPosition.getZ());
            snapshot.zoneName = miner.zoneName;
            snapshot.profileKey = selectedProfileKey;
            snapshot.resourceName = selectedResource.name;
            snapshot.resourceType = selectedResource.type;
            snapshot.targetSource = targetSource;
            snapshot.acceptedDensityTarget = acceptedDensityTarget;
            snapshot.pathFound = false;
            snapshot.rejectReason = "exceedsMaxPathDistance";
            snapshot.pathTrustStatus = "exceedsMaxPathDistance";
            snapshot.pathNodes = 0;
            snapshot.pathDistance = 0.f;
            snapshot.density = densityTarget.density;
            snapshot.directDistance = directDistance;
            snapshot.targetX = targetPosition.getX();
            snapshot.targetY = targetPosition.getY();
            snapshot.targetZ = targetPosition.getZ();
            snapshot.minerX = miner.position.getX();
            snapshot.minerY = miner.position.getY();
            snapshot.minerZ = miner.position.getZ();
            snapshot.directFallback = false;
            snapshot.minerInNavmeshKnown = true;
            snapshot.minerInNavmesh = miner.inNavmesh;
            snapshot.targetNavmeshChecked = targetNavmeshChecked;
            snapshot.targetInNavmesh = targetInNavmesh;
            snapshot.targetTerrainHeightKnown = targetTerrainHeightKnown;
            snapshot.targetTerrainHeight = targetTerrainHeight;
            snapshot.targetZDelta = targetTerrainHeightKnown ?
                targetPosition.getZ() - targetTerrainHeight : 0.f;
            snapshot.maxPathDistance = minerPathValidationMaxPathDistance;
            snapshot.maxPathNodes = minerPathValidationMaxPathNodes;
            snapshot.recordedAtMs = System::getMiliTime();
            recordMinerPathValidationSnapshot(miner.objectID, snapshot);

            info(String("MinerPathValidationSimulation miner=") + String::valueOf(miner.objectID) +
                " zone=" + miner.zoneName +
                " profile=" + selectedProfileKey +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " targetSource=" + targetSource +
                " assignmentStatus=" +
                    (usingAssignment ?
                        (assignment.status.isEmpty() ?
                            String("candidate") : assignment.status) :
                        String("none")) +
                " target=(x:" + String::valueOf(Math::getPrecision(targetPosition.getX(), 1)) +
                ",y:" + String::valueOf(Math::getPrecision(targetPosition.getY(), 1)) +
                ",z:" + String::valueOf(Math::getPrecision(targetPosition.getZ(), 1)) + ")" +
                " density=" + String::valueOf(Math::getPrecision(densityTarget.density, 3)) +
                " distance=" + String::valueOf(Math::getPrecision(directDistance, 1)) +
                " skipped=true reason=exceedsMaxPathDistance mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        Reference<MinerPathValidationTask*> pathTask = new MinerPathValidationTask(
            miner.objectID,
            usingAssignment ? assignment.assignmentGenerationId : 0,
            usingAssignment && !assignment.targetHash.isEmpty() ?
                assignment.targetHash :
                buildMinerAssignmentTargetHash(
                    targetSource,
                    selectedProfileKey,
                    selectedResource.name,
                    selectedResource.type,
                    miner.zoneName,
                    targetPosition.getX(),
                    targetPosition.getY(),
                    targetPosition.getZ()),
            miner.zoneName,
            selectedProfileKey,
            selectedResource.name,
            selectedResource.type,
            targetSource,
            miner.position,
            targetPosition,
            densityTarget.density,
            directDistance,
            minerPathValidationMaxPathDistance,
            minerPathValidationMaxPathNodes,
            acceptedDensityTarget,
            miner.inNavmesh,
            miner.zone);
        pathTask->schedule(0);

        ++minerOrdinal;
    }
}

uint64 SimPlayerManager::recordMinerPathValidationSnapshot(
        uint64 minerID, MinerPathValidationSnapshot& snapshot) {
    if (minerID == 0)
        return 0;

    Locker locker(&minerPathValidationSnapshotMutex);
    if (snapshot.validationSnapshotId == 0)
        snapshot.validationSnapshotId = nextMinerPathValidationSnapshotId++;

    minerPathValidationSnapshots.put(minerID, snapshot);
    return snapshot.validationSnapshotId;
}

bool SimPlayerManager::getMinerPathValidationSnapshot(
        uint64 minerID, MinerPathValidationSnapshot& snapshot) {
    if (minerID == 0)
        return false;

    Locker locker(&minerPathValidationSnapshotMutex);

    if (!minerPathValidationSnapshots.contains(minerID))
        return false;

    snapshot = minerPathValidationSnapshots.get(minerID);
    return true;
}

void SimPlayerManager::startControllerForAgent(AiAgent* agent, Reference<SimPlayerController*> ctrl) {
    if (!enabled || agent == nullptr || ctrl == nullptr)
        return;

    uint64 oid = agent->getObjectID();

    // Shared “Starting SimPlayer” flags (copied from your toggleBot start path)
    agent->setCustomAiMap(String("patrol").hashCode());
    agent->setAITemplate();

    agent->writeBlackboard("simAlwaysActive", true);
    agent->setSimAlwaysActive(true);
    agent->setSimPlayerBot(true);
    agent->setDespawnOnNoPlayerInRange(false);

    controllers.put(oid, ctrl);

    agent->activateAiBehavior(true);
    ctrl->startSimLoop();
}

static bool inferImperialFromTemplateName(const String& templateNameOrTName) {
    String lower = templateNameOrTName.toLowerCase();
    bool looksRebel = lower.beginsWith("rebel");
    bool looksImperial = lower.beginsWith("imperial") || lower.contains("stormtrooper");
    if (looksRebel) return false;
    if (looksImperial) return true;
    // default if unknown
    return true;
}

void SimPlayerManager::spawnSimPlayerWithRoute(const String& planet,
                                              const Vector3& spawn,
                                              const Vector3& hangout,
                                              const String& templateName,
                                              const String& groupType,
                                              const String& locationName) {
    if (!enabled)
        return;

    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return;

    Zone* zone = zoneServer->getZone(planet);
    if (zone == nullptr) {
        info("Could not find zone: " + planet, true);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr) return;

    // Compute good Z from navmesh/terrain
    float spawnZ = zone->getHeight(spawn.getX(), spawn.getY());
    if (spawnZ == 0.0f && spawn.getZ() != 0.0f) spawnZ = spawn.getZ();

    float hangoutZ = zone->getHeight(hangout.getX(), hangout.getY());
    if (hangoutZ == 0.0f && hangout.getZ() != 0.0f) hangoutZ = hangout.getZ();

    Vector3 spawnPos(spawn.getX(), spawn.getY(), spawnZ);
    Vector3 hangoutPos(hangout.getX(), hangout.getY(), hangoutZ);

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0,
                                                              spawnPos.getX(), spawnPos.getZ(), spawnPos.getY(), 0);
    if (creature == nullptr) {
        info("Failed to spawn SimPlayer template: " + templateName, true);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr) return;

    // Reset default flags to clean slate
    agent->setCreatureBitmask(0);
    agent->setDespawnOnNoPlayerInRange(false);

    // Decide controller based on groupType first (authoritative)
    Reference<SimPlayerController*> ctrl = nullptr;

    if (groupType.beginsWith("pvp")) {
        bool imperial = inferImperialFromTemplateName(templateName);

        agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

        SimPvPController* pvp = new SimPvPController(agent, imperial, spawnPos, hangoutPos);
        pvp->setCycleContext(this, templateName, groupType, planet, locationName);
        ctrl = pvp;
    } else {
        agent->setPvpStatusBitmask(0);
        ctrl = new SimMinerController(agent);
    }
#ifdef DEBUG_SIMPLAYER
    info("spawnSimPlayerWithRoute: spawned " + templateName +
         " at " + planet + ":" + locationName +
         " spawn=(" + String::valueOf(spawnPos.getX()) + "," + String::valueOf(spawnPos.getY()) + ")" +
         " hangout=(" + String::valueOf(hangoutPos.getX()) + "," + String::valueOf(hangoutPos.getY()) + ")",
         true);
#endif
    startControllerForAgent(agent, ctrl);
}

// -----------------------------------------------------------------------------
// Lua config glue
// -----------------------------------------------------------------------------

static Vector3 readVec3FromLua(LuaObject& arr) {
    if (!arr.isValidTable() || arr.getTableSize() < 3)
        return Vector3(0, 0, 0);

    float x = (float)arr.getFloatAt(1);
    float y = (float)arr.getFloatAt(2);
    float z = (float)arr.getFloatAt(3);
    return Vector3(x, y, z);
}

void SimPlayerManager::spawnConfiguredGroups() {
    if (!enabled)
        return;

    if (spawnGroups.size() == 0 || allShuttleports.size() == 0) {
#ifdef DEBUG_SIMPLAYER
        info("SimPlayerManager has no config to spawn from (spawnGroups/shuttleports empty).");
#endif
        return;
    }

    for (int gi = 0; gi < spawnGroups.size(); ++gi) {
        const SpawnGroup& g = spawnGroups.get(gi);
        for (int c = 0; c < g.totalCount; ++c) {
            ShuttleportLocation loc;
            if (!pickRandomShuttleport(loc))
                return;

            String tmpl = pickRandomTemplate(g);
            spawnFromConfig(g, loc, tmpl);
        }
    }
}

bool SimPlayerManager::pickRandomShuttleport(ShuttleportLocation& out) const {
    if (allShuttleports.size() == 0)
        return false;

    int idx = System::random(allShuttleports.size() - 1);
    out = allShuttleports.get(idx);
    return true;
}

String SimPlayerManager::pickRandomTemplate(const SpawnGroup& g) const {
    if (g.templates.size() == 0) {
        // Reasonable fallbacks for empty template list
        if (g.type.beginsWith("pvp"))
            return "stormtrooper";
        return "artisan";
    }

    int idx = System::random(g.templates.size() - 1);
    return g.templates.get(idx);
}

bool SimPlayerManager::isImperialForSpawn(const SpawnGroup& g, const String& templateName) const {
    // Explicit override in Lua
    if (g.faction == "imperial")
        return true;
    if (g.faction == "rebel")
        return false;
    if (g.faction == "random")
        return (System::random(1) == 1);

    // Infer from template naming
    String lower = templateName;
    lower = lower.toLowerCase();
    if (lower.beginsWith("rebel"))
        return false;
    if (lower.beginsWith("imperial") || lower.contains("stormtrooper"))
        return true;

    // Default: random
    return (System::random(1) == 1);
}

void SimPlayerManager::spawnFromConfig(const SpawnGroup& g, const ShuttleportLocation& loc, const String& templateName) {
    if (!enabled)
        return;

    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr)
        return;

    Zone* zone = zoneServer->getZone(loc.planet);
    if (zone == nullptr) {
        info("Could not find zone: " + loc.planet);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr)
        return;

    // Use nav height when available; fall back to Lua-provided Z if needed.
    float spawnZ = zone->getHeight(loc.spawn.getX(), loc.spawn.getY());
    if (spawnZ == 0.0f && loc.spawn.getZ() != 0.0f)
        spawnZ = loc.spawn.getZ();

    float hangoutZ = zone->getHeight(loc.hangout.getX(), loc.hangout.getY());
    if (hangoutZ == 0.0f && loc.hangout.getZ() != 0.0f)
        hangoutZ = loc.hangout.getZ();

    Vector3 spawnPos(loc.spawn.getX(), loc.spawn.getY(), spawnZ);
    Vector3 hangoutPos(loc.hangout.getX(), loc.hangout.getY(), hangoutZ);
#ifdef DEBUG_SIMPLAYER
    info("Spawning SimPlayer type=" + g.type + " template=" + templateName + " planet=" + loc.planet + " loc=" + loc.name, true);
#endif
    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, spawnPos.getX(), spawnPos.getZ(), spawnPos.getY(), 0);
    if (creature == nullptr) {
        info("Failed to spawn SimPlayer template: " + templateName);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr)
        return;

    // Reset default flags to clean slate
    agent->setCreatureBitmask(0);
    agent->setDespawnOnNoPlayerInRange(false);

    // Start (non-toggle) using the same flags as toggleBot's "Starting" path
    uint64 oid = agent->getObjectID();

    agent->setCustomAiMap(String("patrol").hashCode());
    agent->setAITemplate();

    agent->writeBlackboard("simAlwaysActive", true);
    agent->setSimAlwaysActive(true);
    agent->setSimPlayerBot(true);
    agent->setDespawnOnNoPlayerInRange(false);

    Reference<SimPlayerController*> ctrl = nullptr;

    if (g.type.beginsWith("pvp")) {
        bool imperial = isImperialForSpawn(g, templateName);
        agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);
#ifdef DEBUG_SIMPLAYER
        info("spawnFromConfig: creating PvP controller with route spawn=("
            + String::valueOf(spawnPos.getX()) + "," + String::valueOf(spawnPos.getY()) + "," + String::valueOf(spawnPos.getZ())
            + ") hangout=("
            + String::valueOf(hangoutPos.getX()) + "," + String::valueOf(hangoutPos.getY()) + "," + String::valueOf(hangoutPos.getZ())
            + ")", true);
#endif
        SimPvPController* pvp = new SimPvPController(agent, imperial, spawnPos, hangoutPos);
        pvp->setCycleContext(this, templateName, g.type, loc.planet, loc.name);
#ifdef DEBUG_SIMPLAYER
        Logger::console.info(
            "SimPlayerManager: spawnFromConfig wired cycle context oid=" + String::valueOf(agent->getObjectID()) +
            " mgr=this groupType=" + g.type +
            " template=" + templateName +
            " planet=" + loc.planet +
            " location=" + loc.name,
            true
        );
#endif
        ctrl = pvp;
    } else {
        agent->setPvpStatusBitmask(0);
        ctrl = new SimMinerController(agent, g.minerConfig);
    }
        controllers.put(oid, ctrl);
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }

void SimPlayerManager::cyclePvPBotWhenShuttleReady(uint64 oldOid,
                                                   const String& groupType,
                                                   const String& templateName,
                                                   bool imperial,
                                                   const String& fromPlanet,
                                                   const String& fromLocation,
                                                   int attempts) {
    if (!enabled)
        return;

    // Safety: don’t wait forever
    if (attempts >= 24) { // 24 * 5s = ~2 minutes
#ifdef DEBUG_SIMPLAYER
        info("cyclePvPBotWhenShuttleReady: timeout waiting for shuttle; cycling anyway oldOid=" +
             String::valueOf(oldOid), true);
#endif
        cyclePvPBot(oldOid, groupType, templateName, imperial, fromPlanet, fromLocation);
        return;
    }

    // Re-acquire the old agent by OID (don’t capture oldAgent across tasks)
    ManagedReference<SceneObject*> obj = ServerCore::getZoneServer()->getObject(oldOid);
    ManagedReference<AiAgent*> oldAgent = cast<AiAgent*>(obj.get());

    if (oldAgent == nullptr) {
#ifdef DEBUG_SIMPLAYER
        info("cyclePvPBotWhenShuttleReady: oldAgent null, abort oldOid=" + String::valueOf(oldOid), true);
#endif
        return;
    }

    bool cleanupOldAgent = false;

    {
        Locker locker(oldAgent);
        // If the bot died while waiting, stop the loop and clean up.
        if (oldAgent->isDead() || oldAgent->isIncapacitated()) {
#ifdef DEBUG_SIMPLAYER
            info("cyclePvPBotWhenShuttleReady: old bot is dead/incap; cleaning up oldOid=" +
                 String::valueOf(oldOid), true);
#endif
            cleanupOldAgent = true;
        }
    }

    if (cleanupOldAgent) {
        controllers.drop(oldOid);

        // If you want NO corpses/loot for simplayers:
        oldAgent->destroyObjectFromWorld(true);
        oldAgent->destroyObjectFromDatabase(true);

        return;
    }

    // Do not hold the bot lock while checking PlanetManager/shuttle state.
    if (!isNearestShuttleBoardable(oldAgent)) {
        {
            Locker locker(oldAgent);

            if (oldAgent->isDead() || oldAgent->isIncapacitated()) {
#ifdef DEBUG_SIMPLAYER
                info("cyclePvPBotWhenShuttleReady: old bot died while waiting for shuttle oldOid=" +
                     String::valueOf(oldOid), true);
#endif
                cleanupOldAgent = true;
            } else {
                // Optional: make it look like it’s waiting
                oldAgent->setMovementState(AiAgent::OBLIVIOUS);
                oldAgent->activateAiBehavior(true);
            }
        }

        if (cleanupOldAgent) {
            controllers.drop(oldOid);

            // If you want NO corpses/loot for simplayers:
            oldAgent->destroyObjectFromWorld(true);
            oldAgent->destroyObjectFromDatabase(true);

            return;
        }

        Core::getTaskManager()->scheduleTask(
            [this, oldOid, groupType, templateName, imperial, fromPlanet, fromLocation, attempts]() {
                this->cyclePvPBotWhenShuttleReady(oldOid, groupType, templateName, imperial, fromPlanet, fromLocation, attempts + 1);
            },
            "SimPvPWaitForShuttle",
            5000
        );

        return;
    }

    // Shuttle is boardable now — do the real cycle
    cyclePvPBot(oldOid, groupType, templateName, imperial, fromPlanet, fromLocation);
}

bool SimPlayerManager::isNearestShuttleBoardable(CreatureObject* creature) {
    if (creature == nullptr)
        return false;

    Zone* zone = creature->getZone();
    if (zone == nullptr)
        return false;

    PlanetManager* pm = zone->getPlanetManager();
    if (pm == nullptr)
        return false;

    Reference<PlanetTravelPoint*> ptp = pm->getNearestPlanetTravelPoint(creature, 128.f);
    if (ptp == nullptr)
        return false;

    ManagedReference<CreatureObject*> shuttle = ptp->getShuttle();
    if (shuttle == nullptr)
        return false;

    // Match BoardShuttleCommand behavior (don’t special-case Theed Spaceport unless you want to)
    if (!ptp->isPoint("naboo", "Theed Spaceport")) {
        return pm->checkShuttleStatus(creature, shuttle);
    }

    return true;
}

void SimPlayerManager::cyclePvPBot(uint64 oldOid,
                                   const String& groupType,
                                   const String& templateName,
                                   bool imperial,
                                   const String& fromPlanet,
                                   const String& fromLocation) {
    if (!enabled)
        return;

#ifdef DEBUG_SIMPLAYER
    info("cyclePvPBot ENTER this=" + String::valueOf((uint64)this) +
         " oldOid=" + String::valueOf(oldOid) +
         " controllersHas=" + String::valueOf(controllers.contains(oldOid)) +
         " from " + fromPlanet + ":" + fromLocation +
         " groupType=" + groupType + " template=" + templateName +
         " shuttleports=" + String::valueOf(allShuttleports.size()) +
         " spawnGroups=" + String::valueOf(spawnGroups.size()), true);
#endif
    // Run on task thread (you already do this style elsewhere)
    Core::getTaskManager()->scheduleTask([this, oldOid, groupType, templateName, imperial, fromPlanet, fromLocation]() {
#ifdef DEBUG_SIMPLAYER
        info("cyclePvPBot TASK START this=" + String::valueOf((uint64)this) +
             " oldOid=" + String::valueOf(oldOid) +
             " enabled=" + String::valueOf(enabled) +
             " shuttleports=" + String::valueOf(allShuttleports.size()) +
             " spawnGroups=" + String::valueOf(spawnGroups.size()), true);
#endif
        if (!controllers.contains(oldOid)) {
#ifdef DEBUG_SIMPLAYER
            info("cyclePvPBot: oldOid no longer in controllers (already cleaned up?)", true);
#endif
            return;
        }

        // If config wasn't loaded (or got wiped), try loading once.
        // With the new loadLuaConfig() this won't destroy good config on failure.
        if (!enabled || allShuttleports.size() == 0 || spawnGroups.size() == 0) {
#ifdef DEBUG_SIMPLAYER
            info("BEFORE loadLuaConfig: enabled=" + String::valueOf(enabled) +
                 " shuttles=" + String::valueOf(allShuttleports.size()) +
                 " groups=" + String::valueOf(spawnGroups.size()), true);
#endif
            loadLuaConfig();
#ifdef DEBUG_SIMPLAYER
            info("AFTER  loadLuaConfig: enabled=" + String::valueOf(enabled) +
                 " shuttles=" + String::valueOf(allShuttleports.size()) +
                 " groups=" + String::valueOf(spawnGroups.size()), true);
#endif
        }

        if (!enabled || allShuttleports.size() == 0 || spawnGroups.size() == 0) {
#ifdef DEBUG_SIMPLAYER
            info("cyclePvPBot: cannot cycle because config still empty/disabled.", true);
#endif
            return;
        }

        // 1) Pick a new location (try not to repeat)
        ShuttleportLocation newLoc;
        bool pickedLoc = false;

        for (int tries = 0; tries < 10; ++tries) {
            if (!pickRandomShuttleport(newLoc))
                return;

            if (!(newLoc.planet == fromPlanet && newLoc.name == fromLocation)) {
                pickedLoc = true;
                break;
            }
        }

        if (!pickedLoc) {
            // Could not find a different one after N tries — allow same
            if (!pickRandomShuttleport(newLoc))
                return;
        }

        // 2) Find spawn group
        SpawnGroup picked;
        bool found = false;

        for (int i = 0; i < spawnGroups.size(); ++i) {
            if (spawnGroups.get(i).type == groupType) {
                picked = spawnGroups.get(i);
                found = true;
                break;
            }
        }

        if (!found) {
#ifdef DEBUG_SIMPLAYER
            info("cyclePvPBot: could not find spawnGroup type=" + groupType + ", falling back to first pvp group", true);
#endif
            for (int i = 0; i < spawnGroups.size(); ++i) {
                if (spawnGroups.get(i).type.beginsWith("pvp")) {
                    picked = spawnGroups.get(i);
                    found = true;
                    break;
                }
            }
            if (!found)
                return;
        }

        // 3) Spawn replacement
        String tmpl = templateName;
        if (tmpl.isEmpty())
            tmpl = pickRandomTemplate(picked);
#ifdef DEBUG_SIMPLAYER
        info("Cycling PvP bot " + String::valueOf(oldOid) +
             " from " + fromPlanet + ":" + fromLocation +
             " -> " + newLoc.planet + ":" + newLoc.name +
             " template=" + tmpl, true);
#endif
        spawnFromConfig(picked, newLoc, tmpl);

        // 4) Drop controller first
        controllers.drop(oldOid);

        // 5) Destroy old object via ZoneServer lookup
        ZoneServer* zoneServer = ServerCore::getZoneServer();
        if (zoneServer == nullptr)
            return;

        ManagedReference<SceneObject*> obj = zoneServer->getObject(oldOid);
        if (obj == nullptr)
            return;

        ManagedReference<AiAgent*> oldAgent = cast<AiAgent*>(obj.get());
        if (oldAgent == nullptr)
            return;

        bool oldAgentDeadOrIncap = false;

        {
            Locker locker(oldAgent);
            oldAgentDeadOrIncap = oldAgent->isDead() || oldAgent->isIncapacitated();
        }

        if (oldAgentDeadOrIncap) {
#ifdef DEBUG_SIMPLAYER
            info("cyclePvPBot: old bot already dead/incap; destroying oldOid=" + String::valueOf(oldOid), true);
#endif
            oldAgent->destroyObjectFromWorld(true);
            oldAgent->destroyObjectFromDatabase(true);
            return;
        }
#ifdef DEBUG_SIMPLAYER
        info("cyclePvPBot: destroying oldOid=" + String::valueOf(oldOid), true);
#endif
        oldAgent->destroyObjectFromWorld(true);
#ifdef DEBUG_SIMPLAYER
        info("cyclePvPBot: destroyObjectFromWorld done oldOid=" + String::valueOf(oldOid), true);
#endif
        oldAgent->destroyObjectFromDatabase(true);
#ifdef DEBUG_SIMPLAYER
        info("cyclePvPBot: destroyObjectFromDatabase done oldOid=" + String::valueOf(oldOid), true);
#endif
    }, "CyclePvPBot", 0);
}
