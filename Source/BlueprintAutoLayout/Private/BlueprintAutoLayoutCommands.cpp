// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#include "BlueprintAutoLayoutCommands.h"
#include "Framework/Commands/InputChord.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

FBlueprintAutoLayoutCommands::FBlueprintAutoLayoutCommands()
	: TCommands<FBlueprintAutoLayoutCommands>(
		TEXT("BlueprintAutoLayout"),
		NSLOCTEXT("Contexts", "BlueprintAutoLayout", "Blueprint Auto Layout"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FBlueprintAutoLayoutCommands::RegisterCommands()
{
	UI_COMMAND(AutoLayoutGraph, "Auto Layout Graph",
		"Automatically arrange the focused Blueprint graph for readable execution flow",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::L));

	UI_COMMAND(LayoutSelected, "Auto Layout Selected Nodes",
		"Automatically arrange only the selected nodes in the focused Blueprint graph",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::K));
}

#undef LOCTEXT_NAMESPACE
