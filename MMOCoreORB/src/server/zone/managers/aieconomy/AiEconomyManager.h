/*
 * Owner for persisted AI economy state.
 */

#ifndef AIECONOMYMANAGER_H_
#define AIECONOMYMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/thread/Mutex.h"
#include "system/thread/atomic/AtomicBoolean.h"
#include "system/util/VectorMap.h"
#include "system/util/Vector.h"
#include "server/zone/managers/aieconomy/AiEconomyData.h"

struct AiEconomyStockpileInspectionLot {
	uint64 entryID = 0;
	uint64 resourceSpawnObjectID = 0;
	String conceptualLabel;
	String resourceSpawnName;
	String resourceType;
	String resourceClassChain;
	String sourcePlanet;
	String sourceZone;
	String acquisitionSource;
	String resourceLifecycleState;
	String ownerScope;
	String identityConfidence;
	String matchedDemandProfiles;
	String qualityTier;
	uint64 quantity = 0;
	uint64 reservedQuantity = 0;
	uint64 availableQuantity = 0;
	uint64 acquiredTimestampMs = 0;
	uint64 lastUpdatedTimestampMs = 0;
	bool activeAtAcquisition = false;
	bool conceptualMinerLot = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct AiEconomyStockpileInspectionSnapshot {
	bool persistenceReady = false;
	String status;
	int loadedLots = 0;
	int conceptualMinerLots = 0;
	uint64 totalQuantity = 0;
	uint64 conceptualMinerQuantity = 0;
	uint64 startupBaselineQuantity = 0;
	uint64 reservedQuantity = 0;
	uint64 availableQuantity = 0;
	uint64 dataCreatedTimestampMs = 0;
	uint64 dataUpdatedTimestampMs = 0;
	VectorMap<String, uint64> conceptualMinerQuantities;
	VectorMap<String, uint64> startupBaselineQuantities;
	Vector<AiEconomyStockpileInspectionLot> lots;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

class AiEconomyManager : public Singleton<AiEconomyManager>, public Object, public Logger {
private:
	ManagedReference<AiEconomyData*> economyData;
	AtomicBoolean persistenceReady{false};
	Mutex persistenceMutationMutex;
	VectorMap<String, uint64> conceptualMinerStartupTotals;

	bool loadOrCreateEconomyData(bool& created, String& failureReason, int& stockpileLotCount);
	bool validateEconomyData(AiEconomyData* data, String& failureReason, int& stockpileLotCount);
	void logLoadedConceptualStockpileSummary();

public:
	AiEconomyManager();

	void initialize();
	bool isPersistenceReady() const;
	bool updateConceptualMinerTotals(
		const VectorMap<String, uint64>& totalsSnapshot,
		int& createdLots, int& updatedLots, uint64& totalQuantity,
		String& failureReason);
	bool snapshotPersistentConceptualMinerSupplyForDemand(
		VectorMap<String, uint64>& totalsSnapshot,
		int& conceptualMinerLots, uint64& totalQuantity,
		String& status);
	bool snapshotStockpileInspection(
		AiEconomyStockpileInspectionSnapshot& snapshot,
		int maxLotRows,
		String& status);
};

#endif /* AIECONOMYMANAGER_H_ */
