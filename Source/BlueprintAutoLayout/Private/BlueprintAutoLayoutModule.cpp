// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#include "BlueprintAutoLayoutModule.h"
#include "BlueprintAutoLayout.h"
#include "BlueprintAutoLayoutCommands.h"

#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Commands/UIAction.h"
#include "KismetEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenu.h"
#include "ToolMenuOwner.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

static const FName GAutoLayoutOwnerName("BlueprintAutoLayout");

void FBlueprintAutoLayoutModule::StartupModule()
{
	FBlueprintAutoLayoutCommands::Register();

	// Subscribe to Blueprint editor open events so we can bind the hotkey to each editor's command list.
	IBlueprintEditorModule& BPEditorModule = FModuleManager::LoadModuleChecked<IBlueprintEditorModule>("Kismet");
	BlueprintEditorOpenedHandle = BPEditorModule.OnBlueprintEditorOpened().AddRaw(
		this, &FBlueprintAutoLayoutModule::OnBlueprintEditorOpened);

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

	if (IBlueprintEditorModule* BPEditorModule = FModuleManager::GetModulePtr<IBlueprintEditorModule>("Kismet"))
	{
		BPEditorModule->OnBlueprintEditorOpened().Remove(BlueprintEditorOpenedHandle);
	}

	FBlueprintAutoLayoutCommands::Unregister();
}

void FBlueprintAutoLayoutModule::OnBlueprintEditorOpened(UBlueprint* Blueprint)
{
	if (!Blueprint || !GEditor) return;

	UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Sub) return;

	IAssetEditorInstance* Instance = Sub->FindEditorForAsset(Blueprint, false);
	if (!Instance || Instance->GetEditorName() != FName("BlueprintEditor")) return;

	// Safe downcast: we verified the concrete type is FBlueprintEditor via GetEditorName().
	// FBlueprintEditor → FBlueprintEditorToolkit → FAssetEditorToolkit → IAssetEditorInstance
	// is a single-inheritance chain, so the static_cast adjusts the pointer correctly.
	FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(Instance);
	TWeakPtr<FBlueprintEditor> WeakEditor =
		StaticCastSharedRef<FBlueprintEditor>(BPEditor->AsShared());

	BPEditor->GetToolkitCommands()->MapAction(
		FBlueprintAutoLayoutCommands::Get().AutoLayoutGraph,
		FExecuteAction::CreateLambda([WeakEditor]()
		{
			TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
			if (!Editor.IsValid()) return;

			UEdGraph* Graph = Editor->GetFocusedGraph();
			if (Graph)
			{
				FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(Graph);
			}
		}));
}

void FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(UEdGraph* Graph)
{
	if (!Graph) return;

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

void FBlueprintAutoLayoutModule::RegisterMenuExtensions()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScope(GAutoLayoutOwnerName);

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
				LOCTEXT("AutoLayoutGraphTooltip", "Automatically arrange this graph's nodes for readable execution flow  (Ctrl+Shift+L)"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop"),
				FUIAction(FExecuteAction::CreateLambda([Graph]()
				{
					FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(Graph);
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
