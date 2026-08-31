-- simTraversalTest.lua
--
-- The C++ scenario controller owns every movement step.  This IDLE-only
-- override prevents the default wanderer from competing with it while the
-- inherited combat sockets remain available for real interrupt scenarios.

idleSimTraversalTest = {
	{id="1000000014", name="Wait", pid="none", args={duration=3600.0}}}
addAiTemplate("idleSimTraversalTest", idleSimTraversalTest)
