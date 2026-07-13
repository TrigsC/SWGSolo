-- FRS council robes are real wearable objects from the client TREs. Keep the
-- rank-to-appearance mapping here so ranked NPC Jedi use the same visual tier
-- boundaries as players (the robe certifications start at ranks 0/1/5/8/10).
darkJediFrsRankOutfits = {
	"object/tangible/wearables/robe/robe_jedi_dark_s01.iff", -- 0: Knight
	"object/tangible/wearables/robe/robe_jedi_dark_s02.iff", -- 1-4: Enforcer
	"object/tangible/wearables/robe/robe_jedi_dark_s02.iff",
	"object/tangible/wearables/robe/robe_jedi_dark_s02.iff",
	"object/tangible/wearables/robe/robe_jedi_dark_s02.iff",
	"object/tangible/wearables/robe/robe_jedi_dark_s03.iff", -- 5-7: Templar
	"object/tangible/wearables/robe/robe_jedi_dark_s03.iff",
	"object/tangible/wearables/robe/robe_jedi_dark_s03.iff",
	"object/tangible/wearables/robe/robe_jedi_dark_s04.iff", -- 8-9: Oppressor
	"object/tangible/wearables/robe/robe_jedi_dark_s04.iff",
	"object/tangible/wearables/robe/robe_jedi_dark_s05.iff", -- 10-11: Overlord / leader
	"object/tangible/wearables/robe/robe_jedi_dark_s05.iff"
}

lightJediFrsRankOutfits = {
	"object/tangible/wearables/robe/robe_jedi_light_s01.iff", -- 0: Knight
	"object/tangible/wearables/robe/robe_jedi_light_s02.iff", -- 1-4: Sentinel
	"object/tangible/wearables/robe/robe_jedi_light_s02.iff",
	"object/tangible/wearables/robe/robe_jedi_light_s02.iff",
	"object/tangible/wearables/robe/robe_jedi_light_s02.iff",
	"object/tangible/wearables/robe/robe_jedi_light_s03.iff", -- 5-7: Consular
	"object/tangible/wearables/robe/robe_jedi_light_s03.iff",
	"object/tangible/wearables/robe/robe_jedi_light_s03.iff",
	"object/tangible/wearables/robe/robe_jedi_light_s04.iff", -- 8-9: Arbiter
	"object/tangible/wearables/robe/robe_jedi_light_s04.iff",
	"object/tangible/wearables/robe/robe_jedi_light_s05.iff", -- 10-11: Council / leader
	"object/tangible/wearables/robe/robe_jedi_light_s05.iff"
}

-- Stock client blade palette IDs from jedi_spam:saber_color_*.
-- Ranked light Jedi choose equally from Light Green, Blue, Yellow, and Light
-- Purple. Dark/Imperial Jedi retain the weapon template's existing red blade.
lightJediSaberColors = { 2, 4, 6, 8 }