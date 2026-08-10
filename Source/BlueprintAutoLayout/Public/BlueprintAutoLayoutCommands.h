// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayoutCommands.h - Rebindable editor commands (keyboard shortcuts).

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/**
 * The plugin's commands. Registering this TCommands makes the shortcuts appear in
 * Editor Preferences → Keyboard Shortcuts (under "Blueprint Anti-Pasta"), where users can
 * rebind them. The default chords are read live at key-press time by the input pre-processor,
 * so rebinding takes effect without a restart.
 */
class FBlueprintAutoLayoutCommands : public TCommands<FBlueprintAutoLayoutCommands>
{
public:
	FBlueprintAutoLayoutCommands();

	virtual void RegisterCommands() override;

	/** Auto-lay-out the whole focused Blueprint graph (default: Ctrl/Cmd+Shift+L). */
	TSharedPtr<FUICommandInfo> AutoLayoutGraph;

	/** Auto-lay-out only the selected nodes in the focused graph (default: Ctrl/Cmd+Shift+K). */
	TSharedPtr<FUICommandInfo> LayoutSelected;
};
