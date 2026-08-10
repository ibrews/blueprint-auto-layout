// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#include "BlueprintAutoLayoutCommands.h"
#include "Framework/Commands/InputChord.h"
#include "InputCoreTypes.h"
#if ENGINE_MAJOR_VERSION >= 5
    #include "Styling/AppStyle.h"
    #define BPAL_STYLE_SETNAME FAppStyle::GetAppStyleSetName()
#else
    #include "EditorStyleSet.h"
    #define BPAL_STYLE_SETNAME FEditorStyle::GetStyleSetName()
#endif

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

FBlueprintAutoLayoutCommands::FBlueprintAutoLayoutCommands()
	: TCommands<FBlueprintAutoLayoutCommands>(
		TEXT("BlueprintAutoLayout"),
		NSLOCTEXT("Contexts", "BlueprintAutoLayout", "Blueprint Anti-Pasta"),
		NAME_None,
		BPAL_STYLE_SETNAME)
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
