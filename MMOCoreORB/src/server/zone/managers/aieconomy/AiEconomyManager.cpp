#include "AiEconomyManager.h"

#include "server/zone/managers/aieconomy/AiEconomyStockpileLot.h"
#include "server/zone/managers/object/ObjectManager.h"

namespace {
	const char* const AI_ECONOMY_DATABASE = "aieconomy";
	const char* const AI_ECONOMY_LOTS_DATABASE = "aieconomylots";
	const int MAX_STOCKPILE_LOTS = 100000;
	const uint64 MAX_STOCKPILE_QUANTITY = 1000000000000ULL;
	const uint64 MAX_FUTURE_TIMESTAMP_MS = 24ULL * 60ULL * 60ULL * 1000ULL;
	const int MAX_LABEL_LENGTH = 128;
	const int MAX_METADATA_LENGTH = 2048;

	bool stringWithinLimit(const String& value, int maxLength) {
		return value.length() <= maxLength;
	}

	bool isAllowedValue(const String& value, const char* const* allowedValues, int valueCount) {
		for (int i = 0; i < valueCount; ++i) {
			if (value == allowedValues[i])
				return true;
		}

		return false;
	}

	bool validResourceStat(int value) {
		return value >= -1 && value <= 1000;
	}
}

AiEconomyManager::AiEconomyManager() : Logger("AiEconomyManager") {
}

void AiEconomyManager::initialize() {
	economyData = nullptr;
	persistenceReady.set(false);
	conceptualMinerStartupTotals.removeAll();

	bool created = false;
	String failureReason;
	int stockpileLotCount = 0;

	if (!loadOrCreateEconomyData(created, failureReason, stockpileLotCount)) {
		error(String("AiEconomyPersistence loaded=false created=") +
			(created ? String("true") : String("false")) +
			" persistenceReady=false reason=\"" +
			failureReason +
			"\" mode=load-only totalsImported=false persistentStockpileSupplyChanged=false");
		return;
	}

	persistenceReady.set(true);

	int schemaVersion = 0;

	{
		Locker dataLocker(economyData);
		schemaVersion = economyData->getSchemaVersion();
	}

	info(String("AiEconomyPersistence loaded=true created=") +
		(created ? String("true") : String("false")) +
		" version=" + String::valueOf(schemaVersion) +
		" stockpileLots=" + String::valueOf(stockpileLotCount) +
		" persistenceReady=" +
			(persistenceReady.get() ? String("true") : String("false")) +
		" mode=load-only totalsImported=false" +
		" persistentStockpileSupplyChanged=false", true);

	logLoadedConceptualStockpileSummary();
}

bool AiEconomyManager::isPersistenceReady() const {
	return persistenceReady.get() && economyData != nullptr;
}

void AiEconomyManager::logLoadedConceptualStockpileSummary() {
	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr)
		return;

	Vector<ManagedReference<AiEconomyStockpileLot*> > lots;

	{
		Locker dataLocker(data);
		Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
			data->getStockpileLots();

		if (storedLots == nullptr)
			return;

		for (int i = 0; i < storedLots->size(); ++i)
			lots.add(storedLots->get(i));
	}

	int conceptualMinerLots = 0;
	uint64 totalQuantity = 0;
	conceptualMinerStartupTotals.removeAll();

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr)
			continue;

		bool conceptualMinerLot = false;
		String conceptualLabel;
		uint64 quantity = 0;

		{
			Locker lotLocker(lot);
			conceptualLabel = lot->getConceptualLabel();
			conceptualMinerLot =
				lot->getAcquisitionSource() == "conceptual_miner" &&
				lot->getResourceLifecycleState() == "conceptual" &&
				lot->getOwnerScope() == "galaxy" &&
				lot->getIdentityConfidence() == "conceptual_label";
			quantity = lot->getQuantity();
		}

		if (!conceptualMinerLot)
			continue;

		conceptualMinerLots++;
		totalQuantity += quantity;
		conceptualMinerStartupTotals.put(conceptualLabel, quantity);
	}

	if (conceptualMinerLots == 0)
		return;

	info(String("AiEconomyPersistenceStockpile loadedLots=") +
		String::valueOf(lots.size()) +
		" conceptualMinerLots=" + String::valueOf(conceptualMinerLots) +
		" totalQuantity=" + String::valueOf(totalQuantity) +
		" mode=read-only persistentStockpileSupplyChanged=false", true);
}

bool AiEconomyManager::updateConceptualMinerTotals(
		const VectorMap<String, uint64>& totalsSnapshot,
		int& createdLots, int& updatedLots, uint64& totalQuantity,
		String& failureReason) {
	createdLots = 0;
	updatedLots = 0;
	totalQuantity = 0;
	failureReason = "";

	Locker mutationLocker(&persistenceMutationMutex);

	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr) {
		failureReason = "persistenceUnavailable";
		return false;
	}

	VectorMap<String, uint64> aggregateSnapshot;

	for (int i = 0; i < totalsSnapshot.size(); ++i) {
		String label = totalsSnapshot.elementAt(i).getKey();
		uint64 sessionQuantity = totalsSnapshot.get(i);

		if (label.isEmpty() || label.length() > MAX_LABEL_LENGTH ||
				sessionQuantity == 0 ||
				sessionQuantity > MAX_STOCKPILE_QUANTITY) {
			failureReason = "invalidSnapshotEntry label=" + label;
			return false;
		}

		uint64 startupQuantity =
			conceptualMinerStartupTotals.contains(label) ?
			conceptualMinerStartupTotals.get(label) : 0;

		if (startupQuantity >
				MAX_STOCKPILE_QUANTITY - sessionQuantity) {
			failureReason = "aggregateQuantityExceeded label=" + label;
			return false;
		}

		uint64 aggregateQuantity = startupQuantity + sessionQuantity;
		aggregateSnapshot.put(label, aggregateQuantity);

		if (totalQuantity > static_cast<uint64>(-1) - aggregateQuantity) {
			failureReason = "aggregateTotalOverflow";
			return false;
		}

		totalQuantity += aggregateQuantity;
	}

	Vector<ManagedReference<AiEconomyStockpileLot*> > lots;
	uint64 nextEntryID = 0;

	{
		Locker dataLocker(data);
		nextEntryID = data->getNextStockpileEntryId();

		Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
			data->getStockpileLots();

		if (storedLots == nullptr) {
			persistenceReady.set(false);
			failureReason = "nullStockpileLotVector";
			return false;
		}

		for (int i = 0; i < storedLots->size(); ++i)
			lots.add(storedLots->get(i));
	}

	VectorMap<String, ManagedReference<AiEconomyStockpileLot*> >
		conceptualMinerLots;

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr) {
			persistenceReady.set(false);
			failureReason = "nullStockpileLot";
			return false;
		}

		String label;
		bool conceptualMinerLot = false;

		{
			Locker lotLocker(lot);
			label = lot->getConceptualLabel();
			conceptualMinerLot =
				lot->getAcquisitionSource() == "conceptual_miner" &&
				lot->getResourceLifecycleState() == "conceptual" &&
				lot->getOwnerScope() == "galaxy" &&
				lot->getIdentityConfidence() == "conceptual_label";
		}

		if (!conceptualMinerLot)
			continue;

		if (label.isEmpty() || conceptualMinerLots.contains(label)) {
			persistenceReady.set(false);
			failureReason = "duplicateOrInvalidConceptualMinerLot label=" +
				label;
			return false;
		}

		conceptualMinerLots.put(label, lot);
	}

	int missingLotCount = 0;

	for (int i = 0; i < aggregateSnapshot.size(); ++i) {
		String label = aggregateSnapshot.elementAt(i).getKey();

		if (!conceptualMinerLots.contains(label))
			missingLotCount++;
	}

	if (lots.size() + missingLotCount > MAX_STOCKPILE_LOTS) {
		persistenceReady.set(false);
		failureReason = "stockpileLotLimitExceeded";
		return false;
	}

	Vector<ManagedReference<AiEconomyStockpileLot*> > newLots;

	try {
		for (int i = 0; i < aggregateSnapshot.size(); ++i) {
			String label = aggregateSnapshot.elementAt(i).getKey();
			uint64 quantity = aggregateSnapshot.get(i);

			if (conceptualMinerLots.contains(label)) {
				ManagedReference<AiEconomyStockpileLot*> lot =
					conceptualMinerLots.get(label);

				{
					Locker lotLocker(lot);
					lot->updateConceptualMinerAggregate(quantity);
				}

				updatedLots++;
				continue;
			}

			if (nextEntryID == 0 || nextEntryID == static_cast<uint64>(-1)) {
				persistenceReady.set(false);
				failureReason = "stockpileEntryIdExhausted";
				return false;
			}

			ManagedReference<AiEconomyStockpileLot*> newLot =
				new AiEconomyStockpileLot(nextEntryID, label, quantity);

			ObjectManager::instance()->persistObject(
				newLot, 1, AI_ECONOMY_LOTS_DATABASE);

			newLots.add(newLot);
			nextEntryID++;
			createdLots++;
		}

		{
			Locker dataLocker(data);

			for (int i = 0; i < newLots.size(); ++i)
				data->addStockpileLot(newLots.get(i));

			if (newLots.size() > 0)
				data->setNextStockpileEntryId(nextEntryID);
			else
				data->updateTimestamp();
		}
	} catch (Exception& e) {
		persistenceReady.set(false);
		failureReason = String("persistenceException: ") + e.getMessage();
		return false;
	}

	int validatedLotCount = 0;

	if (!validateEconomyData(data, failureReason, validatedLotCount)) {
		persistenceReady.set(false);
		return false;
	}

	return true;
}

bool AiEconomyManager::snapshotPersistentConceptualMinerSupplyForDemand(
		VectorMap<String, uint64>& totalsSnapshot,
		int& conceptualMinerLots, uint64& totalQuantity,
		String& status) {
	totalsSnapshot.removeAll();
	conceptualMinerLots = 0;
	totalQuantity = 0;
	status = "unavailable";

	Locker mutationLocker(&persistenceMutationMutex);

	if (!persistenceReady.get() || economyData == nullptr)
		return false;

	for (int i = 0; i < conceptualMinerStartupTotals.size(); ++i) {
		String label = conceptualMinerStartupTotals.elementAt(i).getKey();
		uint64 quantity = conceptualMinerStartupTotals.get(i);

		if (label.isEmpty() || label.length() > MAX_LABEL_LENGTH ||
				quantity > MAX_STOCKPILE_QUANTITY) {
			totalsSnapshot.removeAll();
			conceptualMinerLots = 0;
			totalQuantity = 0;
			status = "invalid";
			return false;
		}

		if (quantity == 0)
			continue;

		if (totalQuantity > static_cast<uint64>(-1) - quantity) {
			totalsSnapshot.removeAll();
			conceptualMinerLots = 0;
			totalQuantity = 0;
			status = "invalid";
			return false;
		}

		totalsSnapshot.put(label, quantity);
		conceptualMinerLots++;
		totalQuantity += quantity;
	}

	status = "ready";
	return true;
}

bool AiEconomyManager::loadOrCreateEconomyData(
		bool& created, String& failureReason, int& stockpileLotCount) {
	created = false;
	stockpileLotCount = 0;

	ObjectDatabase* economyDatabase =
		ObjectDatabaseManager::instance()->loadObjectDatabase(
			AI_ECONOMY_DATABASE, true);

	if (economyDatabase == nullptr) {
		failureReason = "databaseUnavailable";
		return false;
	}

	ManagedReference<AiEconomyData*> loadedData;
	int databaseObjectCount = 0;

	try {
		ObjectDatabaseIterator iterator(economyDatabase);
		uint64 objectID = 0;

		while (iterator.getNextKey(objectID)) {
			databaseObjectCount++;

			Reference<AiEconomyData*> candidate =
				Core::getObjectBroker()->lookUp(objectID).castTo<AiEconomyData*>();

			if (candidate == nullptr) {
				failureReason =
					"incompatibleObject oid=" + String::valueOf(objectID);
				return false;
			}

			if (loadedData != nullptr) {
				failureReason = "multipleEconomyDataObjects count=" +
					String::valueOf(databaseObjectCount);
				return false;
			}

			loadedData = candidate;
		}
	} catch (DatabaseException& e) {
		failureReason =
			String("databaseException: ") + e.getMessage();
		return false;
	}

	if (loadedData == nullptr) {
		ManagedReference<AiEconomyData*> newData = new AiEconomyData();

		try {
			ObjectManager::instance()->persistObject(
				newData, 1, AI_ECONOMY_DATABASE);
		} catch (Exception& e) {
			failureReason =
				String("createFailed: ") + e.getMessage();
			return false;
		}

		loadedData = newData;
		created = true;
	}

	if (!validateEconomyData(
			loadedData, failureReason, stockpileLotCount)) {
		return false;
	}

	economyData = loadedData;
	return true;
}

bool AiEconomyManager::validateEconomyData(
		AiEconomyData* data, String& failureReason, int& stockpileLotCount) {
	if (data == nullptr) {
		failureReason = "nullEconomyData";
		return false;
	}

	int schemaVersion = 0;
	uint64 nextEntryID = 0;
	uint64 createdTimestamp = 0;
	uint64 updatedTimestamp = 0;
	Vector<ManagedReference<AiEconomyStockpileLot*> > lots;

	{
		Locker dataLocker(data);
		schemaVersion = data->getSchemaVersion();
		nextEntryID = data->getNextStockpileEntryId();
		createdTimestamp = data->getCreatedTimestamp();
		updatedTimestamp = data->getUpdatedTimestamp();

		Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
			data->getStockpileLots();

		if (storedLots == nullptr) {
			failureReason = "nullStockpileLotVector";
			return false;
		}

		if (storedLots->size() > MAX_STOCKPILE_LOTS) {
			failureReason = "stockpileLotLimitExceeded count=" +
				String::valueOf(storedLots->size());
			return false;
		}

		for (int i = 0; i < storedLots->size(); ++i)
			lots.add(storedLots->get(i));
	}

	stockpileLotCount = lots.size();

	if (schemaVersion != AiEconomyData::CURRENT_SCHEMA_VERSION) {
		failureReason = "unsupportedSchemaVersion version=" +
			String::valueOf(schemaVersion);
		return false;
	}

	if (nextEntryID == 0) {
		failureReason = "invalidNextStockpileEntryId";
		return false;
	}

	Time now;
	uint64 currentTime = now.getMiliTime();

	if (createdTimestamp == 0 || updatedTimestamp < createdTimestamp ||
			createdTimestamp > currentTime + MAX_FUTURE_TIMESTAMP_MS ||
			updatedTimestamp > currentTime + MAX_FUTURE_TIMESTAMP_MS) {
		failureReason = "invalidEconomyTimestamps";
		return false;
	}

	Vector<uint64> entryIDs;
	Vector<String> conceptualMinerLabels;
	uint64 highestEntryID = 0;
	const char* const acquisitionSources[] = {
		"conceptual_miner",
		"future_ai_harvester",
		"market_purchase",
		"admin_seed",
		"future_ai_crafter",
		"unknown"
	};
	const char* const lifecycleStates[] = {
		"active",
		"inactive",
		"despawned",
		"conceptual",
		"unknown"
	};
	const char* const confidenceValues[] = {
		"exact_type",
		"coarse_family",
		"conceptual_label",
		"unknown"
	};

	for (int lotIndex = 0; lotIndex < lots.size(); ++lotIndex) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(lotIndex);

		if (lot == nullptr) {
			failureReason = "nullStockpileLot index=" +
				String::valueOf(lotIndex);
			return false;
		}

		uint64 entryID = 0;
		uint64 quantity = 0;
		uint64 reservedQuantity = 0;
		uint64 acquiredTimestamp = 0;
		uint64 lastUpdatedTimestamp = 0;
		String conceptualLabel;
		String resourceSpawnName;
		String resourceType;
		String resourceClassChain;
		String sourcePlanet;
		String sourceZone;
		String acquisitionSource;
		String lifecycleState;
		String ownerScope;
		String identityConfidence;
		String matchedDemandProfiles;
		String qualityTier;
		int stats[10];

		{
			Locker lotLocker(lot);
			entryID = lot->getEntryId();
			quantity = lot->getQuantity();
			reservedQuantity = lot->getReservedQuantity();
			acquiredTimestamp = lot->getAcquiredTimestamp();
			lastUpdatedTimestamp = lot->getLastUpdatedTimestamp();
			conceptualLabel = lot->getConceptualLabel();
			resourceSpawnName = lot->getResourceSpawnName();
			resourceType = lot->getResourceType();
			resourceClassChain = lot->getResourceClassChain();
			sourcePlanet = lot->getSourcePlanet();
			sourceZone = lot->getSourceZone();
			acquisitionSource = lot->getAcquisitionSource();
			lifecycleState = lot->getResourceLifecycleState();
			ownerScope = lot->getOwnerScope();
			identityConfidence = lot->getIdentityConfidence();
			matchedDemandProfiles = lot->getMatchedDemandProfiles();
			qualityTier = lot->getQualityTier();
			stats[0] = lot->getOq();
			stats[1] = lot->getCd();
			stats[2] = lot->getDr();
			stats[3] = lot->getHr();
			stats[4] = lot->getFl();
			stats[5] = lot->getMa();
			stats[6] = lot->getPe();
			stats[7] = lot->getSr();
			stats[8] = lot->getUt();
			stats[9] = lot->getCr();
		}

		if (entryID == 0 || entryIDs.contains(entryID)) {
			failureReason = "invalidOrDuplicateEntryId index=" +
				String::valueOf(lotIndex);
			return false;
		}

		entryIDs.add(entryID);

		if (entryID > highestEntryID)
			highestEntryID = entryID;

		if (quantity > MAX_STOCKPILE_QUANTITY ||
				reservedQuantity > quantity) {
			failureReason = "invalidLotQuantity entryId=" +
				String::valueOf(entryID);
			return false;
		}

		if (conceptualLabel.isEmpty() && resourceSpawnName.isEmpty() &&
				resourceType.isEmpty()) {
			failureReason = "missingLotIdentity entryId=" +
				String::valueOf(entryID);
			return false;
		}

		if (!stringWithinLimit(conceptualLabel, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(resourceSpawnName, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(resourceType, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(sourcePlanet, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(sourceZone, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(ownerScope, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(qualityTier, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(resourceClassChain, MAX_METADATA_LENGTH) ||
				!stringWithinLimit(matchedDemandProfiles, MAX_METADATA_LENGTH)) {
			failureReason = "lotMetadataLimitExceeded entryId=" +
				String::valueOf(entryID);
			return false;
		}

		if (ownerScope.isEmpty() ||
				!isAllowedValue(
					acquisitionSource, acquisitionSources, 6) ||
				!isAllowedValue(lifecycleState, lifecycleStates, 5) ||
				!isAllowedValue(
					identityConfidence, confidenceValues, 4)) {
			failureReason = "invalidLotClassification entryId=" +
				String::valueOf(entryID);
			return false;
		}

		if (acquisitionSource == "conceptual_miner" &&
				lifecycleState == "conceptual" &&
				ownerScope == "galaxy" &&
				identityConfidence == "conceptual_label") {
			if (conceptualLabel.isEmpty() ||
					conceptualMinerLabels.contains(conceptualLabel)) {
				failureReason =
					"duplicateOrInvalidConceptualMinerLot entryId=" +
					String::valueOf(entryID);
				return false;
			}

			conceptualMinerLabels.add(conceptualLabel);
		}

		if (acquiredTimestamp == 0 ||
				lastUpdatedTimestamp < acquiredTimestamp ||
				acquiredTimestamp > currentTime + MAX_FUTURE_TIMESTAMP_MS ||
				lastUpdatedTimestamp >
					currentTime + MAX_FUTURE_TIMESTAMP_MS) {
			failureReason = "invalidLotTimestamps entryId=" +
				String::valueOf(entryID);
			return false;
		}

		for (int statIndex = 0; statIndex < 10; ++statIndex) {
			if (!validResourceStat(stats[statIndex])) {
				failureReason = "invalidResourceStat entryId=" +
					String::valueOf(entryID);
				return false;
			}
		}
	}

	if (highestEntryID >= nextEntryID) {
		failureReason = "nextStockpileEntryIdNotMonotonic";
		return false;
	}

	return true;
}
