-- simHunter.lua
--
-- P.8.1 hunter bodies are controller-driven for movement. Only IDLE is
-- overridden; the default root/attack/target/equip/kill sockets remain live
-- so the explicitly equipped rifle can drive the stock combat tree.

idleSimHunter = {
	{id="1000000012", name="Wait", pid="none", args={duration=3600.0}}}
addAiTemplate("idleSimHunter", idleSimHunter)
