object_tangible_loot_loot_schematic_heavy_reaper_cannon_schematic = object_tangible_loot_loot_schematic_shared_heavy_reaper_cannon_schematic:new {
	templateType = LOOTSCHEMATIC,
	objectMenuComponent = "LootSchematicMenuComponent",
	attributeListComponent = "LootSchematicAttributeListComponent",
	requiredSkill = "crafting_weaponsmith_master",
	targetDraftSchematic = "object/draft_schematic/weapon/heavy_reaper_cannon.iff",
	targetUseCount = 1
}

ObjectTemplates:addTemplate(object_tangible_loot_loot_schematic_heavy_reaper_cannon_schematic, "object/tangible/loot/loot_schematic/heavy_reaper_cannon_schematic.iff")
