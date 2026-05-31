// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

using UnrealBuildTool;

public class BlueprintAutoLayout : ModuleRules
{
	public BlueprintAutoLayout(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",        // FInputChord / EKeys for the keyboard shortcuts
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"BlueprintGraph",
			"Kismet",
			"GraphEditor",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"EditorStyle",
			"EditorFramework",
			"DeveloperSettings",
		});
	}
}
