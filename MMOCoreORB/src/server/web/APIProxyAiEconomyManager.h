/*
                Copyright <SWGEmu>
        See file COPYING for copying conditions.*/

#pragma once

#include "BaseAPIProxy.h"

namespace server {
 namespace web3 {
	class APIRequest;

	class APIProxyAiEconomyManager : public BaseAPIProxy {
	public:
		APIProxyAiEconomyManager() : BaseAPIProxy("AiEconomyManager") {
		}

		void handle(APIRequest& apiRequest);
	private:
		void handleDashboardGET(APIRequest& apiRequest);
	};
 }
}
