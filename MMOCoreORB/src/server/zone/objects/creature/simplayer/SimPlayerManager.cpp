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
#include "server/zone/objects/resource/ResourceSpawn.h"

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
    spawnConfiguredGroups();
    scheduleMinerSummaryTask();
    scheduleResourceIntelligenceTask();
}

void SimPlayerManager::loadLuaConfig() {
#ifdef DEBUG_SIMPLAYER
    info("DEBUG: Attempting to run Lua file: scripts/managers/sim_player_manager.lua", true);
#endif

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

    LuaObject resourceIntelligenceConfig = config.getObjectField("resourceIntelligenceConfig");
    if (resourceIntelligenceConfig.isValidTable()) {
        resourceIntelligenceEnabled = resourceIntelligenceConfig.getBooleanField("enabled", resourceIntelligenceEnabled);
        resourceIntelligenceLogTopResources = resourceIntelligenceConfig.getBooleanField("logTopResources", resourceIntelligenceLogTopResources);
        resourceIntelligenceIntervalSeconds = clampMinerInt(resourceIntelligenceConfig.getIntField("summaryIntervalSeconds"), resourceIntelligenceIntervalSeconds, 30, 3600);
        resourceIntelligenceTopN = clampMinerInt(resourceIntelligenceConfig.getIntField("topN"), resourceIntelligenceTopN, 1, 50);
    }
    resourceIntelligenceConfig.pop();

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
    if (resourceName.isEmpty() || amount <= 0)
        return 0;

    uint64 total = 0;

    {
        Locker locker(&conceptualMinerTotalsMutex);

        if (conceptualMinerTotals.contains(resourceName))
            total = conceptualMinerTotals.get(resourceName);

        total += amount;
        conceptualMinerTotals.put(resourceName, total);
    }

    if (logYield) {
        info("SimMiner: recorded " + String::valueOf(amount) + " conceptual " + resourceName +
             " from bot " + String::valueOf(sourceObjectID) + "; total " + resourceName + "=" +
             String::valueOf(total), true);
    }

    return total;
}

void SimPlayerManager::scheduleMinerSummaryTask() {
    if (!minerSummaryLoggingEnabled || minerSummaryTaskScheduled)
        return;

    minerSummaryTaskScheduled = true;

    Reference<SimMinerSummaryTask*> task = new SimMinerSummaryTask();
    task->schedule(minerSummaryIntervalSeconds * 1000);
}

void SimPlayerManager::runMinerSummaryTask() {
    minerSummaryTaskScheduled = false;

    if (!minerSummaryLoggingEnabled)
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
    if (!resourceIntelligenceEnabled || resourceIntelligenceTaskScheduled)
        return;

    resourceIntelligenceTaskScheduled = true;

    Reference<ResourceIntelligenceTask*> task = new ResourceIntelligenceTask();
    task->schedule(resourceIntelligenceIntervalSeconds * 1000);
}

void SimPlayerManager::runResourceIntelligenceTask() {
    resourceIntelligenceTaskScheduled = false;

    if (!resourceIntelligenceEnabled)
        return;

    logResourceIntelligenceSummary();
    scheduleResourceIntelligenceTask();
}

void SimPlayerManager::logResourceIntelligenceSummary() {
    ZoneServer* zoneServer = ServerCore::getZoneServer();

    if (zoneServer == nullptr) {
        info("ResourceIntelligence: ZoneServer unavailable; skipping read-only snapshot", true);
        return;
    }

    ManagedReference<ResourceManager*> resourceManager = zoneServer->getResourceManager();

    if (resourceManager == nullptr) {
        info("ResourceIntelligence: ResourceManager unavailable; skipping read-only snapshot", true);
        return;
    }

    Vector<ResourceIntelligenceEntry> entries;

    {
        Locker managerLocker(resourceManager);

        ResourceSpawner* spawner = resourceManager->getResourceSpawner();

        if (spawner == nullptr || spawner->getResourceMap() == nullptr) {
            info("ResourceIntelligence: ResourceSpawner/resource map unavailable; skipping read-only snapshot", true);
            return;
        }

        ResourceMap* resourceMap = spawner->getResourceMap();
        int resourceCount = resourceMap->size();

        for (int i = 0; i < resourceCount; ++i) {
            ManagedReference<ResourceSpawn*> spawn = resourceMap->get(i);

            if (spawn == nullptr || !spawn->inShift())
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
    }

    for (int i = 0; i < entries.size(); ++i) {
        ResourceIntelligenceEntry entry = entries.get(i);

        entry.genericScore = calculateGenericScore(entry);
        entry.weaponsmithScore = calculateWeaponsmithScore(entry);
        entry.armorsmithScore = calculateArmorsmithScore(entry);
        entry.chefScore = calculateChefScore(entry);
        entry.architectScore = calculateArchitectScore(entry);

        entries.set(i, entry);
    }

    info("ResourceIntelligence: read-only snapshot activeResources=" + String::valueOf(entries.size()) +
         " heuristicScores=true topLogging=" + String(resourceIntelligenceLogTopResources ? "true" : "false"), true);

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

                int score = getResourceIntelligenceScore(entries.get(i), scoreFamily);

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

void SimPlayerManager::startControllerForAgent(AiAgent* agent, Reference<SimPlayerController*> ctrl) {
    if (agent == nullptr || ctrl == nullptr)
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
