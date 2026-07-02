-- simMiner.lua
--
-- No-op behavior tree for SimPlayer miner bots.
--
-- SimMiner movement is driven entirely by SimMinerController (C++), which
-- computes a path, feeds patrol points, and steps the agent via
-- findNextPosition. The default creature tree (idleDefault) would compete with
-- that controller: it walks the same patrol queue (forcing moveMode=WALK) and,
-- once the queue empties, calls GeneratePatrol to wander ~25m around "home",
-- pulling stationed miners off their resource target.
--
-- Assigning this tree makes the AiBehaviorEvent a harmless idle Wait: the root
-- only ever enters the IDLE socket, so the MOVE socket / GeneratePatrol never
-- run, leaving the controller as the single movement authority.

idleSimMiner = {
	{id="1000000001",	name="Wait",	pid="none",	args={duration=3600.0}}}
addAiTemplate("idleSimMiner", idleSimMiner)

rootSimMiner = {
	{id="1000000002",	name="Selector",	pid="none"},
	{id="1000000003",	name="TreeSocket",	pid="1000000002",	args={slot=IDLE}}}
addAiTemplate("rootSimMiner", rootSimMiner)
