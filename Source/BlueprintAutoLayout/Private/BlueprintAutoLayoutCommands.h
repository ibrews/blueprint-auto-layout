// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FBlueprintAutoLayoutCommands : public TCommands<FBlueprintAutoLayoutCommands>
{
public:
	FBlueprintAutoLayoutCommands()
		: TCommands<FBlueprintAutoLayoutCommands>(
			TEXT("BlueprintAutoLayout"),
			NSLOCTEXT("Contexts", "BlueprintAutoLayout", "Blueprint Auto Layout"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> AutoLayoutGraph;
};
