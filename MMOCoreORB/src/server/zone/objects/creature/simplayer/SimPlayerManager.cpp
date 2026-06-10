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

class MinerPathValidationTask : public Task {
    uint64 minerID;
    String zoneName;
    String profileKey;
    String resourceName;
    String resourceType;
    Vector3 startPosition;
    Vector3 targetPosition;
    float density;
    float directDistance;
    int maxPathDistance;
    int maxPathNodes;
    bool acceptedDensityTarget;
    ManagedReference<Zone*> zone;

public:
    MinerPathValidationTask(
            uint64 minerID,
            const String& zoneName,
            const String& profileKey,
            const String& resourceName,
            const String& resourceType,
            const Vector3& startPosition,
            const Vector3& targetPosition,
            float density,
            float directDistance,
            int maxPathDistance,
            int maxPathNodes,
            bool acceptedDensityTarget,
            Zone* zone)
        : minerID(minerID),
          zoneName(zoneName),
          profileKey(profileKey),
          resourceName(resourceName),
          resourceType(resourceType),
          startPosition(startPosition),
          targetPosition(targetPosition),
          density(density),
          directDistance(directDistance),
          maxPathDistance(maxPathDistance),
          maxPathNodes(maxPathNodes),
          acceptedDensityTarget(acceptedDensityTarget),
          zone(zone) {
    }

    void run() override;
};

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
    uint64 totalKnownSupply = 0;
    uint64 shortageUnits = 0;
    uint64 surplusUnits = 0;
    int marketListingsMatched = 0;
    float marketCheapestPricePerUnit = -1.f;
    float marketMedianPricePerUnit = -1.f;
    float reserveRatio = 0.f;
    float shortagePressure = 0.f;
    float opportunityPressure = 0.f;
    float pressureScore = 0.f;
    bool hasActiveOpportunity = false;
    bool activeProfileAvailableForPhase = true;
    String marketSupplyConfidence = "none";
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

struct MinerDensityTargetCandidate {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float density = 0.f;
    float distance = 0.f;
    float adjustedScore = 0.f;
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

static bool densityCandidateIndexUsed(const Vector<int>& usedIndexes, int index) {
    for (int i = 0; i < usedIndexes.size(); ++i) {
        if (usedIndexes.get(i) == index)
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

    SortedVector<ManagedReference<NavArea*>> areas;
    zone->getInRangeNavMeshes(x, y, &areas, false);

    for (const auto& area : areas) {
        if (area != nullptr && area->containsPoint(x, y))
            return true;
    }

    return false;
}

static bool findMinerDensityTarget(
        uint64 minerID,
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
            candidate.adjustedScore = candidate.density * 1000.f -
                candidate.distance * distancePenaltyPerMeter;
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

    String line = String("MinerPathValidationSimulation miner=") + String::valueOf(minerID) +
        " zone=" + zoneName +
        " profile=" + profileKey +
        " resource=" + resourceName +
        " type=" + resourceType +
        " target=(x:" + String::valueOf(Math::getPrecision(targetPosition.getX(), 1)) +
        ",y:" + String::valueOf(Math::getPrecision(targetPosition.getY(), 1)) +
        ",z:" + String::valueOf(Math::getPrecision(targetPosition.getZ(), 1)) + ")" +
        " density=" + String::valueOf(Math::getPrecision(density, 3)) +
        " distance=" + String::valueOf(Math::getPrecision(directDistance, 1)) +
        " densityTarget=" + (acceptedDensityTarget ? String("accepted") : String("rejected")) +
        " pathFound=" + (pathFound ? String("true") : String("false")) +
        " pathNodes=" + String::valueOf(pathNodes) +
        " pathDistance=" + String::valueOf(Math::getPrecision(pathDistance, 1)) +
        " directFallback=" + (directFallback ? String("true") : String("false"));

    if (!rejectReason.isEmpty())
        line += " rejectReason=" + rejectReason;

    line += " mode=simulation-only";
    manager->info(line, true);

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
    scheduleDemandStateSimulationTask();
    scheduleDemandWeightedMinerPlanSimulationTask();
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

    LuaObject demandWeightedMinerPlanSimulationConfig =
        config.getObjectField("demandWeightedMinerPlanSimulationConfig");
    if (demandWeightedMinerPlanSimulationConfig.isValidTable())
        applyDemandWeightedMinerPlanSimulationConfig(
            demandWeightedMinerPlanSimulationConfig);
    demandWeightedMinerPlanSimulationConfig.pop();
    applyDemandWeightedMinerPlanDependencyConfig(config);

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
            reason = result.marketObservedSupply > 0 ?
                String("reserve met by known supply including observed market supply; active opportunity dampened") :
                String("reserve met; active opportunity dampened");
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

    Vector<ResourceScoringProfile> profiles = createCuratedResourceScoringProfiles();
    Vector<int> enabledProfileIndexes;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        ResourceScoringProfile profile = profiles.get(profileIndex);
        float profileWeight = getMinerTargetSimulationProfileWeight(minerTargetSimulationProfileWeights, profile.key);

        if (profileWeight > 0.f)
            enabledProfileIndexes.add(profileIndex);
    }

    if (enabledProfileIndexes.size() == 0) {
        info("MinerDensityTargetSimulation skipped=true reason=noEnabledProfiles mode=simulation-only", true);
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

        Zone* zone = agent->getZone();

        if (zone == nullptr)
            continue;

        uint64 minerID = agent->getObjectID();
        String zoneName = zone->getZoneName();
        Vector3 minerPosition = agent->getWorldPosition();
        int assignedProfileIndex = -1;
        MinerTargetSimulationPlan plan = selectAssignedMinerTargetSimulationPlan(
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

        if (!plan.isValid()) {
            String assignedProfile = assignedProfileIndex >= 0 ?
                profiles.get(assignedProfileIndex).key : String("none");

            info(String("MinerDensityTargetSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " assignedProfile=" + assignedProfile +
                " noEligibleTarget=true noDensityTarget=true" +
                " minAcceptableDensity=" +
                String::valueOf(Math::getPrecision(minerDensityTargetSimulationMinAcceptableDensity, 3)) +
                " candidateCount=0 rejectReason=noValidCandidate mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        ResourceScoringProfile selectedProfile = profiles.get(plan.profileIndex);
        ResourceIntelligenceEntry selectedResource = entries.get(plan.resourceIndex);

        if (!plan.samePlanet) {
            info(String("MinerDensityTargetSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " profile=" + selectedProfile.key +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
                " zones=" + (selectedResource.zones.isEmpty() ? String("unknown") : selectedResource.zones) +
                " travelRequired=true noSamePlanetDensityTarget=true noDensityTarget=true" +
                " minAcceptableDensity=" +
                String::valueOf(Math::getPrecision(minerDensityTargetSimulationMinAcceptableDensity, 3)) +
                " candidateCount=0 rejectReason=wrongPlanet mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        MinerDensityTargetCandidate densityTarget;
        MinerDensityTargetDiagnostics densityDiagnostics;
        bool minerInNavmesh = agent->isInNavMesh();
        bool foundTarget = findMinerDensityTarget(
            minerID,
            selectedResource,
            zone,
            minerPosition,
            minerDensityTargetSimulationSearchRadii,
            minerDensityTargetSimulationSamplesPerRadius,
            minerDensityTargetSimulationMinAcceptableDensity,
            minerDensityTargetSimulationRequireNavmesh,
            minerInNavmesh,
            minerDensityTargetSimulationMaxPathCheckAttempts,
            minerDensityTargetSimulationDistancePenaltyPerMeter,
            densityTarget,
            densityDiagnostics);

        if (foundTarget) {
            String reason = densityTarget.density >= minerDensityTargetSimulationPreferredDensity ?
                String("nearest preferred pocket") : String("nearest acceptable pocket");

            info(String("MinerDensityTargetSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " profile=" + selectedProfile.key +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
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
            String rejectionLine = String("MinerDensityTargetSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " profile=" + selectedProfile.key +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
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

    Vector<ResourceScoringProfile> profiles = createCuratedResourceScoringProfiles();
    Vector<int> enabledProfileIndexes;

    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        ResourceScoringProfile profile = profiles.get(profileIndex);
        float profileWeight = getMinerTargetSimulationProfileWeight(minerTargetSimulationProfileWeights, profile.key);

        if (profileWeight > 0.f)
            enabledProfileIndexes.add(profileIndex);
    }

    if (enabledProfileIndexes.size() == 0) {
        info("MinerPathValidationSimulation skipped=true reason=noEnabledProfiles mode=simulation-only", true);
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

        ManagedReference<Zone*> zone;
        uint64 minerID = 0;
        String zoneName;
        Vector3 minerPosition;
        bool minerInNavmesh = false;

        {
            Locker agentLocker(agent);
            zone = agent->getZone();

            if (zone != nullptr) {
                minerID = agent->getObjectID();
                zoneName = zone->getZoneName();
                minerPosition = agent->getWorldPosition();
                minerInNavmesh = agent->isInNavMesh();
            }
        }

        if (zone == nullptr)
            continue;

        int assignedProfileIndex = -1;
        MinerTargetSimulationPlan plan = selectAssignedMinerTargetSimulationPlan(
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

        if (!plan.isValid()) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " skipped=true reason=noAcceptedDensityTarget mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        ResourceScoringProfile selectedProfile = profiles.get(plan.profileIndex);
        ResourceIntelligenceEntry selectedResource = entries.get(plan.resourceIndex);

        if (!plan.samePlanet) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " profile=" + selectedProfile.key +
                " resource=" + selectedResource.name +
                " skipped=true reason=wrongPlanet mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        MinerDensityTargetCandidate densityTarget;
        MinerDensityTargetDiagnostics densityDiagnostics;
        bool acceptedDensityTarget = findMinerDensityTarget(
            minerID,
            selectedResource,
            zone,
            minerPosition,
            minerDensityTargetSimulationSearchRadii,
            minerDensityTargetSimulationSamplesPerRadius,
            minerDensityTargetSimulationMinAcceptableDensity,
            minerDensityTargetSimulationRequireNavmesh,
            minerInNavmesh,
            minerDensityTargetSimulationMaxPathCheckAttempts,
            minerDensityTargetSimulationDistancePenaltyPerMeter,
            densityTarget,
            densityDiagnostics);

        if (!acceptedDensityTarget && !minerPathValidationOnlyAcceptedDensityTargets &&
                densityDiagnostics.hasBestRejectedCandidate()) {
            densityTarget = densityDiagnostics.bestRejectedCandidate;
            densityTarget.z = zone->getHeight(densityTarget.x, densityTarget.y);
        }

        if (!densityTarget.isValid()) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " profile=" + selectedProfile.key +
                " resource=" + selectedResource.name +
                " skipped=true reason=noAcceptedDensityTarget" +
                " densityRejectReason=" + densityDiagnostics.rejectReason +
                " mode=simulation-only", true);
            ++minerOrdinal;
            continue;
        }

        Vector3 targetPosition(densityTarget.x, densityTarget.y, densityTarget.z);
        float directDistance = minerPosition.distanceTo(targetPosition);

        if (directDistance > static_cast<float>(minerPathValidationMaxPathDistance)) {
            info(String("MinerPathValidationSimulation miner=") + String::valueOf(minerID) +
                " zone=" + zoneName +
                " profile=" + selectedProfile.key +
                " resource=" + selectedResource.name +
                " type=" + selectedResource.type +
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
            minerID,
            zoneName,
            selectedProfile.key,
            selectedResource.name,
            selectedResource.type,
            minerPosition,
            targetPosition,
            densityTarget.density,
            directDistance,
            minerPathValidationMaxPathDistance,
            minerPathValidationMaxPathNodes,
            acceptedDensityTarget,
            zone);
        pathTask->schedule(0);

        ++minerOrdinal;
    }
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
