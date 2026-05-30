// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UBlueprint;
class UEdGraph;

class FBlueprintAutoLayoutModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void ExecuteLayoutOnGraph(UEdGraph* Graph);

private:
	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();
	void OnBlueprintEditorOpened(UBlueprint* Blueprint);

	FDelegateHandle ToolMenusStartupHandle;
	FDelegateHandle BlueprintEditorOpenedHandle;
};
