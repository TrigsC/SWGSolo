/*
                Copyright <SWGEmu>
        See file COPYING for copying conditions.*/

#ifdef WITH_REST_API

#include "engine/engine.h"
#include "server/zone/objects/creature/simplayer/SimPlayerManager.h"
#include "APIProxyAiEconomyManager.h"
#include "APIRequest.h"

namespace server {
 namespace web3 {

void APIProxyAiEconomyManager::handleDashboardGET(APIRequest& apiRequest) {
	JSONSerializationType result;

	result["result"] = SimPlayerManager::instance()->getAiEconomyDashboardSnapshot();

	apiRequest.success(result);
}

void APIProxyAiEconomyManager::handlePlayerBotGET(APIRequest& apiRequest) {
	uint64 identityId = apiRequest.getPathFieldUnsignedLong("identityId");
	SimBotIdentity identity;
	if (identityId == 0 ||
			!SimPlayerManager::instance()->snapshotPlayerBotIdentity(identityId,
				identity)) {
		apiRequest.fail("PlayerBot not found.", "Unknown PlayerBot identity: " +
			String::valueOf(identityId), APIRequestStatus::NotFound);
		return;
	}

	JSONSerializationType result;
	result["result"] = SimPlayerManager::instance()->getPlayerBotInspectJson(
		identityId);
	apiRequest.success(result);
}

void APIProxyAiEconomyManager::handle(APIRequest& apiRequest) {
	if (apiRequest.isMethodGET()) {
		handleDashboardGET(apiRequest);
		return;
	}

	apiRequest.fail("Unsupported method.");
}

}
}

#endif // WITH_REST_API
