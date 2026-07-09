-- simPvp.lua
--
-- Custom AI map for SimPvP squad LEADERS (P.6.1).
--
-- The leader's movement is driven entirely by SimPvPController (C++), which
-- computes a path, feeds patrol points, and steps the agent via
-- findNextPosition - the same proven single-mover architecture as SimMiners.
-- The default IDLE tree would compete with it: it walks the patrol queue at
-- forced moveMode=WALK and, once the queue empties, GeneratePatrol wanders the
-- bot ~25m off its post.
--
-- Unlike simMiner, ONLY the IDLE slot is overridden here. The NONE (root)
-- slot intentionally falls back to rootDefault via AiMap's per-slot lookup,
-- so the ATTACK/TARGET/HEAL combat sockets still run and PvP bots fight back.
--
-- Squad MEMBERS do not use this map: they run the engine-native FOLLOW
-- bitmask trees (runEscort) like GCW security patrols.

idleSimPvp = {
	{id="1000000011",	name="Wait",	pid="none",	args={duration=3600.0}}}
addAiTemplate("idleSimPvp", idleSimPvp)
