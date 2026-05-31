// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UEdGraph;

class FBlueprintAutoLayoutModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void ExecuteLayoutOnGraph(UEdGraph* Graph);
	static void ExecuteLayoutAndGroupOnGraph(UEdGraph* Graph);

private:
	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();

	FDelegateHandle ToolMenusStartupHandle;
};
