// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.

#include "BlueprintAutoLayoutModule.h"
#include "BlueprintAutoLayout.h"
#include "BlueprintAutoLayoutSettings.h"
#include "BlueprintAutoLayoutCommands.h"

#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UIAction.h"
#include "GraphEditor.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuOwner.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

static const FName GAutoLayoutOwnerName("BlueprintAutoLayout");

//------------------------------------------------------------------------------
// Keyboard shortcut handling
//------------------------------------------------------------------------------

// Walk up the widget tree from the focused widget to find the Blueprint graph editor (if any)
// the user is currently working in. Returns null when focus isn't inside a K2 graph editor.
static TSharedPtr<SGraphEditor> FindFocusedBlueprintGraphEditor(FSlateApplication& SlateApp, int32 UserIndex)
{
	TSharedPtr<SGraphEditor> Result;
	for (TSharedPtr<SWidget> W = SlateApp.GetUserFocusedWidget(UserIndex); W.IsValid(); W = W->GetParentWidget())
	{
		// Both the SGraphEditor facade and its inner SGraphEditorImpl derive from SGraphEditor,
		// so a type-name match + static cast is safe for whichever we hit first.
		if (W->GetType().ToString().Contains(TEXT("GraphEditor")))
		{
			Result = StaticCastSharedPtr<SGraphEditor>(W);
			break;
		}
	}

	if (Result.IsValid())
	{
		const UEdGraph* Graph = Result->GetCurrentGraph();
		if (!Graph || !Graph->GetSchema() || !Graph->GetSchema()->IsA<UEdGraphSchema_K2>())
		{
			// A non-Blueprint graph editor (material, Niagara, …) — not ours to lay out.
			Result.Reset();
		}
	}
	return Result;
}

// Slate pre-processor that runs the plugin's rebindable shortcuts when a Blueprint graph editor
// is focused. It reads each command's CURRENT chord, so rebinding in Keyboard Shortcuts applies live.
class FBlueprintAutoLayoutInputProcessor : public IInputProcessor
{
public:
	virtual ~FBlueprintAutoLayoutInputProcessor() override = default;

	virtual void Tick(const float /*DeltaTime*/, FSlateApplication& /*SlateApp*/, TSharedRef<ICursor> /*Cursor*/) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		const FBlueprintAutoLayoutCommands& Cmds = FBlueprintAutoLayoutCommands::Get();
		if (!Cmds.AutoLayoutGraph.IsValid() || !Cmds.LayoutSelected.IsValid())
		{
			return false;
		}

		// FInputChord(Key, bShift, bCtrl, bAlt, bCmd)
		const FInputChord Pressed(
			InKeyEvent.GetKey(),
			InKeyEvent.IsShiftDown(), InKeyEvent.IsControlDown(),
			InKeyEvent.IsAltDown(), InKeyEvent.IsCommandDown());

		const bool bWantLayout = Cmds.AutoLayoutGraph->HasActiveChord(Pressed);
		const bool bWantSelected = Cmds.LayoutSelected->HasActiveChord(Pressed);
		if (!bWantLayout && !bWantSelected)
		{
			return false;
		}

		TSharedPtr<SGraphEditor> GraphEd = FindFocusedBlueprintGraphEditor(SlateApp, InKeyEvent.GetUserIndex());
		if (!GraphEd.IsValid())
		{
			return false; // not in a Blueprint graph — let the key fall through to other handlers
		}

		UEdGraph* Graph = GraphEd->GetCurrentGraph();
		if (bWantSelected)
		{
			TArray<UEdGraphNode*> Selected;
			for (UObject* Obj : GraphEd->GetSelectedNodes())
			{
				if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
				{
					Selected.Add(Node);
				}
			}
			FBlueprintAutoLayoutModule::ExecuteLayoutSelectedOnGraph(Graph, Selected);
		}
		else
		{
			FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(Graph);
		}
		return true; // consumed
	}
};

// Build the layout config from the user's editor preferences (spacing, color, straighten
// tolerance). Returns the chosen wire-handling mode via OutMode so callers pick the entry point.
static FBlueprintLayoutConfig GetLayoutConfig(EBPALWireHandling& OutMode)
{
	FBlueprintLayoutConfig Config;
	OutMode = EBPALWireHandling::StraightenAndMove;
	if (const UBlueprintAutoLayoutSettings* Settings = GetDefault<UBlueprintAutoLayoutSettings>())
	{
		OutMode = Settings->WireHandling;

		// Layered (Sugiyama) engine on/off, and feed the user's spacing into its column/row gaps.
		Config.bUseLayeredEngine = Settings->bUseLayeredEngine;
		Config.bMaterializeLongEdges = Settings->bMaterializeLongEdges;
		Config.LayeredRankSpacingX = (float)Settings->HorizontalSpacing;
		Config.LayeredNodeSpacingY = (float)Settings->VerticalSpacing;

		Config.CommentColorMode = (Settings->CommentColorMode == EBPALCommentColorMode::CyclingPalette)
			? ECommentColorMode::CyclingPalette
			: ECommentColorMode::KeywordSemantic;

		Config.NodePaddingX = Settings->HorizontalSpacing;
		Config.NodePaddingY = Settings->VerticalSpacing;
		Config.BranchExtraPaddingY = Settings->BranchSpacing;
		Config.RootExtraPaddingY = Settings->EventSpacing;
		Config.MaxStraightenNudgeY = Settings->StraightenMaxNudge;

		// "Off" lays out nodes but leaves wires alone; every other mode straightens + re-lanes.
		const bool bStraighten = (Settings->WireHandling != EBPALWireHandling::Off);
		Config.bStraightenWires = bStraighten;
		Config.bStackSequenceOutputs = bStraighten;
	}
	return Config;
}

void FBlueprintAutoLayoutModule::StartupModule()
{
	// Register the rebindable commands so they appear in Editor Preferences → Keyboard Shortcuts.
	FBlueprintAutoLayoutCommands::Register();

	// Install a Slate input pre-processor that fires those shortcuts when a Blueprint graph
	// editor is focused. (Done here rather than per-graph-editor so a single binding covers all.)
	if (FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FBlueprintAutoLayoutInputProcessor>();
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}

	// UToolMenus may not be ready when this module starts. Defer registration
	// to a startup callback that fires once the menu system is initialized.
	ToolMenusStartupHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FBlueprintAutoLayoutModule::RegisterMenuExtensions));
}

void FBlueprintAutoLayoutModule::ShutdownModule()
{
	UnregisterMenuExtensions();

	if (InputProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		}
		InputProcessor.Reset();
	}

	FBlueprintAutoLayoutCommands::Unregister();

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

	EBPALWireHandling Mode = EBPALWireHandling::StraightenAndMove;
	FBlueprintAutoLayout Layout(GetLayoutConfig(Mode));
	// "Reroute with knots" makes the plain action bend wires; otherwise the base layout already
	// straightens & moves nodes (or does nothing to wires, when the mode is "Off").
	if (Mode == EBPALWireHandling::RerouteWithKnots)
	{
		Layout.LayoutAndRouteGraph(Graph);
	}
	else
	{
		Layout.LayoutGraph(Graph);
	}

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

	EBPALWireHandling Mode = EBPALWireHandling::StraightenAndMove;
	FBlueprintAutoLayout Layout(GetLayoutConfig(Mode));
	if (Mode == EBPALWireHandling::RerouteWithKnots)
	{
		Layout.LayoutGroupAndRouteGraph(Graph);
	}
	else
	{
		Layout.LayoutAndGroupGraph(Graph);
	}

	Graph->NotifyGraphChanged();
}

void FBlueprintAutoLayoutModule::ExecuteLayoutAndRouteOnGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AutoLayoutRouteGraphTransaction", "Auto Layout (Route Wires)"));
	Graph->Modify();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Node->Modify();
		}
	}

	// Explicit "Route Wires" action: always inserts knots (the fallback path). Straighten state
	// follows the user's setting, so knots only handle what straightening couldn't.
	EBPALWireHandling Mode = EBPALWireHandling::StraightenAndMove;
	FBlueprintAutoLayout Layout(GetLayoutConfig(Mode));
	Layout.LayoutAndRouteGraph(Graph);

	Graph->NotifyGraphChanged();
}

void FBlueprintAutoLayoutModule::ExecuteLayoutGroupAndRouteOnGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AutoLayoutGroupRouteGraphTransaction", "Auto Layout, Group & Route"));
	Graph->Modify();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Node->Modify();
		}
	}

	EBPALWireHandling Mode = EBPALWireHandling::StraightenAndMove;
	FBlueprintAutoLayout Layout(GetLayoutConfig(Mode));
	Layout.LayoutGroupAndRouteGraph(Graph);

	Graph->NotifyGraphChanged();
}

void FBlueprintAutoLayoutModule::ExecuteLayoutSelectedOnGraph(UEdGraph* Graph, const TArray<UEdGraphNode*>& SelectedNodes)
{
	if (!Graph || SelectedNodes.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AutoLayoutSelectedTransaction", "Auto Layout Selected Nodes"));
	Graph->Modify();
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (Node)
		{
			Node->Modify();
		}
	}

	// Anchor the result at the selection's current top-left so it lays out roughly in place
	// rather than teleporting to the graph origin.
	int32 StartX = TNumericLimits<int32>::Max();
	int32 StartY = TNumericLimits<int32>::Max();
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (Node)
		{
			StartX = FMath::Min(StartX, Node->NodePosX);
			StartY = FMath::Min(StartY, Node->NodePosY);
		}
	}
	if (StartX == TNumericLimits<int32>::Max()) { StartX = 0; StartY = 0; }

	// Treat the selected nodes as layout roots and arrange just their subtrees in place.
	EBPALWireHandling Mode = EBPALWireHandling::StraightenAndMove;
	FBlueprintAutoLayout Layout(GetLayoutConfig(Mode));
	Layout.LayoutSubtree(Graph, SelectedNodes, StartX, StartY);

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

			InSection.AddMenuEntry(
				"AutoLayoutAndRouteGraph",
				LOCTEXT("AutoLayoutRouteGraphLabel", "Auto Layout (Route Wires)"),
				LOCTEXT("AutoLayoutRouteGraphTooltip", "Arrange the graph, then insert reroute (knot) nodes so wires bend around nodes instead of cutting across them"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop"),
				FUIAction(FExecuteAction::CreateLambda([Graph]()
				{
					FBlueprintAutoLayoutModule::ExecuteLayoutAndRouteOnGraph(Graph);
				})));

			InSection.AddMenuEntry(
				"AutoLayoutGroupAndRouteGraph",
				LOCTEXT("AutoLayoutGroupRouteGraphLabel", "Auto Layout, Group && Route"),
				LOCTEXT("AutoLayoutGroupRouteGraphTooltip", "Arrange the graph, wrap each subtree in an auto-named comment box, then reroute wires around obstacle nodes"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop"),
				FUIAction(FExecuteAction::CreateLambda([Graph]()
				{
					FBlueprintAutoLayoutModule::ExecuteLayoutGroupAndRouteOnGraph(Graph);
				})));

			// "Layout Selected" — only when nodes are selected in this graph's open editor.
			TArray<UEdGraphNode*> Selected;
			if (TSharedPtr<SGraphEditor> Ed = SGraphEditor::FindGraphEditorForGraph(Graph))
			{
				for (UObject* Obj : Ed->GetSelectedNodes())
				{
					if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
					{
						Selected.Add(Node);
					}
				}
			}
			if (Selected.Num() > 0)
			{
				InSection.AddMenuEntry(
					"AutoLayoutSelected",
					LOCTEXT("AutoLayoutSelectedLabel", "Auto Layout Selected"),
					LOCTEXT("AutoLayoutSelectedTooltip", "Arrange only the currently selected nodes, in place"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop"),
					FUIAction(FExecuteAction::CreateLambda([Graph, Selected]()
					{
						FBlueprintAutoLayoutModule::ExecuteLayoutSelectedOnGraph(Graph, Selected);
					})));
			}
		}));

	// Add the toolbar button to the Blueprint editor's toolbar.
	RegisterToolbarExtension();
}

void FBlueprintAutoLayoutModule::RegisterToolbarExtension()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScope(GAutoLayoutOwnerName);

	// The Blueprint editor's slim toolbar. Name = AssetEditor.<ToolkitFName>.ToolBar; the base
	// Blueprint editor's toolkit name is "BlueprintEditor".
	UToolMenu* Toolbar = ToolMenus->ExtendMenu("AssetEditor.BlueprintEditor.ToolBar");
	if (!Toolbar)
	{
		return;
	}

	FToolMenuSection& Section = Toolbar->FindOrAddSection("BlueprintAutoLayout");
	Section.AddDynamicEntry(
		"AutoLayoutToolbarButton",
		FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UAssetEditorToolkitMenuContext* Context = InSection.FindContext<UAssetEditorToolkitMenuContext>();
			if (!Context || !Context->Toolkit.IsValid())
			{
				return;
			}

			InSection.AddEntry(FToolMenuEntry::InitToolBarButton(
				"AutoLayoutGraph",
				FUIAction(FExecuteAction::CreateLambda([WeakToolkit = Context->Toolkit]()
				{
					TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
					if (!Toolkit.IsValid())
					{
						return;
					}
					// This toolbar belongs to the Blueprint editor, so the toolkit is an FBlueprintEditor.
					TSharedPtr<FBlueprintEditor> BlueprintEditor = StaticCastSharedPtr<FBlueprintEditor>(Toolkit);
					if (UEdGraph* Graph = BlueprintEditor->GetFocusedGraph())
					{
						FBlueprintAutoLayoutModule::ExecuteLayoutOnGraph(Graph);
					}
				})),
				LOCTEXT("ToolbarAutoLayoutLabel", "Auto Layout"),
				LOCTEXT("ToolbarAutoLayoutTooltip", "Automatically arrange the current Blueprint graph (Ctrl/Cmd+Shift+L)"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.AlignNodesTop")));
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
