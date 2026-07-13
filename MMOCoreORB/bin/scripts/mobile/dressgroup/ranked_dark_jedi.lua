-- Ranked Jedi wear their FRS robe as a server-side wearable. Keep this group
-- limited to mobile IFFs whose client CDF does not bake another outfit over it.
--
-- The Imperial pool intentionally favors Humans. The available generic Zabrak
-- body renders without the species' horns/markings, and every Chiss body has a
-- dressed CDF, so neither is suitable for the production pool yet.
ranked_dark_jedi = {
	"object/mobile/ranked_jedi_human_male.iff",
	"object/mobile/ranked_jedi_human_female.iff",
	"object/mobile/ranked_jedi_human_male.iff",
	"object/mobile/ranked_jedi_human_female.iff",
	"object/mobile/ranked_jedi_human_male.iff",
	"object/mobile/ranked_jedi_human_female.iff",
	"object/mobile/ranked_jedi_human_male.iff",
	"object/mobile/ranked_jedi_human_female.iff",
	"object/mobile/ranked_jedi_zabrak_male.iff",
	"object/mobile/ranked_jedi_zabrak_female.iff"
}

addDressGroup("ranked_dark_jedi", ranked_dark_jedi)