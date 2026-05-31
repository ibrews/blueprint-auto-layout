// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#include "BlueprintAutoLayoutModule.h"
#include "BlueprintAutoLayout.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Commands/UIAction.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenu.h"
#include "ToolMenuOwner.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

static const FName GAutoLayoutOwnerName("BlueprintAutoLayout");

void FBlueprintAutoLayoutModule::StartupModule()
{
	// UToolMenus may not be ready when this module starts. Defer registration
	// to a startup callback that fires once the menu system is initialized.
	ToolMenusStartupHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FBlueprintAutoLayoutModule::RegisterMenuExtensions));
}

void FBlueprintAutoLayoutModule::ShutdownModule()
{
	UnregisterMenuExtensions();

	if (ToolMenusStartupHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(ToolMenusStartupHandle);
		ToolMenusStartupHandle.Reset();
	}
}

void FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AutoLayoutGraphTransaction", "Auto Layout Graph"));
	Graph->Modify();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Node->Modify();
		}
	}

	FBlueprintAutoLayout Layout;
	Layout.LayoutGraph(Graph);

	Graph->NotifyGraphChanged();
}

void FBlueprintAutoLayoutModule::ExecuteLayoutAndGroupOnGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AutoLayoutGroupGraphTransaction", "Auto Layout & Group Graph"));
	Graph->Modify();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Node->Modify();
		}
	}

	FBlueprintAutoLayout Layout;
	Layout.LayoutAndGroupGraph(Graph);

	Graph->NotifyGraphChanged();
}

void FBlueprintAutoLayoutModule::RegisterMenuExtensions()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// Scope all subsequent registrations to our named owner so ShutdownModule()
	// can cleanly tear everything down via UnregisterOwnerByName.
	FToolMenuOwnerScoped OwnerScope(GAutoLayoutOwnerName);

	// The Blueprint graph context menu is built from a chain of UToolMenus, one per schema
	// in the schema's inheritance chain. The K2 schema's menu name follows the convention:
	//   GraphEditor.GraphContextMenu.EdGraphSchema_K2
	// Hooking this level catches all UEdGraphSchema_K2-derived schemas (Blueprint, Anim BP).
	const FName K2ContextMenuName = UEdGraphSchema::GetContextMenuName(UEdGraphSchema_K2::StaticClass());

	UToolMenu* Menu = ToolMenus->ExtendMenu(K2ContextMenuName);
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(
		"BlueprintAutoLayout",
		LOCTEXT("BlueprintAutoLayoutSectionLabel", "Layout"));

	Section.AddDynamicEntry(
		"AutoLayoutGraph",
		FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UGraphNodeContextMenuContext* Context = InSection.FindContext<UGraphNodeContextMenuContext>();
			if (!Context)
			{
				return;
			}

			// Only show on empty-graph context (no specific node, no specific pin).
			if (Context->Node || Context->Pin)
			{
				return;
			}

			UEdGraph* Graph = const_cast<UEdGraph*>(Context->Graph.Get());
			if (!Graph)
			{
				return;
			}

			InSection.AddMenuEntry(
				"AutoLayoutGraph",
				LOCTEXT("AutoLayoutGraphLabel", "Auto Layout Graph"),
				LOCTEXT("AutoLayoutGraphTooltip", "Automatically arrange this graph's nodes for readable execution flow"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop"),
				FUIAction(FExecuteAction::CreateLambda([Graph]()
				{
					FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(Graph);
				})));

			InSection.AddMenuEntry(
				"AutoLayoutAndGroupGraph",
				LOCTEXT("AutoLayoutGroupGraphLabel", "Auto Layout && Group Graph"),
				LOCTEXT("AutoLayoutGroupGraphTooltip", "Arrange the graph, then wrap each event/function subtree in a comment box named after its root"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop"),
				FUIAction(FExecuteAction::CreateLambda([Graph]()
				{
					FBlueprintAutoLayoutModule::ExecuteLayoutAndGroupOnGraph(Graph);
				})));
		}));
}

void FBlueprintAutoLayoutModule::UnregisterMenuExtensions()
{
	if (UObjectInitialized() && UToolMenus::Get())
	{
		UToolMenus::Get()->UnregisterOwnerByName(GAutoLayoutOwnerName);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintAutoLayoutModule, BlueprintAutoLayout)
