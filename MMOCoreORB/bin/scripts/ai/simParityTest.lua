-- simParityTest.lua
--
-- The progression parity harness is manager/controller driven. Keep the AI
-- tree idle so it cannot compete with the identity-body lifecycle checks.

idleSimParityTest = {
	{id="1000000015", name="Wait", pid="none", args={duration=3600.0}}}
addAiTemplate("idleSimParityTest", idleSimParityTest)
