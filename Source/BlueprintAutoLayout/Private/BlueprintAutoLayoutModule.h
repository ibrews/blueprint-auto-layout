// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UEdGraph;
class UEdGraphNode;
class FBlueprintAutoLayoutInputProcessor;

class FBlueprintAutoLayoutModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void ExecuteLayoutOnGraph(UEdGraph* Graph);
	static void ExecuteLayoutAndGroupOnGraph(UEdGraph* Graph);
	static void ExecuteLayoutAndRouteOnGraph(UEdGraph* Graph);
	static void ExecuteLayoutGroupAndRouteOnGraph(UEdGraph* Graph);
	static void ExecuteLayoutSelectedOnGraph(UEdGraph* Graph, const TArray<UEdGraphNode*>& SelectedNodes);

private:
	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();
	void RegisterToolbarExtension();

	FDelegateHandle ToolMenusStartupHandle;

	// Slate input pre-processor that fires the rebindable keyboard shortcuts when a Blueprint
	// graph editor is focused. Registered in StartupModule, removed in ShutdownModule.
	TSharedPtr<FBlueprintAutoLayoutInputProcessor> InputProcessor;
};
