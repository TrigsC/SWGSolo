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

	// P.5.4a: mirrors the demand engine's resourceTypeMatches semantics
	// (SimPlayerManager) on plain lot fields: exact type, type prefix, or
	// class-chain containment.
	bool lotFieldsMatchResourceQuery(
			const String& resourceType, const String& classChain,
			const String& query) {
		if (query.isEmpty())
			return false;

		if (resourceType == query || resourceType.beginsWith(query))
			return true;

		if (classChain.isEmpty())
			return false;

		return classChain.indexOf(query) >= 0;
	}

	bool lotFieldsMatchFamily(
			const String& resourceType, const String& classChain,
			const String& family) {
		String normalizedType = resourceType.toLowerCase();
		String normalizedChain = classChain.toLowerCase();
		String normalizedFamily = family.toLowerCase().trim();

		if (normalizedFamily.isEmpty())
			return false;

		if (normalizedType == normalizedFamily ||
				normalizedType.beginsWith(normalizedFamily))
			return true;

		return !normalizedChain.isEmpty() &&
			normalizedChain.indexOf(normalizedFamily) >= 0;
	}

	String spawnLotKey(uint64 resourceSpawnObjectID,
			const String& acquisitionSource) {
		return String::valueOf(resourceSpawnObjectID) + "|" +
			acquisitionSource;
	}
}

AiEconomyManager::AiEconomyManager() : Logger("AiEconomyManager") {
}

void AiEconomyManager::initialize() {
	economyData = nullptr;
	persistenceReady.set(false);
	conceptualMinerStartupTotals.removeAll();
	activeReservations.removeAll();
	nextReservationToken = 1;
	reservationsGranted = 0;
	reservationsConsumed = 0;
	reservationsReleased = 0;

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
	// P.5.2: reservations do not survive a restart, so clear any reservedQuantity
	// left on lots by a crash mid-reservation.
	reconcileReservationsOnLoad();
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

bool AiEconomyManager::updateStockpileSpawnLots(
		const Vector<AiEconomySpawnLotDeposit>& deposits,
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

	// (spawnObjectID, acquisitionSource) -> quantity delta to ADD this flush
	// (P.5.2 increment model).
	VectorMap<String, uint64> deltaSnapshot;

	for (int i = 0; i < deposits.size(); ++i) {
		const AiEconomySpawnLotDeposit& deposit = deposits.get(i);

		if (deposit.resourceSpawnObjectID == 0) {
			failureReason = "invalidSpawnDepositId";
			return false;
		}

		if (deposit.quantityDelta == 0)
			continue;

		if (deposit.quantityDelta > MAX_STOCKPILE_QUANTITY) {
			failureReason = "invalidSpawnDepositQuantity spawn=" +
				String::valueOf(deposit.resourceSpawnObjectID);
			return false;
		}

		if (deposit.resourceType.isEmpty() &&
				deposit.resourceSpawnName.isEmpty()) {
			failureReason = "missingSpawnDepositIdentity spawn=" +
				String::valueOf(deposit.resourceSpawnObjectID);
			return false;
		}

		if (!stringWithinLimit(deposit.resourceSpawnName, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(deposit.resourceType, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(deposit.sourcePlanet, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(deposit.sourceZone, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(deposit.qualityTier, MAX_LABEL_LENGTH) ||
				!stringWithinLimit(
					deposit.resourceClassChain, MAX_METADATA_LENGTH) ||
				!stringWithinLimit(
					deposit.matchedDemandProfiles, MAX_METADATA_LENGTH)) {
			failureReason = "spawnDepositMetadataLimitExceeded spawn=" +
				String::valueOf(deposit.resourceSpawnObjectID);
			return false;
		}

		String depositKey = spawnLotKey(
			deposit.resourceSpawnObjectID, deposit.acquisitionSource);

		if (deltaSnapshot.contains(depositKey)) {
			failureReason = "duplicateSpawnDeposit spawn=" +
				String::valueOf(deposit.resourceSpawnObjectID) +
				" source=" + deposit.acquisitionSource;
			return false;
		}

		deltaSnapshot.put(
			depositKey, deposit.quantityDelta);
	}

	if (deltaSnapshot.size() == 0)
		return true;

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

	VectorMap<String, ManagedReference<AiEconomyStockpileLot*> > spawnLots;

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr) {
			persistenceReady.set(false);
			failureReason = "nullStockpileLot";
			return false;
		}

		uint64 spawnObjectID = 0;
		String acquisitionSource;
		String identityConfidence;

		{
			Locker lotLocker(lot);
			spawnObjectID = lot->getResourceSpawnObjectId();
			acquisitionSource = lot->getAcquisitionSource();
			identityConfidence = lot->getIdentityConfidence();
		}

		if (spawnObjectID == 0 || identityConfidence != "exact_type")
			continue;

		String lotKey = spawnLotKey(spawnObjectID, acquisitionSource);

		if (spawnLots.contains(lotKey)) {
			persistenceReady.set(false);
			failureReason = "duplicateSpawnLot spawn=" +
				String::valueOf(spawnObjectID) + " source=" + acquisitionSource;
			return false;
		}

		spawnLots.put(lotKey, lot);
	}

	int missingLotCount = 0;

	for (int i = 0; i < deltaSnapshot.size(); ++i) {
		String depositKey = deltaSnapshot.elementAt(i).getKey();

		if (!spawnLots.contains(depositKey))
			missingLotCount++;
	}

	if (lots.size() + missingLotCount > MAX_STOCKPILE_LOTS) {
		persistenceReady.set(false);
		failureReason = "stockpileLotLimitExceeded";
		return false;
	}

	Vector<ManagedReference<AiEconomyStockpileLot*> > newLots;

	try {
		for (int i = 0; i < deposits.size(); ++i) {
			const AiEconomySpawnLotDeposit& deposit = deposits.get(i);

			String depositKey = spawnLotKey(
				deposit.resourceSpawnObjectID, deposit.acquisitionSource);

			if (!deltaSnapshot.contains(depositKey))
				continue;

			uint64 delta = deltaSnapshot.get(depositKey);

			if (spawnLots.contains(depositKey)) {
				ManagedReference<AiEconomyStockpileLot*> lot =
					spawnLots.get(depositKey);

				bool lotChanged = false;

				{
					Locker lotLocker(lot);
					// Clamp so on-hand never exceeds the validation ceiling.
					uint64 current = lot->getQuantity();
					uint64 addable =
						current >= MAX_STOCKPILE_QUANTITY ? 0 :
						(delta < MAX_STOCKPILE_QUANTITY - current ? delta :
							MAX_STOCKPILE_QUANTITY - current);

					if (addable > 0) {
						lot->addSpawnLotQuantity(
							addable,
							deposit.resourceLifecycleState,
							deposit.activeAtAcquisition);
						totalQuantity += addable;
						lotChanged = true;
					}
				}

				// Durability: addSpawnLotQuantity writes the field directly and
				// does not dirty the managed object, so the periodic DB save
				// would skip it and the deposit growth would be lost on restart.
				// Flag it for persistence (matches the persistObject idiom below).
				if (lotChanged)
					ObjectManager::instance()->updatePersistentObject(lot);

				updatedLots++;
				continue;
			}

			if (nextEntryID == 0 || nextEntryID == static_cast<uint64>(-1)) {
				persistenceReady.set(false);
				failureReason = "stockpileEntryIdExhausted";
				return false;
			}

			// Allocate via the existing constructor; initializeSpawnLot below
			// overwrites every field with the exact-identity deposit values.
			ManagedReference<AiEconomyStockpileLot*> newLot =
				new AiEconomyStockpileLot(
					nextEntryID, deposit.resourceType, delta);

			{
				Locker lotLocker(newLot);
				newLot->initializeSpawnLot(
					nextEntryID,
					deposit.resourceSpawnObjectID,
					deposit.resourceSpawnName,
					deposit.resourceType,
					deposit.resourceClassChain,
					deposit.sourcePlanet,
					deposit.sourceZone,
					deposit.acquisitionSource,
					deposit.resourceLifecycleState,
					deposit.identityConfidence,
					deposit.matchedDemandProfiles,
					deposit.qualityTier,
					deposit.activeAtAcquisition,
					delta);
				newLot->setResourceStats(
					deposit.oq, deposit.cd, deposit.dr, deposit.hr,
					deposit.fl, deposit.ma, deposit.pe, deposit.sr,
					deposit.ut, deposit.cr);
			}

			totalQuantity += delta;

			ObjectManager::instance()->persistObject(
				newLot, 1, AI_ECONOMY_LOTS_DATABASE);

			newLots.add(newLot);
			spawnLots.put(depositKey, newLot);
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

void AiEconomyManager::reconcileReservationsOnLoad() {
	Locker mutationLocker(&persistenceMutationMutex);

	activeReservations.removeAll();

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

	int clearedLots = 0;

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr)
			continue;

		Locker lotLocker(lot);

		if (lot->getReservedQuantity() > 0) {
			lot->clearReservedQuantity();
			clearedLots++;
		}
	}

	if (clearedLots > 0)
		info(String("AiEconomyReservationReconcile clearedLots=") +
			String::valueOf(clearedLots) + " mode=load-reset", true);
}

bool AiEconomyManager::reserveFromStockpile(
		const String& resourceType, int minOq, uint64 quantity,
		uint64& outToken, uint64& outEntryID, String& failureReason) {
	outToken = 0;
	outEntryID = 0;
	failureReason = "";

	if (quantity == 0) {
		failureReason = "zeroQuantity";
		return false;
	}

	Locker mutationLocker(&persistenceMutationMutex);

	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr) {
		failureReason = "persistenceUnavailable";
		return false;
	}

	Vector<ManagedReference<AiEconomyStockpileLot*> > lots;

	{
		Locker dataLocker(data);
		Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
			data->getStockpileLots();

		if (storedLots == nullptr) {
			failureReason = "nullStockpileLotVector";
			return false;
		}

		for (int i = 0; i < storedLots->size(); ++i)
			lots.add(storedLots->get(i));
	}

	ManagedReference<AiEconomyStockpileLot*> bestLot;
	uint64 bestEntryID = 0;
	String bestResourceType;
	int bestOq = -1;
	uint64 bestAvailable = 0;

	// Crafting-grade selection: prefer the highest-OQ eligible exact lot, then
	// the deepest stack.
	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr)
			continue;

		Locker lotLocker(lot);

		if (lot->getIdentityConfidence() != "exact_type" ||
				lot->getResourceLifecycleState() == "despawned")
			continue;

		if (!resourceType.isEmpty() && lot->getResourceType() != resourceType)
			continue;

		int oq = lot->getOq();

		if (oq < minOq)
			continue;

		uint64 available = lot->getAvailableQuantity();

		if (available < quantity)
			continue;

		if (oq > bestOq || (oq == bestOq && available > bestAvailable)) {
			bestOq = oq;
			bestAvailable = available;
			bestLot = lot;
			bestEntryID = lot->getEntryId();
			bestResourceType = lot->getResourceType();
		}
	}

	if (bestLot == nullptr) {
		failureReason = "noEligibleLot";
		return false;
	}

	{
		Locker lotLocker(bestLot);

		if (bestLot->getAvailableQuantity() < quantity) {
			failureReason = "insufficientAvailable";
			return false;
		}

		bestLot->addReservedQuantity(quantity);
	}

	uint64 token = nextReservationToken++;

	HiveReservation reservation;
	reservation.token = token;
	reservation.entryID = bestEntryID;
	reservation.quantity = quantity;
	reservation.resourceType = bestResourceType;
	reservation.lot = bestLot;

	activeReservations.put(token, reservation);
	reservationsGranted++;

	outToken = token;
	outEntryID = bestEntryID;
	return true;
}

bool AiEconomyManager::reserveFromStockpileMatching(
		const Vector<String>& orderedQueries, int minOq, uint64 quantity,
		AiEconomyMatchedReservation& outReservation, String& failureReason) {
	outReservation = AiEconomyMatchedReservation();
	failureReason = "";

	if (quantity == 0) {
		failureReason = "zeroQuantity";
		return false;
	}

	if (orderedQueries.size() == 0) {
		failureReason = "noQueries";
		return false;
	}

	Locker mutationLocker(&persistenceMutationMutex);

	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr) {
		failureReason = "persistenceUnavailable";
		return false;
	}

	Vector<ManagedReference<AiEconomyStockpileLot*> > lots;

	{
		Locker dataLocker(data);
		Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
			data->getStockpileLots();

		if (storedLots == nullptr) {
			failureReason = "nullStockpileLotVector";
			return false;
		}

		for (int i = 0; i < storedLots->size(); ++i)
			lots.add(storedLots->get(i));
	}

	// Snapshot eligible raw lots once so each query tier matches in-memory
	// (persistenceMutationMutex serializes all mutators, so this is stable).
	Vector<int> candidateIndexes;
	Vector<String> candidateTypes;
	Vector<String> candidateChains;
	Vector<int> candidateOqs;
	Vector<uint64> candidateAvailable;

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr)
			continue;

		Locker lotLocker(lot);

		if (lot->getIdentityConfidence() != "exact_type" ||
				lot->getResourceLifecycleState() == "despawned")
			continue;

		int oq = lot->getOq();

		if (oq < minOq)
			continue;

		uint64 available = lot->getAvailableQuantity();

		if (available < quantity)
			continue;

		candidateIndexes.add(i);
		candidateTypes.add(lot->getResourceType());
		candidateChains.add(lot->getResourceClassChain());
		candidateOqs.add(oq);
		candidateAvailable.add(available);
	}

	if (candidateIndexes.size() == 0) {
		failureReason = "noEligibleLot";
		return false;
	}

	// First query tier that matches any lot wins; within the tier keep the
	// proven selection rule (highest OQ, tie-break deepest stack).
	int bestCandidate = -1;
	int matchedQueryIndex = -1;

	for (int queryIndex = 0;
			queryIndex < orderedQueries.size() && bestCandidate < 0;
			++queryIndex) {
		const String& query = orderedQueries.get(queryIndex);

		for (int c = 0; c < candidateIndexes.size(); ++c) {
			if (!lotFieldsMatchResourceQuery(
					candidateTypes.get(c), candidateChains.get(c), query))
				continue;

			if (bestCandidate < 0 ||
					candidateOqs.get(c) > candidateOqs.get(bestCandidate) ||
					(candidateOqs.get(c) == candidateOqs.get(bestCandidate) &&
						candidateAvailable.get(c) >
							candidateAvailable.get(bestCandidate))) {
				bestCandidate = c;
				matchedQueryIndex = queryIndex;
			}
		}
	}

	if (bestCandidate < 0) {
		failureReason = "noMatchingLot";
		return false;
	}

	ManagedReference<AiEconomyStockpileLot*> bestLot =
		lots.get(candidateIndexes.get(bestCandidate));

	{
		Locker lotLocker(bestLot);

		if (bestLot->getAvailableQuantity() < quantity) {
			failureReason = "insufficientAvailable";
			return false;
		}

		bestLot->addReservedQuantity(quantity);

		outReservation.entryID = bestLot->getEntryId();
		outReservation.resourceType = bestLot->getResourceType();
		outReservation.resourceSpawnName = bestLot->getResourceSpawnName();
		outReservation.oq = bestLot->getOq();
		outReservation.cd = bestLot->getCd();
		outReservation.dr = bestLot->getDr();
		outReservation.hr = bestLot->getHr();
		outReservation.fl = bestLot->getFl();
		outReservation.ma = bestLot->getMa();
		outReservation.pe = bestLot->getPe();
		outReservation.sr = bestLot->getSr();
		outReservation.ut = bestLot->getUt();
		outReservation.cr = bestLot->getCr();
	}

	uint64 token = nextReservationToken++;

	HiveReservation reservation;
	reservation.token = token;
	reservation.entryID = outReservation.entryID;
	reservation.quantity = quantity;
	reservation.resourceType = outReservation.resourceType;
	reservation.lot = bestLot;

	activeReservations.put(token, reservation);
	reservationsGranted++;

	outReservation.token = token;
	outReservation.matchedQuery = orderedQueries.get(matchedQueryIndex);
	outReservation.matchedQueryIndex = matchedQueryIndex;
	return true;
}

bool AiEconomyManager::depositFinishedGood(
		const String& goodKey, const String& goodName,
		const String& goodClassChain, const String& producingProfile,
		const String& qualityTier, int qualityScore, uint64 outputUnits,
		uint64& outEntryID, uint64& outNewQuantity, String& failureReason) {
	outEntryID = 0;
	outNewQuantity = 0;
	failureReason = "";

	if (goodKey.isEmpty() || outputUnits == 0 ||
			outputUnits > MAX_STOCKPILE_QUANTITY) {
		failureReason = "invalidFinishedGoodDeposit";
		return false;
	}

	if (!stringWithinLimit(goodKey, MAX_LABEL_LENGTH) ||
			!stringWithinLimit(goodName, MAX_LABEL_LENGTH) ||
			!stringWithinLimit(qualityTier, MAX_LABEL_LENGTH) ||
			!stringWithinLimit(goodClassChain, MAX_METADATA_LENGTH) ||
			!stringWithinLimit(producingProfile, MAX_METADATA_LENGTH)) {
		failureReason = "finishedGoodMetadataLimitExceeded";
		return false;
	}

	if (qualityScore < -1)
		qualityScore = -1;
	else if (qualityScore > 1000)
		qualityScore = 1000;

	Locker mutationLocker(&persistenceMutationMutex);

	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr) {
		failureReason = "persistenceUnavailable";
		return false;
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

	// Upsert: one finished_good lot per goodKey.
	ManagedReference<AiEconomyStockpileLot*> goodLot;

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr)
			continue;

		Locker lotLocker(lot);

		if (lot->getIdentityConfidence() != "finished_good" ||
				lot->getResourceType() != goodKey)
			continue;

		if (goodLot != nullptr) {
			persistenceReady.set(false);
			failureReason = "duplicateFinishedGoodLot goodKey=" + goodKey;
			return false;
		}

		goodLot = lot;
	}

	try {
		if (goodLot != nullptr) {
			{
				Locker lotLocker(goodLot);

				uint64 current = goodLot->getQuantity();
				uint64 addable =
					current >= MAX_STOCKPILE_QUANTITY ? 0 :
					(outputUnits < MAX_STOCKPILE_QUANTITY - current ?
						outputUnits : MAX_STOCKPILE_QUANTITY - current);

				if (addable == 0) {
					failureReason = "finishedGoodQuantityCeiling";
					return false;
				}

				// Quantity-weighted running-average quality, kept in oq.
				int currentQuality = goodLot->getOq();
				int newQuality = qualityScore;

				if (currentQuality >= 0 && qualityScore >= 0) {
					uint64 blended =
						(static_cast<uint64>(currentQuality) * current +
							static_cast<uint64>(qualityScore) * addable) /
						(current + addable);
					newQuality = static_cast<int>(blended);
				} else if (currentQuality >= 0) {
					newQuality = currentQuality;
				}

				goodLot->addFinishedGoodQuantity(
					addable, newQuality, qualityTier);
				outEntryID = goodLot->getEntryId();
				outNewQuantity = goodLot->getQuantity();
			}

			// Durability: addFinishedGoodQuantity writes fields directly and
			// does not dirty the managed object, so flag it for the periodic
			// DB save (same idiom as the deposit/consume paths).
			ObjectManager::instance()->updatePersistentObject(goodLot);

			{
				Locker dataLocker(data);
				data->updateTimestamp();
			}
		} else {
			if (nextEntryID == 0 || nextEntryID == static_cast<uint64>(-1)) {
				persistenceReady.set(false);
				failureReason = "stockpileEntryIdExhausted";
				return false;
			}

			if (lots.size() + 1 > MAX_STOCKPILE_LOTS) {
				persistenceReady.set(false);
				failureReason = "stockpileLotLimitExceeded";
				return false;
			}

			ManagedReference<AiEconomyStockpileLot*> newLot =
				new AiEconomyStockpileLot(nextEntryID, goodKey, outputUnits);

			{
				Locker lotLocker(newLot);
				newLot->initializeSpawnLot(
					nextEntryID,
					0,
					goodName,
					goodKey,
					goodClassChain,
					"",
					"",
					"hive_crafter",
					"crafted",
					"finished_good",
					producingProfile,
					qualityTier,
					true,
					outputUnits);
				newLot->setResourceStats(
					qualityScore, -1, -1, -1, -1, -1, -1, -1, -1, -1);
			}

			ObjectManager::instance()->persistObject(
				newLot, 1, AI_ECONOMY_LOTS_DATABASE);

			{
				Locker dataLocker(data);
				data->addStockpileLot(newLot);
				data->setNextStockpileEntryId(nextEntryID + 1);
			}

			outEntryID = nextEntryID;
			outNewQuantity = outputUnits;
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

bool AiEconomyManager::consumeReservation(uint64 token, String& failureReason) {
	failureReason = "";

	Locker mutationLocker(&persistenceMutationMutex);

	if (!persistenceReady.get()) {
		failureReason = "persistenceUnavailable";
		return false;
	}

	if (!activeReservations.contains(token)) {
		failureReason = "unknownReservation";
		return false;
	}

	HiveReservation reservation = activeReservations.get(token);
	ManagedReference<AiEconomyStockpileLot*> lot = reservation.lot;

	if (lot == nullptr) {
		activeReservations.drop(token);
		failureReason = "lotMissing";
		return false;
	}

	{
		Locker lotLocker(lot);

		if (lot->getReservedQuantity() < reservation.quantity ||
				lot->getQuantity() < reservation.quantity) {
			failureReason = "ledgerInconsistent";
			return false;
		}

		lot->consumeReservedQuantity(reservation.quantity);
	}

	// Durability: consumeReservedQuantity writes quantity/reservedQuantity
	// directly without dirtying the managed object, so the draw-down must be
	// flagged for the periodic DB save or it would be lost on restart.
	ObjectManager::instance()->updatePersistentObject(lot);

	activeReservations.drop(token);
	reservationsConsumed++;
	return true;
}

bool AiEconomyManager::releaseReservation(uint64 token, String& failureReason) {
	failureReason = "";

	Locker mutationLocker(&persistenceMutationMutex);

	if (!activeReservations.contains(token)) {
		failureReason = "unknownReservation";
		return false;
	}

	HiveReservation reservation = activeReservations.get(token);
	ManagedReference<AiEconomyStockpileLot*> lot = reservation.lot;

	if (lot != nullptr) {
		Locker lotLocker(lot);

		uint64 reserved = lot->getReservedQuantity();
		uint64 release = reservation.quantity < reserved ?
			reservation.quantity : reserved;

		if (release > 0)
			lot->releaseReservedQuantity(release);
	}

	activeReservations.drop(token);
	reservationsReleased++;
	return true;
}

void AiEconomyManager::getReservationStats(
		int& activeReservationsOut, uint64& reservedQuantity,
		uint64& granted, uint64& consumed, uint64& released) {
	Locker mutationLocker(&persistenceMutationMutex);

	activeReservationsOut = activeReservations.size();
	granted = reservationsGranted;
	consumed = reservationsConsumed;
	released = reservationsReleased;
	reservedQuantity = 0;

	for (int i = 0; i < activeReservations.size(); ++i)
		reservedQuantity += activeReservations.get(i).quantity;
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

bool AiEconomyManager::snapshotStockpileTotalsByFamily(
		const Vector<String>& families,
		VectorMap<String, uint64>& totalsByFamily,
		String& status,
		const String& requiredAcquisitionSource) {
	totalsByFamily.removeAll();
	status = "unavailable";

	Locker mutationLocker(&persistenceMutationMutex);

	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr)
		return false;

	Vector<String> normalizedFamilies;
	for (int familyIndex = 0; familyIndex < families.size(); ++familyIndex) {
		String family = families.get(familyIndex).toLowerCase().trim();

		if (family.isEmpty() || family.length() > MAX_LABEL_LENGTH) {
			totalsByFamily.removeAll();
			status = "invalidFamily";
			return false;
		}

		if (normalizedFamilies.contains(family))
			continue;

		normalizedFamilies.add(family);
		totalsByFamily.put(family, 0);
	}

	for (int lotIndex = 0; ; ++lotIndex) {
		ManagedReference<AiEconomyStockpileLot*> lot;

		// Do not copy the full lot vector or inspection rows. The persistence
		// mutation mutex keeps the vector stable while each reference is copied;
		// the data lock is released before taking the lot lock, matching the
		// existing snapshot lock choreography.
		{
			Locker dataLocker(data);
			Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
				data->getStockpileLots();

			if (storedLots == nullptr) {
				totalsByFamily.removeAll();
				status = "nullStockpileLotVector";
				return false;
			}

			if (lotIndex >= storedLots->size())
				break;

			lot = storedLots->get(lotIndex);
		}

		if (lot == nullptr) {
			totalsByFamily.removeAll();
			status = "nullStockpileLot";
			return false;
		}

		String resourceType;
		String resourceClassChain;
		String acquisitionSource;
		String lifecycleState;
		uint64 quantity = 0;

		{
			Locker lotLocker(lot);
			resourceType = lot->getResourceType();
			resourceClassChain = lot->getResourceClassChain();
			acquisitionSource = lot->getAcquisitionSource();
			lifecycleState = lot->getResourceLifecycleState();
			quantity = lot->getQuantity();
		}

		if (lifecycleState == "despawned" || quantity == 0)
			continue;

		if (!requiredAcquisitionSource.isEmpty() &&
				acquisitionSource != requiredAcquisitionSource)
			continue;

		for (int familyIndex = 0;
				familyIndex < normalizedFamilies.size(); ++familyIndex) {
			String family = normalizedFamilies.get(familyIndex);

			if (!lotFieldsMatchFamily(
					resourceType, resourceClassChain, family))
				continue;

			uint64 current = totalsByFamily.get(family);
			if (quantity > static_cast<uint64>(-1) - current) {
				totalsByFamily.removeAll();
				status = "quantityOverflow";
				return false;
			}

			totalsByFamily.put(family, current + quantity);
		}
	}

	status = "ready";
	return true;
}

bool AiEconomyManager::snapshotStockpileInspection(
		AiEconomyStockpileInspectionSnapshot& snapshot,
		int maxLotRows,
		String& status) {
	snapshot.persistenceReady = false;
	snapshot.status = "unavailable";
	snapshot.loadedLots = 0;
	snapshot.conceptualMinerLots = 0;
	snapshot.totalQuantity = 0;
	snapshot.conceptualMinerQuantity = 0;
	snapshot.startupBaselineQuantity = 0;
	snapshot.reservedQuantity = 0;
	snapshot.availableQuantity = 0;
	snapshot.dataCreatedTimestampMs = 0;
	snapshot.dataUpdatedTimestampMs = 0;
	snapshot.conceptualMinerQuantities.removeAll();
	snapshot.startupBaselineQuantities.removeAll();
	snapshot.lots.removeAll();
	status = "unavailable";

	Locker mutationLocker(&persistenceMutationMutex);

	ManagedReference<AiEconomyData*> data = economyData;

	if (!persistenceReady.get() || data == nullptr)
		return false;

	snapshot.persistenceReady = true;

	for (int i = 0; i < conceptualMinerStartupTotals.size(); ++i) {
		String label = conceptualMinerStartupTotals.elementAt(i).getKey();
		uint64 quantity = conceptualMinerStartupTotals.get(i);

		if (label.isEmpty() || label.length() > MAX_LABEL_LENGTH ||
				quantity > MAX_STOCKPILE_QUANTITY) {
			status = "invalid";
			snapshot.status = status;
			snapshot.persistenceReady = false;
			return false;
		}

		if (quantity == 0)
			continue;

		snapshot.startupBaselineQuantities.put(label, quantity);
		snapshot.startupBaselineQuantity += quantity;
	}

	Vector<ManagedReference<AiEconomyStockpileLot*> > lots;

	{
		Locker dataLocker(data);
		snapshot.dataCreatedTimestampMs = data->getCreatedTimestamp();
		snapshot.dataUpdatedTimestampMs = data->getUpdatedTimestamp();

		Vector<ManagedReference<AiEconomyStockpileLot*> >* storedLots =
			data->getStockpileLots();

		if (storedLots == nullptr) {
			status = "invalid";
			snapshot.status = status;
			snapshot.persistenceReady = false;
			return false;
		}

		snapshot.loadedLots = storedLots->size();

		for (int i = 0; i < storedLots->size(); ++i)
			lots.add(storedLots->get(i));
	}

	for (int i = 0; i < lots.size(); ++i) {
		ManagedReference<AiEconomyStockpileLot*> lot = lots.get(i);

		if (lot == nullptr) {
			status = "invalid";
			snapshot.status = status;
			snapshot.persistenceReady = false;
			return false;
		}

		AiEconomyStockpileInspectionLot row;

		{
			Locker lotLocker(lot);
			row.entryID = lot->getEntryId();
			row.resourceSpawnObjectID = lot->getResourceSpawnObjectId();
			row.conceptualLabel = lot->getConceptualLabel();
			row.resourceSpawnName = lot->getResourceSpawnName();
			row.resourceType = lot->getResourceType();
			row.resourceClassChain = lot->getResourceClassChain();
			row.sourcePlanet = lot->getSourcePlanet();
			row.sourceZone = lot->getSourceZone();
			row.acquisitionSource = lot->getAcquisitionSource();
			row.resourceLifecycleState = lot->getResourceLifecycleState();
			row.ownerScope = lot->getOwnerScope();
			row.identityConfidence = lot->getIdentityConfidence();
			row.matchedDemandProfiles = lot->getMatchedDemandProfiles();
			row.qualityTier = lot->getQualityTier();
			row.quantity = lot->getQuantity();
			row.reservedQuantity = lot->getReservedQuantity();
			row.availableQuantity = lot->getAvailableQuantity();
			row.acquiredTimestampMs = lot->getAcquiredTimestamp();
			row.lastUpdatedTimestampMs = lot->getLastUpdatedTimestamp();
			row.activeAtAcquisition = lot->wasActiveAtAcquisition();
			row.oq = lot->getOq();
			row.cd = lot->getCd();
			row.dr = lot->getDr();
			row.hr = lot->getHr();
			row.fl = lot->getFl();
			row.ma = lot->getMa();
			row.pe = lot->getPe();
			row.sr = lot->getSr();
			row.ut = lot->getUt();
			row.cr = lot->getCr();
		}

		row.conceptualMinerLot =
			row.acquisitionSource == "conceptual_miner" &&
			row.resourceLifecycleState == "conceptual" &&
			row.ownerScope == "galaxy" &&
			row.identityConfidence == "conceptual_label";
		row.finishedGoodLot = row.identityConfidence == "finished_good";

		snapshot.totalQuantity += row.quantity;
		snapshot.reservedQuantity += row.reservedQuantity;
		snapshot.availableQuantity += row.availableQuantity;

		if (row.conceptualMinerLot) {
			snapshot.conceptualMinerLots++;
			snapshot.conceptualMinerQuantity += row.quantity;

			if (!row.conceptualLabel.isEmpty())
				snapshot.conceptualMinerQuantities.put(
					row.conceptualLabel, row.quantity);
		}

		if (maxLotRows <= 0 || snapshot.lots.size() < maxLotRows)
			snapshot.lots.add(row);
	}

	status = "ready";
	snapshot.status = status;
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
		"pve_hunter",
		"future_ai_harvester",
		"market_purchase",
		"admin_seed",
		"future_ai_crafter",
		"hive_crafter",
		"unknown"
	};
	const char* const lifecycleStates[] = {
		"active",
		"inactive",
		"despawned",
		"conceptual",
		"crafted",
		"unknown"
	};
	const char* const confidenceValues[] = {
		"exact_type",
		"coarse_family",
		"conceptual_label",
		"finished_good",
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
					acquisitionSource, acquisitionSources, 8) ||
				!isAllowedValue(lifecycleState, lifecycleStates, 6) ||
				!isAllowedValue(
					identityConfidence, confidenceValues, 5)) {
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
