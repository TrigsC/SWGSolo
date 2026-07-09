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
	bool finishedGoodLot = false;
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

struct AiEconomySpawnLotDeposit {
	uint64 resourceSpawnObjectID = 0;
	// P.5.2: new yield to ADD this flush (delta since last flush), not an
	// absolute total, so consumer draws are not overwritten.
	uint64 quantityDelta = 0;
	String resourceSpawnName;
	String resourceType;
	String resourceClassChain;
	String sourcePlanet;
	String sourceZone;
	String acquisitionSource = "conceptual_miner";
	String resourceLifecycleState = "active";
	String identityConfidence = "exact_type";
	String matchedDemandProfiles;
	String qualityTier;
	bool activeAtAcquisition = true;
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

// P.5.4a: result of a family/class-chain matched reservation. Carries the
// matched lot's identity + 10 stats so the crafter can compute output quality.
struct AiEconomyMatchedReservation {
	uint64 token = 0;
	uint64 entryID = 0;
	String resourceType;
	String resourceSpawnName;
	String matchedQuery;
	int matchedQueryIndex = -1;
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

// P.5.2: a live reservation held against a specific hive lot. Runtime-only; the
// lot's reservedQuantity is reconciled to 0 on load so a crash cannot leak it.
struct HiveReservation {
	uint64 token = 0;
	uint64 entryID = 0;
	uint64 quantity = 0;
	String resourceType;
	ManagedReference<AiEconomyStockpileLot*> lot;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

class AiEconomyManager : public Singleton<AiEconomyManager>, public Object, public Logger {
private:
	ManagedReference<AiEconomyData*> economyData;
	AtomicBoolean persistenceReady{false};
	Mutex persistenceMutationMutex;
	VectorMap<String, uint64> conceptualMinerStartupTotals;
	VectorMap<uint64, HiveReservation> activeReservations;
	uint64 nextReservationToken = 1;
	uint64 reservationsGranted = 0;
	uint64 reservationsConsumed = 0;
	uint64 reservationsReleased = 0;

	bool loadOrCreateEconomyData(bool& created, String& failureReason, int& stockpileLotCount);
	bool validateEconomyData(AiEconomyData* data, String& failureReason, int& stockpileLotCount);
	void logLoadedConceptualStockpileSummary();
	void reconcileReservationsOnLoad();

public:
	AiEconomyManager();

	void initialize();
	bool isPersistenceReady() const;
	bool updateConceptualMinerTotals(
		const VectorMap<String, uint64>& totalsSnapshot,
		int& createdLots, int& updatedLots, uint64& totalQuantity,
		String& failureReason);
	bool updateStockpileSpawnLots(
		const Vector<AiEconomySpawnLotDeposit>& deposits,
		int& createdLots, int& updatedLots, uint64& totalQuantity,
		String& failureReason);
	// P.5.2 reservation API for future crafter NPCs. Simulation-only: consume
	// decrements the hive ledger, it does not create/destroy ResourceContainers.
	bool reserveFromStockpile(
		const String& resourceType, int minOq, uint64 quantity,
		uint64& outToken, uint64& outEntryID, String& failureReason);
	// P.5.4a: type-correct reservation. Tries each query in order (exact types
	// first, then families); a lot matches a query when its resourceType equals
	// or begins with it, or its classChain contains it (mirrors the demand
	// engine's resourceTypeMatches semantics).
	bool reserveFromStockpileMatching(
		const Vector<String>& orderedQueries, int minOq, uint64 quantity,
		AiEconomyMatchedReservation& outReservation, String& failureReason);
	// P.5.4b: upsert crafted output into the finished_good lot tier (one lot
	// per goodKey). Simulation-only: writes stay in the private hive ledger.
	bool depositFinishedGood(
		const String& goodKey, const String& goodName,
		const String& goodClassChain, const String& producingProfile,
		const String& qualityTier, int qualityScore, uint64 outputUnits,
		uint64& outEntryID, uint64& outNewQuantity, String& failureReason);
	bool consumeReservation(uint64 token, String& failureReason);
	bool releaseReservation(uint64 token, String& failureReason);
	void getReservationStats(
		int& activeReservations, uint64& reservedQuantity,
		uint64& granted, uint64& consumed, uint64& released);
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
