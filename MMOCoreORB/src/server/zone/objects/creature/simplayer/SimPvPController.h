/*
 * SimPvPController.h
 * P.6.1 SimPvP squad controllers (leader city-loop driver + follow member).
 *
 * Replaces the legacy solo PvP bot (destroy+respawn cycling, "patrol" custom
 * AI map that matched nothing, presentation-bit wipe). Squads are persistent:
 * the leader runs shuttleport -> hangout -> loiter -> shuttleport and the
 * manager travels the whole squad between cities with the proven switchZone
 * outdoor reposition (P.4.5). Members are engine-driven followers (the GCW
 * security-patrol FOLLOW pattern); their controller only self-heals the
 * follow, scans for targets and reports deaths. All of it is gated behind
 * pvpConfig.enablePvpBots (C++ default off).
 */

#ifndef SIMPVPCONTROLLER_H_
#define SIMPVPCONTROLLER_H_

#include "SimPlayerController.h"

#include "engine/core/Task.h"
#include "system/lang/String.h"
#include "engine/util/u3d/Vector3.h"

class SimPlayerManager;
class SimPvPController;

// Leader loiter timer ("post up at the starport for a while, then move on").
// Generation-guarded like every other SimPlayer work-loop task so a stale
// timer can never double-fire after the leader has already moved.
class SimPvpLoiterTask : public Task {
	WeakReference<SimPvPController*> controller;
	uint64 generation;

public:
	SimPvpLoiterTask(SimPvPController* ctrl, uint64 g)
		: controller(ctrl), generation(g) {
	}
	void run() override;
};

// ---------------------------------------------------------------------------
// Shared PvP bot behavior: faction identity, target scan, death reporting.
// ---------------------------------------------------------------------------
class SimPvpBotController : public SimPlayerController {
protected:
	uint64 squadId;
	bool imperial;
	// One death report per life; the manager owns roster/cleanup/respawn.
	bool deathReported;
	// P.6.2: scouts scan wider and (when pvpConfig.scouts.reportOnly) REPORT
	// contacts to the manager instead of engaging; a patrol squad of the same
	// faction then converges on the scout's city.
	bool scoutRole;
	uint64 lastContactReportMs;
	// P.6.2a: consecutive ticks seen "in combat" with no reachable live enemy.
	// A fight whose defender died/left leaves isInCombat() stuck true, which
	// freezes the controller AND blocks the maintenance TTL; after a few such
	// ticks we clear the stale combat state so the loop resumes.
	int phantomCombatTicks;

public:
	SimPvpBotController(AiAgent* aiAgent, uint64 squad, bool isImperial);
	virtual ~SimPvpBotController();

	uint64 getSquadId() const { return squadId; }
	bool isImperial() const { return imperial; }
	void setScoutRole(bool scout) { scoutRole = scout; }
	bool isScoutRole() const { return scoutRole; }

	// Called every arrival-check tick (~1s). Reports death once, stays quiet
	// in combat, otherwise scans for attackable enemies.
	void onTick() override;

	// Scan CloseObjects for an enemy to engage: overt enemy-faction players
	// always; enemy sim bots only behind pvpConfig.allowBotVsBotCombat.
	// Engagement mirrors the proven lock choreography (agent lock, cross-lock
	// target, setTargetObject/addDefender/setCombatState).
	void scanForTargets();

	virtual const char* getPvpRoleName() const = 0;
	virtual String getPvpPhaseName() const = 0;
};

// ---------------------------------------------------------------------------
// Squad leader: drives the city loop.
//   TO_HANGOUT -> LOITERING -> TO_SHUTTLE -> AWAITING_SHUTTLE -> (manager
//   boards the squad via switchZone) -> TO_HANGOUT on the next city.
// ---------------------------------------------------------------------------
class SimPvPController : public SimPvpBotController {
public:
	enum PvpPhase {
		PVP_FORMING,
		PVP_TO_HANGOUT,
		PVP_LOITERING,
		PVP_TO_SHUTTLE,
		PVP_AWAITING_SHUTTLE
	};

	SimPvPController(AiAgent* aiAgent, uint64 squad, bool isImperial);
	virtual ~SimPvPController();

	// Set (or reset, after boarding) the current city route, then re-enter the
	// loop at TO_HANGOUT. Safe to call right after switchZone.
	void beginCityLoop(const String& planetName, const String& cityName,
		const Vector3& shuttlePos, const Vector3& hangoutPos);

	// P.6.5a routed travel: a TRANSIT stop on a multi-leg journey. The squad
	// waits at the arrival pad for the connecting ship (short dwell, no
	// arrival/departure announces), then re-enters AWAITING_SHUTTLE so the
	// manager boards the next leg. Reuses the whole city-loop phase machine
	// with hangout == shuttle == the pad.
	void beginTransitStop(const String& planetName, const String& cityName,
		const Vector3& padPos, int dwellSeconds);

	// SimPlayerController interface. startSimLoop() (re)drives the CURRENT
	// phase, so the base path-fail retry task naturally resumes the loop.
	void startSimLoop() override;
	void onArrived() override;
	void onPathFailed() override;
	void prepareForRelocation(const String& reason) override;
	// P.6.1b: city legs must end at the requested target - rejects any stale
	// path that slipped a work-loop generation race (observed live: a boarded
	// leader accepted a pre-teleport path and sprinted toward the previous
	// city's pad on the new planet).
	bool acceptFoundPath(const Vector3& pathEnd) override;

	// P.6.1c diagnostics-only: heartbeat that logs the leader's live movement
	// vs its phase target so we can see (from the log alone) whether the tick
	// chain is alive, which way it walks, and whether patrol points are fed.
	void onTick() override;

	const char* getPvpRoleName() const override { return "pvp_leader"; }
	String getPvpPhaseName() const override;
	int getPvpPhase() const { return phase; }
	uint64 getPhaseSinceMs() const { return phaseSinceMs; }
	bool isCurrentShuttleCollectorTarget() const { return shuttleTargetIsCollector; }

	// Maintenance-task escalation: a phase that exceeded its TTL is forced
	// forward (post up where we are / travel from where we are) so a squad can
	// never wedge in a movement phase.
	void forceAdvancePhase(const String& reason);

	// P.6.2: break off the current city business and head for the shuttle so
	// the squad can travel to a reported contact (boardPvpSquad consumes the
	// squad's pending convergence destination). No-op if already outbound.
	void interruptForConvergence();

private:
	friend class SimPvpLoiterTask;

	void drivePhase(const String& reason);
	void setPhase(PvpPhase newPhase);
	void startLoitering();
	void finishLoitering();
	void enterToShuttle(const String& reason);
	void notifyReadyToTravel();
	// P.6.1a: hard-stop the agent's engine-side movement (patrol queue, saved
	// points, movement state). An interrupted leg (TTL force-advance, board)
	// must never leave stale movement running - the engine's own movement
	// event walks whatever is queued regardless of controller generations.
	void haltAgentMovement(const String& reason);
	// P.6.1c diagnostics-only: log the target coords each moveTo leg uses.
	void logMoveTarget(const char* label, const Vector3& target);

private:
	PvpPhase phase;
	uint64 phaseSinceMs;
	String planet;
	String city;
	Vector3 shuttleLocation;
	Vector3 shuttleTargetLocalPosition;
	uint64 shuttleTargetCellOid = 0;
	bool shuttleTargetIsCollector = false;
	Vector3 hangoutLocation;
	// Consecutive path failures for the current movement phase; bounded, then
	// the phase is forced forward instead of retrying forever.
	int pathFailStreak;
	// P.6.5a: >0 while stopped at a TRANSIT pad on a multi-leg journey - used
	// as the loiter duration (seconds) and to suppress the arrival/departure
	// announces. Reset to 0 by beginCityLoop (a real city stop).
	int transitLoiterSeconds = 0;
	// P.6.1c diagnostics-only: throttles the heartbeat log.
	int heartbeatTicks = 0;
};

// ---------------------------------------------------------------------------
// Squad member: engine FOLLOW drives movement; this controller only keeps the
// follow healthy, scans for targets and reports deaths.
// ---------------------------------------------------------------------------
class SimPvPMemberController : public SimPvpBotController {
	ManagedReference<AiAgent*> leaderAgent;

public:
	SimPvPMemberController(AiAgent* aiAgent, uint64 squad, bool isImperial,
		AiAgent* leader);
	virtual ~SimPvPMemberController();

	// Starts the 1s arrival-check/onTick chain and asserts the follow. The
	// member never calls moveTo(); the FOLLOW behavior trees own movement, so
	// there is no dual-driver by construction.
	void startSimLoop() override;
	void onArrived() override;
	void onTick() override;

	// Point the member at (a possibly new) leader and put it back into
	// FOLLOWING. Idempotent; used at spawn, after boarding, after combat and
	// on leader promotion. Takes/releases its own locks.
	void assertFollow();
	void setLeader(AiAgent* leader);

	const char* getPvpRoleName() const override { return "pvp_member"; }
	String getPvpPhaseName() const override { return "following"; }
};

#endif /* SIMPVPCONTROLLER_H_ */
