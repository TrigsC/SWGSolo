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
