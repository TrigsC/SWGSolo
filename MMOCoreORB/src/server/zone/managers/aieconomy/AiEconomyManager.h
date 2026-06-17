/*
 * Owner for persisted AI economy state.
 */

#ifndef AIECONOMYMANAGER_H_
#define AIECONOMYMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/thread/Mutex.h"
#include "system/thread/atomic/AtomicBoolean.h"
#include "system/util/VectorMap.h"
#include "server/zone/managers/aieconomy/AiEconomyData.h"

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
};

#endif /* AIECONOMYMANAGER_H_ */
