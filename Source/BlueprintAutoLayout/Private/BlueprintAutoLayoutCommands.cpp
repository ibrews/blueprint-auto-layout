// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#include "BlueprintAutoLayoutCommands.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

void FBlueprintAutoLayoutCommands::RegisterCommands()
{
	UI_COMMAND(AutoLayoutGraph,
		"Auto Layout Graph",
		"Automatically arrange Blueprint nodes into a readable left-to-right execution flow",
		EUserInterfaceActionType::Button,
		FInputChord(EKeys::L, EModifierKey::Control | EModifierKey::Shift));
}

#undef LOCTEXT_NAMESPACE
