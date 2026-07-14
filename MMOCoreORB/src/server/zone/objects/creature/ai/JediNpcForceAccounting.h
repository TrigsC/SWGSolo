/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef JEDINPCFORCEACCOUNTING_H_
#define JEDINPCFORCEACCOUNTING_H_

namespace sys {
namespace lang {
class String;
}
}

namespace server {
namespace zone {
namespace objects {
namespace creature {
namespace ai {
class AiAgent;
}
}
}
}
}

namespace JediNpcForceAccounting {

using server::zone::objects::creature::ai::AiAgent;

// The single mutation gateway for runtime NPC Jedi Force changes. The caller
// supplies the intended signed delta and resulting pool value; the gateway
// clamps the pool and emits an auditable before/after ledger entry.
void apply(AiAgent* agent, const sys::lang::String& event,
	const sys::lang::String& source,
	int requestedDelta, int newForce);

// Records decisions that do not mutate the pool (spawn baseline, insufficient
// Force, or a mitigation buff dropping before it can pay the next hit).
void record(AiAgent* agent, const sys::lang::String& event,
	const sys::lang::String& source,
	int requestedDelta, int beforeForce, int afterForce);

} // namespace JediNpcForceAccounting

#endif // JEDINPCFORCEACCOUNTING_H_
