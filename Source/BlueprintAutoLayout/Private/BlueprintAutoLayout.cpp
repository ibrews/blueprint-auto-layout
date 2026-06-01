// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayout.cpp - Pin-aware Blueprint graph layout algorithm.

#include "BlueprintAutoLayout.h"
#include "BPALLayeredLayout.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "GraphEditor.h"
#include "SGraphPanel.h"
#include "SGraphNode.h"

int32 FBlueprintAutoLayout::LayoutGraph(UEdGraph* Graph, int32 StartX, int32 StartY)
{
	if (!Graph)
	{
		return 0;
	}

	// Clear previous state
	NodeInfoMap.Empty();
	RootNodes.Empty();
	AllExecNodes.Empty();
	AllPureNodes.Empty();
	AllKnots.Empty();
	AllComments.Empty();
	PositionedPureNodes.Empty();
	CommentMembers.Empty();
	PendingLaneRoutes.Empty();
	LiveGraphPanel = nullptr;

	// Drop any long-edge knots a previous run materialized (reconnecting the wires) so layout sees
	// the original topology and knots never accumulate. No-op if the feature was off.
	if (Config.bMaterializeLongEdges)
	{
		RemoveAutoRoutedKnots(Graph);
	}

	// If the graph's editor is open, grab its panel so we can read real node sizes.
	ResolveLiveGraphPanel(Graph);

	// Record which nodes each comment currently wraps, while positions are still original.
	CaptureCommentMembership(Graph);

	// Phase 1: Build the layout tree from exec flow
	BuildLayoutTree(Graph);

	if (RootNodes.Num() == 0)
	{
		return 0;
	}

	// Phases 2-3: assign coordinates. The layered engine handles DAGs (cross-row links, multi-
	// consumer data, long edges) and straightens on pin Y; the tree packer is the fallback.
	bool bLaidOut = false;
	if (Config.bUseLayeredEngine)
	{
		bLaidOut = AssignPositionsLayered(StartX, StartY);
	}
	if (!bLaidOut)
	{
		CalculateSubtreeHeights();          // Phase 2: subtree heights (bottom-up, in pixels)
		AssignPositions(StartX, StartY);    // Phase 3: positions (top-down)
	}

	// Phase 4: Apply to actual nodes
	ApplyPositions();

	// Phase 4.5: Straighten data wires by nudging single-consumer pure providers onto the consumer
	// pin. The tree packer nudges within a bound; the layered engine anchors data chains at their
	// source, so its providers can sit far from what they feed — pull them the full way (unclamped),
	// twice, so a provider-of-a-provider cascades onto the consumer its own consumer just moved to.
	StraightenWires(/*bUnclamped*/ bLaidOut);
	if (bLaidOut)
	{
		StraightenWires(/*bUnclamped*/ true);
		// Straighten the first exec wire off each event: snap the (un-anchored) root to its successor.
		AlignRootEventsToSuccessor();
	}

	// Phase 5: Cosmetic post-passes — reroute nodes become wire bends, comments re-wrap their members
	PositionKnots();
	WrapComments();

	// Phase 6 (layered): turn long-edge lanes into straight knot-routed wires. Runs last so the
	// knots it creates aren't repositioned by PositionKnots.
	if (bLaidOut && Config.bMaterializeLongEdges)
	{
		MaterializeLaneKnots(Graph);
	}

	return NodeInfoMap.Num();
}

int32 FBlueprintAutoLayout::LayoutSubtree(UEdGraph* Graph, const TArray<UEdGraphNode*>& SpecificRoots, int32 StartX, int32 StartY)
{
	if (!Graph || SpecificRoots.Num() == 0)
	{
		return 0;
	}

	NodeInfoMap.Empty();
	RootNodes.Empty();
	AllExecNodes.Empty();
	AllPureNodes.Empty();
	AllKnots.Empty();
	AllComments.Empty();
	PositionedPureNodes.Empty();
	CommentMembers.Empty();
	PendingLaneRoutes.Empty();   // not materialized for selected-only layout, but keep state clean
	LiveGraphPanel = nullptr;

	ResolveLiveGraphPanel(Graph);
	CaptureCommentMembership(Graph);

	BuildLayoutTree(Graph, &SpecificRoots);

	if (RootNodes.Num() == 0)
	{
		return 0;
	}

	bool bLaidOut = false;
	if (Config.bUseLayeredEngine)
	{
		bLaidOut = AssignPositionsLayered(StartX, StartY);
	}
	if (!bLaidOut)
	{
		CalculateSubtreeHeights();
		AssignPositions(StartX, StartY);
	}
	ApplyPositions();
	StraightenWires(/*bUnclamped*/ bLaidOut);
	PositionKnots();
	WrapComments();

	return NodeInfoMap.Num();
}

int32 FBlueprintAutoLayout::LayoutAndGroupGraph(UEdGraph* Graph, int32 StartX, int32 StartY)
{
	// Run the normal layout first. The tree state (RootNodes, NodeInfoMap, ExecChildren,
	// DataProviders) persists afterward, so CreateGroupComments can use it.
	const int32 Count = LayoutGraph(Graph, StartX, StartY);
	if (Count > 0)
	{
		CreateGroupComments(Graph);
	}
	return Count;
}

int32 FBlueprintAutoLayout::LayoutAndRouteGraph(UEdGraph* Graph, int32 StartX, int32 StartY)
{
	if (!Graph)
	{
		return 0;
	}

	// Idempotent re-run: drop any knots a previous routing pass added (and reconnect the
	// wire they were on) so the layout sees the original topology and knots don't accumulate.
	RemoveAutoRoutedKnots(Graph);

	const int32 Count = LayoutGraph(Graph, StartX, StartY);
	if (Count > 0)
	{
		RerouteWiresAroundObstacles(Graph);
	}
	return Count;
}

int32 FBlueprintAutoLayout::LayoutGroupAndRouteGraph(UEdGraph* Graph, int32 StartX, int32 StartY)
{
	if (!Graph)
	{
		return 0;
	}

	RemoveAutoRoutedKnots(Graph);

	const int32 Count = LayoutAndGroupGraph(Graph, StartX, StartY);
	if (Count > 0)
	{
		RerouteWiresAroundObstacles(Graph);
	}
	return Count;
}

//------------------------------------------------------------------------------
// Phase 1: Build Layout Tree
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::BuildLayoutTree(UEdGraph* Graph, const TArray<UEdGraphNode*>* SpecificRoots)
{
	// First pass: Create info for all nodes and classify them
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			FLayoutNodeInfo* Info = GetOrCreateNodeInfo(Node);
			ClassifyNode(Info);

			// Reroute (knot) nodes and comment boxes are not part of the exec/pure
			// layout — they are handled by cosmetic post-passes.
			if (Info->bIsComment)
			{
				AllComments.Add(Info);
			}
			else if (Info->bIsKnot)
			{
				AllKnots.Add(Info);
			}
			else if (Info->bIsPureNode)
			{
				AllPureNodes.Add(Info);
			}
		}
	}

	// Find root nodes
	if (SpecificRoots)
	{
		for (UEdGraphNode* Root : *SpecificRoots)
		{
			if (FLayoutNodeInfo* Info = NodeInfoMap.Find(Root))
			{
				Info->bIsRootNode = true;
				RootNodes.Add(Info);
			}
		}
	}
	else
	{
		// Auto-detect roots: events and function entries
		for (auto& Pair : NodeInfoMap)
		{
			FLayoutNodeInfo& Info = Pair.Value;
			if (Info.Node->IsA<UK2Node_Event>() || Info.Node->IsA<UK2Node_FunctionEntry>())
			{
				Info.bIsRootNode = true;
				RootNodes.Add(&Info);
			}
		}

		// If no event/entry nodes, find nodes with exec output but no exec input
		if (RootNodes.Num() == 0)
		{
			for (auto& Pair : NodeInfoMap)
			{
				FLayoutNodeInfo& Info = Pair.Value;
				if (Info.bIsPureNode || Info.bIsKnot || Info.bIsComment) continue;

				TArray<UEdGraphPin*> ExecInputs = GetExecInputPins(Info.Node);
				TArray<UEdGraphPin*> ExecOutputs = GetExecOutputPins(Info.Node);

				bool bHasConnectedExecInput = false;
				for (UEdGraphPin* Pin : ExecInputs)
				{
					if (Pin->LinkedTo.Num() > 0)
					{
						bHasConnectedExecInput = true;
						break;
					}
				}

				if (!bHasConnectedExecInput && ExecOutputs.Num() > 0)
				{
					Info.bIsRootNode = true;
					RootNodes.Add(&Info);
				}
			}
		}
	}

	// Sort roots by original Y position for consistent ordering
	RootNodes.Sort([](const FLayoutNodeInfo& A, const FLayoutNodeInfo& B) {
		return A.Node->NodePosY < B.Node->NodePosY;
	});

	// Traverse from each root to build tree structure
	for (FLayoutNodeInfo* Root : RootNodes)
	{
		TraverseExecFlow(Root, 0);
	}

	// Collect pure node providers for each exec node
	for (FLayoutNodeInfo* ExecNode : AllExecNodes)
	{
		CollectPureProviders(ExecNode);
	}

	// Collect pure node providers for pure nodes themselves so chained pure nodes
	// (e.g. variable get → math → math → exec) get positioned correctly.
	for (FLayoutNodeInfo* PureNode : AllPureNodes)
	{
		CollectPureProviders(PureNode);
	}
}

FLayoutNodeInfo* FBlueprintAutoLayout::GetOrCreateNodeInfo(UEdGraphNode* Node)
{
	if (FLayoutNodeInfo* Existing = NodeInfoMap.Find(Node))
	{
		return Existing;
	}

	FLayoutNodeInfo& NewInfo = NodeInfoMap.Add(Node);
	NewInfo.Node = Node;
	return &NewInfo;
}

void FBlueprintAutoLayout::ClassifyNode(FLayoutNodeInfo* Info)
{
	if (!Info || !Info->Node) return;

	Info->bIsKnot = IsKnot(Info->Node);
	Info->bIsComment = IsComment(Info->Node);

	// Knots and comments are never treated as exec/pure/branch layout nodes.
	if (Info->bIsKnot || Info->bIsComment)
	{
		Info->bIsPureNode = false;
		Info->bIsBranchNode = false;
	}
	else
	{
		Info->bIsPureNode = IsPureNode(Info->Node);
		Info->bIsBranchNode = IsBranchNode(Info->Node);
		Info->bIsSequenceNode = IsSequenceNode(Info->Node);
	}

	// Calculate actual node dimensions
	CalculateNodeDimensions(Info);
}

void FBlueprintAutoLayout::CalculateNodeDimensions(FLayoutNodeInfo* Info)
{
	if (!Info || !Info->Node) return;

	UEdGraphNode* Node = Info->Node;

	// Try to get actual dimensions from node
	int32 Width = Node->NodeWidth;
	int32 Height = Node->NodeHeight;

	// If node doesn't have valid dimensions, estimate from pins
	if (Width <= 0)
	{
		// Estimate width based on node title length and type
		FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Width = FMath::Max(Config.DefaultNodeWidth, Title.Len() * 8 + 60);

		// Call function nodes tend to be wider
		if (Node->IsA<UK2Node_CallFunction>())
		{
			Width = FMath::Max(Width, 250);
		}
	}

	if (Height <= 0)
	{
		// Estimate height based on pin count
		int32 InputPins = 0;
		int32 OutputPins = 0;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bHidden)
			{
				if (Pin->Direction == EGPD_Input)
					InputPins++;
				else
					OutputPins++;
			}
		}

		int32 MaxPins = FMath::Max(InputPins, OutputPins);
		Height = FMath::Max(Config.DefaultNodeHeight, 40 + MaxPins * Config.PinHeightEstimate);
	}

	// If the editor is open, the rendered widget knows the node's TRUE size. Prefer it (but never
	// shrink below the estimate) so spacing and comment boxes are built from real geometry — this
	// is what prevents node overlap and keeps comment boxes fully around their contents.
	int32 RealWidth = 0, RealHeight = 0;
	if (TryGetRenderedNodeSize(Node, RealWidth, RealHeight))
	{
		Width = FMath::Max(Width, RealWidth);
		Height = FMath::Max(Height, RealHeight);
	}

	Info->NodeWidth = Width;
	Info->NodeHeight = Height;
}

void FBlueprintAutoLayout::ResolveLiveGraphPanel(UEdGraph* Graph)
{
	LiveGraphPanel = nullptr;
	if (!Graph) return;

#if ENGINE_MAJOR_VERSION >= 5
	// GetGraphPanel() was added to SGraphEditor in UE 5.0; in UE 4 live node sizes are not available.
	if (TSharedPtr<SGraphEditor> Editor = SGraphEditor::FindGraphEditorForGraph(Graph))
	{
		LiveGraphPanel = Editor->GetGraphPanel();
	}
#endif
}

bool FBlueprintAutoLayout::TryGetRenderedNodeSize(UEdGraphNode* Node, int32& OutWidth, int32& OutHeight) const
{
	if (!LiveGraphPanel || !Node) return false;

	TSharedPtr<SGraphNode> Widget = LiveGraphPanel->GetNodeWidgetFromGuid(Node->NodeGuid);
	if (!Widget.IsValid()) return false;

	// Desired size is the node's natural (unzoomed) size in graph units — the same space as NodePosX/Y.
	// In UE 5.x GetDesiredSize() returns FVector2f (or a FVector2f-compatible compat type);
	// in UE 4.x it returns FVector2D.
#if ENGINE_MAJOR_VERSION >= 5
	const FVector2f Size = Widget->GetDesiredSize();
#else
	const FVector2D Size = Widget->GetDesiredSize();
#endif
	if (Size.X <= 1.f || Size.Y <= 1.f) return false; // not arranged yet — fall back to the estimate

	OutWidth = FMath::RoundToInt(Size.X);
	OutHeight = FMath::RoundToInt(Size.Y);
	return true;
}

void FBlueprintAutoLayout::TraverseExecFlow(FLayoutNodeInfo* Current, int32 CurrentDepth)
{
	if (!Current || Current->bVisited || Current->bIsPureNode) return;

	Current->bVisited = true;
	Current->Depth = CurrentDepth;
	AllExecNodes.Add(Current);

	// Get exec output pins
	TArray<UEdGraphPin*> ExecOutputs = GetExecOutputPins(Current->Node);

	for (UEdGraphPin* ExecOut : ExecOutputs)
	{
		// Follow this exec output to the real downstream nodes, transparently passing
		// through any reroute (knot) nodes on the wire.
		TArray<UEdGraphNode*> RealChildren;
		TSet<UEdGraphPin*> Visited;
		GatherRealExecTargets(ExecOut, RealChildren, Visited);

		for (UEdGraphNode* ChildNode : RealChildren)
		{
			FLayoutNodeInfo* ChildInfo = GetOrCreateNodeInfo(ChildNode);

			if (!ChildInfo->bVisited && !ChildInfo->bIsPureNode && !ChildInfo->bIsKnot && !ChildInfo->bIsComment)
			{
				ChildInfo->Parent = Current;
				Current->ExecChildren.Add(ChildInfo);
				Current->ChildPinNames.Add(ChildInfo, ExecOut->PinName);

				TraverseExecFlow(ChildInfo, CurrentDepth + 1);
			}
		}
	}
}

void FBlueprintAutoLayout::CollectPureProviders(FLayoutNodeInfo* ExecNode)
{
	if (!ExecNode || !ExecNode->Node) return;

	TSet<UEdGraphNode*> Seen;

	for (UEdGraphPin* Pin : ExecNode->Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && !IsExecPin(Pin))
		{
			// Gather the real pure source nodes feeding this data pin, passing through
			// any reroute (knot) nodes on the wire.
			TArray<UEdGraphNode*> Sources;
			TSet<UEdGraphPin*> Visited;
			GatherRealPureSources(Pin, Seen, Sources, Visited);

			for (UEdGraphNode* SourceNode : Sources)
			{
				if (FLayoutNodeInfo* PureInfo = NodeInfoMap.Find(SourceNode))
				{
					ExecNode->DataProviders.Add(PureInfo);
				}
			}
		}
	}
}

//------------------------------------------------------------------------------
// Phase 2: Calculate Subtree Heights (in pixels)
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::CalculateSubtreeHeights()
{
	// Process nodes from deepest to shallowest (post-order)
	AllExecNodes.Sort([](const FLayoutNodeInfo& A, const FLayoutNodeInfo& B) {
		return A.Depth > B.Depth;
	});

	for (FLayoutNodeInfo* Node : AllExecNodes)
	{
		Node->SubtreeHeight = CalculateHeight(Node);
	}
}

int32 FBlueprintAutoLayout::CalculateHeight(FLayoutNodeInfo* Node)
{
	if (!Node) return Config.DefaultNodeHeight;

	// Use actual node height
	int32 OwnHeight = Node->NodeHeight + Config.NodePaddingY;

	if (Node->ExecChildren.Num() == 0)
	{
		// Leaf node - just its own height
		return OwnHeight;
	}

	if (ShouldStackChildren(Node))
	{
		// Stacked node (branch / sequence): children occupy separate vertical lanes.
		// Total height = sum of all children heights + extra spacing between them
		int32 TotalChildHeight = 0;
		for (int32 i = 0; i < Node->ExecChildren.Num(); i++)
		{
			FLayoutNodeInfo* Child = Node->ExecChildren[i];
			TotalChildHeight += Child->SubtreeHeight;

			// Add extra branch spacing between children (not after last)
			if (i < Node->ExecChildren.Num() - 1)
			{
				TotalChildHeight += Config.BranchExtraPaddingY;
			}
		}
		return FMath::Max(TotalChildHeight, OwnHeight);
	}
	else
	{
		// Sequential node: children are in sequence (same lane)
		// Height = max of (own height, max child subtree height)
		int32 MaxChildHeight = OwnHeight;
		for (FLayoutNodeInfo* Child : Node->ExecChildren)
		{
			MaxChildHeight = FMath::Max(MaxChildHeight, Child->SubtreeHeight);
		}
		return MaxChildHeight;
	}
}

//------------------------------------------------------------------------------
// Phase 3: Assign Positions
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::AssignPositions(int32 StartX, int32 StartY)
{
	int32 CurrentY = StartY;

	for (FLayoutNodeInfo* Root : RootNodes)
	{
		PositionExecSubtree(Root, StartX, CurrentY);

		// Move Y down by this root's subtree height plus extra spacing between roots
		CurrentY += Root->SubtreeHeight + Config.RootExtraPaddingY;
	}
}

void FBlueprintAutoLayout::PositionExecSubtree(FLayoutNodeInfo* Node, int32 X, int32 Y)
{
	if (!Node || Node->bPositioned) return;

	Node->bPositioned = true;
	Node->LayoutX = X;
	Node->LayoutY = Y;

	// Position pure nodes for this exec node
	PositionPureNodesForConsumer(Node);

	// Position children - use actual node width + padding
	int32 ChildX = X + Node->NodeWidth + Config.NodePaddingX;

	if (ShouldStackChildren(Node))
	{
		// Stacked: children go in separate vertical lanes (branch paths, or Sequence outputs).
		// Sort children by pin name (Then before Else; then_0 before then_1 …)
		TArray<FLayoutNodeInfo*> SortedChildren = Node->ExecChildren;
		SortedChildren.Sort([Node](const FLayoutNodeInfo& A, const FLayoutNodeInfo& B) {
			FName PinA = Node->ChildPinNames.FindRef(const_cast<FLayoutNodeInfo*>(&A));
			FName PinB = Node->ChildPinNames.FindRef(const_cast<FLayoutNodeInfo*>(&B));
			if (PinA == TEXT("Then")) return true;
			if (PinB == TEXT("Then")) return false;
			return PinA.LexicalLess(PinB);
		});

		int32 ChildY = Y; // First child at same Y as branch

		for (FLayoutNodeInfo* Child : SortedChildren)
		{
			// Reserve a horizontal lane for the child's pure-node column so it doesn't
			// collide with this node (the child's exec predecessor).
			const int32 PureLane = GetPureColumnWidth(Child);
			PositionExecSubtree(Child, ChildX + PureLane, ChildY);

			// Next child below this one's subtree (use actual subtree height + extra padding)
			ChildY += Child->SubtreeHeight + Config.BranchExtraPaddingY;
		}
	}
	else
	{
		// Sequential: children follow in same lane
		for (FLayoutNodeInfo* Child : Node->ExecChildren)
		{
			// Reserve room for the child's pure-node column ahead of it.
			const int32 PureLane = GetPureColumnWidth(Child);
			PositionExecSubtree(Child, ChildX + PureLane, Y);
			// Subsequent sequential children continue using their actual width
			ChildX += PureLane + Child->NodeWidth + Config.NodePaddingX;
		}
	}
}

int32 FBlueprintAutoLayout::GetPureColumnWidth(FLayoutNodeInfo* Consumer) const
{
	if (!Consumer || Consumer->DataProviders.Num() == 0)
	{
		return 0;
	}

	// Only reserve a lane if at least one provider still needs positioning here
	// (shared pure nodes may already have been placed by an earlier consumer).
	bool bAnyUnpositioned = false;
	for (FLayoutNodeInfo* Pure : Consumer->DataProviders)
	{
		if (Pure && !PositionedPureNodes.Contains(Pure))
		{
			bAnyUnpositioned = true;
			break;
		}
	}
	if (!bAnyUnpositioned)
	{
		return 0;
	}

	// Mirror PositionPureNodesForConsumer's column width exactly (DefaultNodeWidth floor,
	// widened by the widest provider) so the reserved lane matches where pure nodes land.
	int32 MaxPureWidth = Config.DefaultNodeWidth;
	for (FLayoutNodeInfo* Pure : Consumer->DataProviders)
	{
		if (Pure)
		{
			MaxPureWidth = FMath::Max(MaxPureWidth, Pure->NodeWidth);
		}
	}

	return MaxPureWidth + Config.NodePaddingX;
}

void FBlueprintAutoLayout::PositionPureNodesForConsumer(FLayoutNodeInfo* Consumer)
{
	if (!Consumer || Consumer->DataProviders.Num() == 0) return;

	// Position pure nodes in a column to the left of consumer
	// Use the widest pure node's width for column spacing
	int32 MaxPureWidth = Config.DefaultNodeWidth;
	for (FLayoutNodeInfo* Pure : Consumer->DataProviders)
	{
		if (Pure)
		{
			MaxPureWidth = FMath::Max(MaxPureWidth, Pure->NodeWidth);
		}
	}

	// Start positioning to the left of consumer
	int32 CurrentX = Consumer->LayoutX - MaxPureWidth - Config.NodePaddingX;
	int32 CurrentY = Consumer->LayoutY;

	int32 Column = 0;
	int32 RowInColumn = 0;
	int32 ColumnStartY = CurrentY;
	TArray<int32> ColumnTopY;  // cumulative Y per column, using actual node heights
	ColumnTopY.Add(ColumnStartY);

	for (FLayoutNodeInfo* Pure : Consumer->DataProviders)
	{
		if (!Pure) continue;

		// Skip if already positioned by another consumer
		if (PositionedPureNodes.Contains(Pure))
		{
			continue;
		}

		// Mark as positioned
		PositionedPureNodes.Add(Pure);
		Pure->bPositioned = true;

		Pure->LayoutX = CurrentX - (Column * (MaxPureWidth + Config.NodePaddingX));
		Pure->LayoutY = ColumnTopY[Column];
		ColumnTopY[Column] += Pure->NodeHeight + Config.NodePaddingY;

		RowInColumn++;
		if (RowInColumn >= Config.MaxPureNodesPerColumn)
		{
			RowInColumn = 0;
			Column++;
			ColumnTopY.Add(ColumnStartY);
		}

		// Recursively position this pure node's pure inputs (they go further left)
		PositionPureNodesForConsumer(Pure);
	}
}

//------------------------------------------------------------------------------
// Phase 4: Apply Positions
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Phase 3 (layered alternative): build an FLayeredGraph and solve it
//------------------------------------------------------------------------------

int32 FBlueprintAutoLayout::PinYOffset(UEdGraphNode* Node, UEdGraphPin* Pin) const
{
	if (!Node) return 0;
	// EstimatePinY returns an absolute Y (NodePosY + offset); subtract the node top to get the
	// pin's offset within the node, which is what the layered core wants as a port position.
	return FMath::RoundToInt(EstimatePinY(Node, Pin) - (float)Node->NodePosY);
}

void FBlueprintAutoLayout::CollectRealInputPinsFromOutput(UEdGraphPin* OutputPin, TArray<UEdGraphPin*>& OutPins, TSet<UEdGraphPin*>& Visited) const
{
	if (!OutputPin || Visited.Contains(OutputPin)) return;
	Visited.Add(OutputPin);

	for (UEdGraphPin* Linked : OutputPin->LinkedTo)
	{
		UEdGraphNode* Owner = Linked ? Linked->GetOwningNode() : nullptr;
		if (!Owner) continue;

		if (IsKnot(Owner))
		{
			// Reroute node: the wire continues out of the knot's output pin(s). Follow them.
			for (UEdGraphPin* P : Owner->Pins)
			{
				if (P && P->Direction == EGPD_Output)
				{
					CollectRealInputPinsFromOutput(P, OutPins, Visited);
				}
			}
		}
		else if (!IsComment(Owner))
		{
			OutPins.AddUnique(Linked);   // a real consumer input pin
		}
	}
}

bool FBlueprintAutoLayout::AssignPositionsLayered(int32 StartX, int32 StartY)
{
	// Vertices: every real node (exec or pure). Knots/comments are excluded — knots are traced
	// through as wire pass-throughs, comments are wrapped by a later cosmetic pass.
	TArray<FLayoutNodeInfo*> Verts;
	TMap<UEdGraphNode*, int32> Vid;
	for (auto& Pair : NodeInfoMap)
	{
		FLayoutNodeInfo& Info = Pair.Value;
		if (!Info.Node || Info.bIsKnot || Info.bIsComment) continue;
		Vid.Add(Info.Node, Verts.Num());
		Verts.Add(&Info);
	}
	const int32 NumV = Verts.Num();
	if (NumV == 0) return false;

	// Edges: every output pin (exec or data), traced through knots to the real input pins it feeds.
	// SrcPin/DstPin are kept so long edges can later be materialized as knot-routed lanes.
	struct FAdapterEdge { int32 Src; int32 Dst; float SrcPort; float DstPort; bool bExec; UEdGraphPin* SrcPin; UEdGraphPin* DstPin; };
	TArray<FAdapterEdge> EdgeList;
	for (int32 i = 0; i < NumV; ++i)
	{
		UEdGraphNode* Node = Verts[i]->Node;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->Direction != EGPD_Output) continue;
			const bool bExec = IsExecPin(Pin);

			TArray<UEdGraphPin*> RealInputs;
			TSet<UEdGraphPin*> Visited;
			CollectRealInputPinsFromOutput(Pin, RealInputs, Visited);

			const float SrcPort = (float)PinYOffset(Node, Pin);
			for (UEdGraphPin* InPin : RealInputs)
			{
				const int32* DstPtr = Vid.Find(InPin->GetOwningNode());
				if (!DstPtr || *DstPtr == i) continue;
				EdgeList.Add({ i, *DstPtr, SrcPort, (float)PinYOffset(InPin->GetOwningNode(), InPin), bExec, Pin, InPin });
			}
		}
	}

	// Ranking is done by the core's cycle-safe longest-path over ALL edges (so data chains push
	// their consumers right too — e.g. a node fed by a Timeline ranks after it). We only flag pure
	// data SOURCES (no inputs, e.g. a variable Get) so the core pulls them right to sit beside the
	// node they feed instead of resting at column 0.
	TArray<int32> InDeg; InDeg.Init(0, NumV);
	for (const FAdapterEdge& E : EdgeList) InDeg[E.Dst]++;

	bpal::FLayeredConfig Cfg;
	Cfg.RankSpacingX = Config.LayeredRankSpacingX;
	Cfg.NodeSpacingY = Config.LayeredNodeSpacingY;
	bpal::FLayeredGraph G(Cfg);
	for (int32 i = 0; i < NumV; ++i)
	{
		G.AddVertex((float)FMath::Max(Verts[i]->NodeWidth, 1), (float)FMath::Max(Verts[i]->NodeHeight, 1));
	}
	for (int32 i = 0; i < NumV; ++i)
	{
		if (Verts[i]->bIsPureNode && InDeg[i] == 0) G.SetPullTowardConsumers(i);
	}
	for (const FAdapterEdge& E : EdgeList) G.AddEdge(E.Src, E.Dst, E.SrcPort, E.DstPort, E.bExec);
	G.Solve();

	// Write coordinates back, normalizing so the top-left real node sits at (StartX, StartY).
	const std::vector<bpal::FLayeredVertex>& Sol = G.Vertices();
	float MinX = TNumericLimits<float>::Max(), MinY = TNumericLimits<float>::Max();
	for (int32 i = 0; i < NumV; ++i) { MinX = FMath::Min(MinX, Sol[i].X); MinY = FMath::Min(MinY, Sol[i].Y); }
	if (MinX == TNumericLimits<float>::Max()) { MinX = 0.f; MinY = 0.f; }
	for (int32 i = 0; i < NumV; ++i)
	{
		Verts[i]->LayoutX = StartX + FMath::RoundToInt(Sol[i].X - MinX);
		Verts[i]->LayoutY = StartY + FMath::RoundToInt(Sol[i].Y - MinY);
		Verts[i]->bPositioned = true;
	}

	// Capture long-edge lanes for knot materialization: any DIRECT real→real wire that got dummy
	// waypoints (spans >1 column). The dummy positions (same coordinate space as the real nodes)
	// become the lane the wire will be knot-routed through, so it renders straight instead of as one
	// curved spline. Edges that pass through pre-existing (user) knots aren't direct, so are skipped.
	if (Config.bMaterializeLongEdges)
	{
		for (int32 i = 0; i < EdgeList.Num(); ++i)
		{
			const std::vector<int>& Chain = G.GetEdgeChain(i);
			if (Chain.empty()) continue;
			const FAdapterEdge& E = EdgeList[i];
			if (!E.SrcPin || !E.DstPin || !E.SrcPin->LinkedTo.Contains(E.DstPin)) continue; // not a direct wire

			FLaneRoute Route;
			Route.SrcPin = E.SrcPin;
			Route.DstPin = E.DstPin;
			for (int Dummy : Chain)
			{
				Route.Waypoints.Add(FIntPoint(
					StartX + FMath::RoundToInt(Sol[Dummy].X - MinX),
					StartY + FMath::RoundToInt(Sol[Dummy].Y - MinY)));
			}
			PendingLaneRoutes.Add(MoveTemp(Route));
		}
	}
	return true;
}

void FBlueprintAutoLayout::ApplyPositions()
{
	for (auto& Pair : NodeInfoMap)
	{
		FLayoutNodeInfo& Info = Pair.Value;
		if (Info.Node && Info.bPositioned)
		{
			Info.Node->NodePosX = Info.LayoutX;
			Info.Node->NodePosY = Info.LayoutY;
		}
	}
}

//------------------------------------------------------------------------------
// Phase 4.5: Straighten data wires by moving the provider nodes
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::StraightenWires(bool bUnclamped)
{
	if (!Config.bStraightenWires)
	{
		return;
	}
	if (!bUnclamped && Config.MaxStraightenNudgeY <= 0)
	{
		return;
	}
	// Layered engine pulls providers the full distance to their consumer; the tree packer keeps the
	// nudge bounded so providers stay near their flow position.
	const float NudgeLimit = bUnclamped ? 1.0e6f : (float)Config.MaxStraightenNudgeY;

	// One proposed vertical move: shift a pure-node leaf so its single output pin lines up with
	// the consumer input pin it feeds, making that data wire a straight horizontal line.
	struct FAlign
	{
		FLayoutNodeInfo* Pure = nullptr;
		int32 NewY = 0;     // proposed top Y after alignment
		int32 Height = 0;
		int32 X = 0;        // column (NodePosX) — used to de-overlap providers sharing a column
	};
	TArray<FAlign> Aligns;

	for (auto& Pair : NodeInfoMap)
	{
		FLayoutNodeInfo& Info = Pair.Value;
		UEdGraphNode* Node = Info.Node;
		if (!Node || !Info.bPositioned || !Info.bIsPureNode)
		{
			continue;
		}

		// Tree packer only straightens "leaf" providers (a variable Get, a literal) so a move can't
		// cascade up a chain. The layered engine (unclamped) also straightens single-consumer chain
		// nodes (e.g. a `float * float` feeding one node) — its providers' wires bend instead, and a
		// second pass pulls those providers onto the now-moved node.
		if (!bUnclamped && Info.DataProviders.Num() > 0)
		{
			continue;
		}

		// Require exactly one data output pin with exactly one link to a real consumer input pin.
		// Multi-output or multi-consumer providers can't be straightened to a single wire, so skip.
		UEdGraphPin* OutPin = nullptr;
		bool bAmbiguous = false;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && !P->bHidden && P->Direction == EGPD_Output && !IsExecPin(P))
			{
				if (OutPin) { bAmbiguous = true; break; }
				OutPin = P;
			}
		}
		if (bAmbiguous || !OutPin || OutPin->LinkedTo.Num() != 1)
		{
			continue;
		}

		UEdGraphPin* ConsumerPin = OutPin->LinkedTo[0];
		if (!ConsumerPin)
		{
			continue;
		}
		UEdGraphNode* Consumer = ConsumerPin->GetOwningNode();
		if (!Consumer || IsKnot(Consumer) || IsComment(Consumer))
		{
			continue;
		}

		// Align the provider's output pin Y to the consumer's input pin Y, clamped to the tolerance.
		const float OutPinY = EstimatePinY(Node, OutPin);
		const float InPinY = EstimatePinY(Consumer, ConsumerPin);
		const float NodeTop = (float)Node->NodePosY;
		const float ProviderHeight = (float)FMath::Max(Info.NodeHeight, Config.DefaultNodeHeight);

		float NewTop = NodeTop + FMath::Clamp(InPinY - OutPinY, -NudgeLimit, NudgeLimit);

		// Crucial: never align the provider INTO the consumer's incoming exec corridor. The white
		// exec wire runs horizontally into the consumer's exec-input pin, straight across this
		// provider's column. If the provider would straddle that line, drop it below — the exec
		// wire stays straight and the data wire bends up to reach the (now lower) provider.
		for (UEdGraphPin* CP : Consumer->Pins)
		{
			if (CP && !CP->bHidden && CP->Direction == EGPD_Input && IsExecPin(CP))
			{
				const float ExecY = EstimatePinY(Consumer, CP);
				const float MinTop = ExecY + (float)Config.ExecCorridorClearance;
				if (NewTop < MinTop && NewTop + ProviderHeight > ExecY)
				{
					NewTop = MinTop;
				}
				break; // first exec input is enough
			}
		}

		if (FMath::Abs(NewTop - NodeTop) < 1.f)
		{
			continue;
		}

		FAlign A;
		A.Pure = &Info;
		A.NewY = FMath::RoundToInt(NewTop);
		A.Height = FMath::RoundToInt(ProviderHeight);
		A.X = Node->NodePosX;
		Aligns.Add(A);
	}

	if (Aligns.Num() == 0)
	{
		return;
	}

	// De-overlap within each column (same X): sort by proposed Y and push each down to clear the
	// previous one. Wires that don't collide stay perfectly straight; where two providers in one
	// column would overlap, the lower one degrades to a tidy stack instead.
	Aligns.Sort([](const FAlign& A, const FAlign& B)
	{
		if (A.X != B.X) return A.X < B.X;
		return A.NewY < B.NewY;
	});

	int32 i = 0;
	while (i < Aligns.Num())
	{
		const int32 ColumnX = Aligns[i].X;
		int32 PrevBottom = TNumericLimits<int32>::Lowest();
		int32 j = i;
		while (j < Aligns.Num() && Aligns[j].X == ColumnX)
		{
			int32 Y = FMath::Max(Aligns[j].NewY, PrevBottom);
			Aligns[j].Pure->Node->NodePosY = Y;
			Aligns[j].Pure->LayoutY = Y;
			PrevBottom = Y + Aligns[j].Height + Config.PureNodePaddingY;
			++j;
		}
		i = j;
	}
}

void FBlueprintAutoLayout::AlignRootEventsToSuccessor()
{
	for (FLayoutNodeInfo* Root : RootNodes)
	{
		if (!Root || !Root->Node || !Root->bPositioned) continue;
		UEdGraphNode* Node = Root->Node;

		// The single exec-output pin (events/entries have one).
		UEdGraphPin* OutExec = nullptr;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && !P->bHidden && P->Direction == EGPD_Output && IsExecPin(P)) { OutExec = P; break; }
		}
		if (!OutExec) continue;

		// The first real exec successor it triggers (tracing through any reroute knots).
		TArray<UEdGraphPin*> RealInputs;
		TSet<UEdGraphPin*> Visited;
		CollectRealInputPinsFromOutput(OutExec, RealInputs, Visited);
		UEdGraphPin* SuccPin = nullptr;
		for (UEdGraphPin* P : RealInputs)
		{
			if (P && IsExecPin(P) && P->GetOwningNode()) { SuccPin = P; break; }
		}
		if (!SuccPin) continue;
		UEdGraphNode* Succ = SuccPin->GetOwningNode();

		// Move the event so its exec-out pin Y matches the successor's exec-in pin Y.
		const float Delta = EstimatePinY(Succ, SuccPin) - EstimatePinY(Node, OutExec);
		if (FMath::Abs(Delta) < 1.f) continue;
		int32 NewTop = Node->NodePosY + FMath::RoundToInt(Delta);

		// Clamp so the root stays between its immediate neighbors in the same column (no overlap).
		const int32 X = Node->NodePosX;
		const int32 H = FMath::Max(Root->NodeHeight, Config.DefaultNodeHeight);
		const int32 CurTop = Node->NodePosY;
		const int32 CurBot = CurTop + H;
		int32 LoBound = TNumericLimits<int32>::Lowest();
		int32 HiBound = TNumericLimits<int32>::Max();
		for (auto& Pair : NodeInfoMap)
		{
			FLayoutNodeInfo& Other = Pair.Value;
			if (Other.Node == Node || !Other.bPositioned || Other.bIsComment || !Other.Node) continue;
			if (Other.Node->NodePosX != X) continue;                 // same column only
			const int32 OTop = Other.Node->NodePosY;
			const int32 OBot = OTop + FMath::Max(Other.NodeHeight, Config.DefaultNodeHeight);
			if (OBot <= CurTop)      LoBound = FMath::Max(LoBound, OBot + Config.NodePaddingY);        // a node above
			else if (OTop >= CurBot) HiBound = FMath::Min(HiBound, OTop - H - Config.NodePaddingY);    // a node below
		}
		if (LoBound != TNumericLimits<int32>::Lowest()) NewTop = FMath::Max(NewTop, LoBound);
		if (HiBound != TNumericLimits<int32>::Max())    NewTop = FMath::Min(NewTop, HiBound);

		Node->NodePosY = NewTop;
		Root->LayoutY = NewTop;
	}
}

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

bool FBlueprintAutoLayout::IsExecPin(UEdGraphPin* Pin) const
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

bool FBlueprintAutoLayout::IsPureNode(UEdGraphNode* Node) const
{
	if (!Node) return false;

	// Check if node has any exec pins
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (IsExecPin(Pin))
		{
			return false;
		}
	}

	return true;
}

bool FBlueprintAutoLayout::IsKnot(UEdGraphNode* Node) const
{
	return Node && Node->IsA<UK2Node_Knot>();
}

bool FBlueprintAutoLayout::IsComment(UEdGraphNode* Node) const
{
	return Node && Node->IsA<UEdGraphNode_Comment>();
}

bool FBlueprintAutoLayout::IsBranchNode(UEdGraphNode* Node) const
{
	if (!Node) return false;

	// UK2Node_IfThenElse is definitely a branch
	if (Node->IsA<UK2Node_IfThenElse>())
	{
		return true;
	}

	// Sequence nodes have multiple outputs but are different - not branches
	if (Node->IsA<UK2Node_ExecutionSequence>())
	{
		return false; // Treat sequence as sequential, not branching
	}

	// Check for multiple exec outputs (switch, etc.)
	TArray<UEdGraphPin*> ExecOutputs = GetExecOutputPins(Node);
	return ExecOutputs.Num() > 1;
}

bool FBlueprintAutoLayout::IsSequenceNode(UEdGraphNode* Node) const
{
	return Node && Node->IsA<UK2Node_ExecutionSequence>();
}

bool FBlueprintAutoLayout::ShouldStackChildren(const FLayoutNodeInfo* Node) const
{
	if (!Node || Node->ExecChildren.Num() < 2)
	{
		return false;
	}

	// Branches (if/switch) always stack their parallel paths vertically. Sequence nodes run
	// their outputs in order, but stacking each Then output into its own lane is what keeps a
	// wire to a later output from crossing an earlier output's subtree — the skip-edge fix.
	if (Node->bIsBranchNode)
	{
		return true;
	}
	return Node->bIsSequenceNode && Config.bStackSequenceOutputs;
}

TArray<UEdGraphPin*> FBlueprintAutoLayout::GetExecOutputPins(UEdGraphNode* Node) const
{
	TArray<UEdGraphPin*> Result;
	if (!Node) return Result;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && IsExecPin(Pin))
		{
			Result.Add(Pin);
		}
	}

	// Sort for consistent ordering
	Result.Sort([](const UEdGraphPin& A, const UEdGraphPin& B) {
		if (A.PinName == TEXT("Then")) return true;
		if (B.PinName == TEXT("Then")) return false;
		if (A.PinName == TEXT("execute")) return true;
		if (B.PinName == TEXT("execute")) return false;
		return A.PinName.LexicalLess(B.PinName);
	});

	return Result;
}

TArray<UEdGraphPin*> FBlueprintAutoLayout::GetExecInputPins(UEdGraphNode* Node) const
{
	TArray<UEdGraphPin*> Result;
	if (!Node) return Result;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && IsExecPin(Pin))
		{
			Result.Add(Pin);
		}
	}

	return Result;
}

TArray<UEdGraphNode*> FBlueprintAutoLayout::GetPureInputNodes(UEdGraphNode* Node) const
{
	TArray<UEdGraphNode*> Result;
	TSet<UEdGraphNode*> Seen;

	if (!Node) return Result;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && !IsExecPin(Pin))
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin)
				{
					UEdGraphNode* SourceNode = LinkedPin->GetOwningNode();
					if (SourceNode && IsPureNode(SourceNode) && !Seen.Contains(SourceNode))
					{
						Seen.Add(SourceNode);
						Result.Add(SourceNode);
					}
				}
			}
		}
	}

	return Result;
}

//------------------------------------------------------------------------------
// Reroute (knot) link tracing
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::GatherRealExecTargets(UEdGraphPin* OutputExecPin, TArray<UEdGraphNode*>& OutTargets, TSet<UEdGraphPin*>& Visited) const
{
	if (!OutputExecPin || Visited.Contains(OutputExecPin)) return;
	Visited.Add(OutputExecPin);

	for (UEdGraphPin* LinkedPin : OutputExecPin->LinkedTo)
	{
		if (!LinkedPin) continue;

		UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		if (!LinkedNode) continue;

		if (IsKnot(LinkedNode))
		{
			// Pass straight through the reroute node to whatever its output feeds.
			if (UK2Node_Knot* Knot = Cast<UK2Node_Knot>(LinkedNode))
			{
				GatherRealExecTargets(Knot->GetOutputPin(), OutTargets, Visited);
			}
		}
		else
		{
			OutTargets.AddUnique(LinkedNode);
		}
	}
}

void FBlueprintAutoLayout::GatherRealPureSources(UEdGraphPin* InputDataPin, TSet<UEdGraphNode*>& Seen, TArray<UEdGraphNode*>& OutSources, TSet<UEdGraphPin*>& Visited) const
{
	if (!InputDataPin || Visited.Contains(InputDataPin)) return;
	Visited.Add(InputDataPin);

	for (UEdGraphPin* LinkedPin : InputDataPin->LinkedTo)
	{
		if (!LinkedPin) continue;

		UEdGraphNode* SourceNode = LinkedPin->GetOwningNode();
		if (!SourceNode) continue;

		if (IsKnot(SourceNode))
		{
			// Trace back through the reroute node to the real data source.
			if (UK2Node_Knot* Knot = Cast<UK2Node_Knot>(SourceNode))
			{
				GatherRealPureSources(Knot->GetInputPin(), Seen, OutSources, Visited);
			}
		}
		else if (IsPureNode(SourceNode) && !Seen.Contains(SourceNode))
		{
			Seen.Add(SourceNode);
			OutSources.Add(SourceNode);
		}
	}
}

//------------------------------------------------------------------------------
// Phase 5: Reroute nodes and comment boxes (cosmetic post-passes)
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::CaptureCommentMembership(UEdGraph* Graph)
{
	if (!Graph) return;

	// While node positions are still original, record which nodes each comment box
	// geometrically contains. After layout moves those nodes, the comment is resized
	// to wrap them in their new positions (see WrapComments).
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!IsComment(Node)) continue;

		const float CommentLeft   = Node->NodePosX;
		const float CommentTop    = Node->NodePosY;
		const float CommentRight  = CommentLeft + FMath::Max(Node->NodeWidth, 1);
		const float CommentBottom = CommentTop + FMath::Max(Node->NodeHeight, 1);

		TArray<UEdGraphNode*> Members;
		for (UEdGraphNode* Other : Graph->Nodes)
		{
			if (!Other || Other == Node || IsComment(Other)) continue;

			// Membership test: the node's top-left corner falls inside the comment rect.
			const float X = Other->NodePosX;
			const float Y = Other->NodePosY;
			if (X >= CommentLeft && X <= CommentRight && Y >= CommentTop && Y <= CommentBottom)
			{
				Members.Add(Other);
			}
		}

		if (Members.Num() > 0)
		{
			CommentMembers.Add(Node, MoveTemp(Members));
		}
	}
}

void FBlueprintAutoLayout::PositionKnots()
{
	if (AllKnots.Num() == 0) return;

	// Place each reroute node at the midpoint of the wire it sits on, so it reads as a
	// bend rather than a free-floating node. Two passes let chained knots settle.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (FLayoutNodeInfo* KnotInfo : AllKnots)
		{
			if (!KnotInfo || !KnotInfo->Node) continue;
			UK2Node_Knot* Knot = Cast<UK2Node_Knot>(KnotInfo->Node);
			if (!Knot) continue;

			UEdGraphPin* InPin = Knot->GetInputPin();
			UEdGraphPin* OutPin = Knot->GetOutputPin();

			bool bHaveSrc = false, bHaveDst = false;
			float SrcX = 0.f, SrcY = 0.f, DstX = 0.f, DstY = 0.f;

			if (InPin && InPin->LinkedTo.Num() > 0 && InPin->LinkedTo[0])
			{
				UEdGraphPin* SrcPin = InPin->LinkedTo[0];
				if (UEdGraphNode* S = SrcPin->GetOwningNode())
				{
					// Right edge of the upstream node, at the actual pin's Y (not node centre),
					// so a knot on an already-straight wire stays on that straight line.
					SrcX = S->NodePosX + FMath::Max(S->NodeWidth, 0);
					SrcY = EstimatePinY(S, SrcPin);
					bHaveSrc = true;
				}
			}
			if (OutPin && OutPin->LinkedTo.Num() > 0 && OutPin->LinkedTo[0])
			{
				UEdGraphPin* DstPin = OutPin->LinkedTo[0];
				if (UEdGraphNode* D = DstPin->GetOwningNode())
				{
					// Left edge of the downstream node, at the actual pin's Y.
					DstX = D->NodePosX;
					DstY = EstimatePinY(D, DstPin);
					bHaveDst = true;
				}
			}

			if (bHaveSrc && bHaveDst)
			{
				Knot->NodePosX = FMath::RoundToInt((SrcX + DstX) * 0.5f);
				Knot->NodePosY = FMath::RoundToInt((SrcY + DstY) * 0.5f);
			}
			else if (bHaveSrc)
			{
				Knot->NodePosX = FMath::RoundToInt(SrcX);
				Knot->NodePosY = FMath::RoundToInt(SrcY);
			}
			else if (bHaveDst)
			{
				Knot->NodePosX = FMath::RoundToInt(DstX);
				Knot->NodePosY = FMath::RoundToInt(DstY);
			}
		}
	}
}

void FBlueprintAutoLayout::WrapComments()
{
	for (FLayoutNodeInfo* CommentInfo : AllComments)
	{
		if (!CommentInfo || !CommentInfo->Node) continue;
		UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(CommentInfo->Node);
		if (!Comment) continue;

		const TArray<UEdGraphNode*>* Members = CommentMembers.Find(Comment);
		if (!Members || Members->Num() == 0)
		{
			// Nothing recorded under this comment — leave it untouched.
			continue;
		}

		// Bounding box of the members in their post-layout positions, using the
		// dimensions the layout computed for each node.
		float MinX = TNumericLimits<float>::Max();
		float MinY = TNumericLimits<float>::Max();
		float MaxX = TNumericLimits<float>::Lowest();
		float MaxY = TNumericLimits<float>::Lowest();
		bool bAny = false;

		for (UEdGraphNode* Member : *Members)
		{
			if (!Member) continue;

			int32 W = Member->NodeWidth;
			int32 H = Member->NodeHeight;
			if (const FLayoutNodeInfo* Info = NodeInfoMap.Find(Member))
			{
				W = FMath::Max(W, Info->NodeWidth);
				H = FMath::Max(H, Info->NodeHeight);
			}
			if (W <= 0) W = Config.DefaultNodeWidth;
			if (H <= 0) H = Config.DefaultNodeHeight;

			MinX = FMath::Min(MinX, (float)Member->NodePosX);
			MinY = FMath::Min(MinY, (float)Member->NodePosY);
			MaxX = FMath::Max(MaxX, (float)(Member->NodePosX + W));
			MaxY = FMath::Max(MaxY, (float)(Member->NodePosY + H));
			bAny = true;
		}

		if (!bAny) continue;

		const int32 Pad = Config.CommentPadding;
		const int32 Title = Config.CommentTitleHeight;

		Comment->NodePosX = FMath::RoundToInt(MinX) - Pad;
		Comment->NodePosY = FMath::RoundToInt(MinY) - Pad - Title;

		const int32 NewWidth  = FMath::RoundToInt(MaxX - MinX) + Pad * 2;
		const int32 NewHeight = FMath::RoundToInt(MaxY - MinY) + Pad * 2 + Title;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		Comment->ResizeNode(FVector2f((float)NewWidth, (float)NewHeight));
#else
		Comment->ResizeNode(FVector2D(NewWidth, NewHeight));
#endif
	}
}

//------------------------------------------------------------------------------
// Auto-grouping (opt-in): one comment box per root subtree
//------------------------------------------------------------------------------

void FBlueprintAutoLayout::CollectSubtreeMembers(FLayoutNodeInfo* Node, TArray<UEdGraphNode*>& OutMembers, TSet<FLayoutNodeInfo*>& Visited) const
{
	if (!Node || Visited.Contains(Node)) return;
	Visited.Add(Node);

	if (Node->Node)
	{
		OutMembers.AddUnique(Node->Node);
	}

	// Pure providers feeding this node (and their own pure inputs) belong to the group too.
	for (FLayoutNodeInfo* Pure : Node->DataProviders)
	{
		if (Pure)
		{
			CollectSubtreeMembers(Pure, OutMembers, Visited);
		}
	}

	// Walk the execution subtree.
	for (FLayoutNodeInfo* Child : Node->ExecChildren)
	{
		CollectSubtreeMembers(Child, OutMembers, Visited);
	}
}

void FBlueprintAutoLayout::CreateGroupComments(UEdGraph* Graph)
{
	if (!Graph) return;

	// A single root means one comment around the whole graph — not useful. Skip.
	if (RootNodes.Num() < 2) return;

	// ColorIndex feeds the cycling-palette mode; keyword mode ignores it (uses the title).
	int32 ColorIndex = 0;

	const int32 Pad = Config.CommentPadding;
	const int32 Title = Config.CommentTitleHeight;

	for (FLayoutNodeInfo* Root : RootNodes)
	{
		if (!Root || !Root->Node) continue;

		TArray<UEdGraphNode*> Members;
		TSet<FLayoutNodeInfo*> Visited;
		CollectSubtreeMembers(Root, Members, Visited);

		// Skip a lone root with no downstream — a box around one node adds noise.
		if (Members.Num() < 2) continue;

		float MinX = TNumericLimits<float>::Max();
		float MinY = TNumericLimits<float>::Max();
		float MaxX = TNumericLimits<float>::Lowest();
		float MaxY = TNumericLimits<float>::Lowest();

		for (UEdGraphNode* Member : Members)
		{
			if (!Member) continue;
			int32 W = Member->NodeWidth;
			int32 H = Member->NodeHeight;
			if (const FLayoutNodeInfo* Info = NodeInfoMap.Find(Member))
			{
				W = FMath::Max(W, Info->NodeWidth);
				H = FMath::Max(H, Info->NodeHeight);
			}
			if (W <= 0) W = Config.DefaultNodeWidth;
			if (H <= 0) H = Config.DefaultNodeHeight;

			MinX = FMath::Min(MinX, (float)Member->NodePosX);
			MinY = FMath::Min(MinY, (float)Member->NodePosY);
			MaxX = FMath::Max(MaxX, (float)(Member->NodePosX + W));
			MaxY = FMath::Max(MaxY, (float)(Member->NodePosY + H));
		}

		UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph);
		if (!Comment) continue;
		Graph->AddNode(Comment, /*bFromUI*/ false, /*bSelectNewNode*/ false);
		Comment->CreateNewGuid();

		Comment->NodePosX = FMath::RoundToInt(MinX) - Pad;
		Comment->NodePosY = FMath::RoundToInt(MinY) - Pad - Title;
		const FString RootTitle = Root->Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		Comment->NodeComment = RootTitle;
		Comment->CommentColor = ChooseCommentColor(RootTitle, ColorIndex);
		Comment->bCommentBubbleVisible = false;

		const int32 NewWidth  = FMath::RoundToInt(MaxX - MinX) + Pad * 2;
		const int32 NewHeight = FMath::RoundToInt(MaxY - MinY) + Pad * 2 + Title;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		Comment->ResizeNode(FVector2f((float)NewWidth, (float)NewHeight));
#else
		Comment->ResizeNode(FVector2D(NewWidth, NewHeight));
#endif

		++ColorIndex;
	}
}

FLinearColor FBlueprintAutoLayout::KeywordColorForTitle(const FString& Title)
{
	const FString T = Title.ToLower();
	auto Has = [&T](const TCHAR* Sub) { return T.Contains(Sub); };

	// Semantic buckets, ordered so the less-ambiguous meaning wins (e.g. "destroy" before "damage").
	if (Has(TEXT("destroy")) || Has(TEXT("death")) || Has(TEXT("die")) || Has(TEXT("dead")) || Has(TEXT("kill")) || Has(TEXT("remove")))
		return FLinearColor(0.40f, 0.16f, 0.18f, 1.0f);   // dark red — teardown
	if (Has(TEXT("damage")) || Has(TEXT("hit")) || Has(TEXT("hurt")) || Has(TEXT("health")) || Has(TEXT("attack")))
		return FLinearColor(0.55f, 0.20f, 0.22f, 1.0f);   // red — combat
	if (Has(TEXT("spawn")) || Has(TEXT("create")) || Has(TEXT("add")) || Has(TEXT("build")) || Has(TEXT("construct")))
		return FLinearColor(0.22f, 0.46f, 0.30f, 1.0f);   // green — creation
	if (Has(TEXT("begin")) || Has(TEXT("init")) || Has(TEXT("start")) || Has(TEXT("setup")) || Has(TEXT("ready")) || Has(TEXT("load")))
		return FLinearColor(0.18f, 0.32f, 0.55f, 1.0f);   // blue — setup
	if (Has(TEXT("tick")) || Has(TEXT("update")) || Has(TEXT("frame")) || Has(TEXT("loop")))
		return FLinearColor(0.16f, 0.42f, 0.45f, 1.0f);   // teal — per-frame
	if (Has(TEXT("input")) || Has(TEXT("press")) || Has(TEXT("release")) || Has(TEXT("key")) || Has(TEXT("axis")) ||
		Has(TEXT("move")) || Has(TEXT("look")) || Has(TEXT("jump")) || Has(TEXT("click")) || Has(TEXT("touch")))
		return FLinearColor(0.55f, 0.36f, 0.16f, 1.0f);   // amber — input
	if (Has(TEXT("overlap")) || Has(TEXT("collision")) || Has(TEXT("trigger")) || Has(TEXT("hit ")) || Has(TEXT("contact")))
		return FLinearColor(0.42f, 0.26f, 0.48f, 1.0f);   // violet — physics/overlap
	if (Has(TEXT("sound")) || Has(TEXT("audio")) || Has(TEXT("play")) || Has(TEXT("music")) || Has(TEXT("sfx")))
		return FLinearColor(0.30f, 0.30f, 0.52f, 1.0f);   // indigo — audio

	// No keyword matched — derive a stable color from the title hash (same name → same color).
	static const FLinearColor Fallback[] = {
		FLinearColor(0.30f, 0.34f, 0.40f, 1.0f),
		FLinearColor(0.38f, 0.34f, 0.26f, 1.0f),
		FLinearColor(0.28f, 0.38f, 0.34f, 1.0f),
		FLinearColor(0.36f, 0.28f, 0.40f, 1.0f),
		FLinearColor(0.40f, 0.32f, 0.30f, 1.0f),
		FLinearColor(0.26f, 0.36f, 0.42f, 1.0f),
	};
	const uint32 H = GetTypeHash(Title);
	return Fallback[H % UE_ARRAY_COUNT(Fallback)];
}

FLinearColor FBlueprintAutoLayout::ChooseCommentColor(const FString& RootTitle, int32 Index) const
{
	if (Config.CommentColorMode == ECommentColorMode::CyclingPalette)
	{
		// A small palette so adjacent group comments read as distinct.
		static const FLinearColor Palette[] = {
			FLinearColor(0.18f, 0.32f, 0.55f, 1.0f), // blue
			FLinearColor(0.55f, 0.34f, 0.18f, 1.0f), // amber
			FLinearColor(0.24f, 0.45f, 0.30f, 1.0f), // green
			FLinearColor(0.42f, 0.26f, 0.48f, 1.0f), // violet
			FLinearColor(0.48f, 0.28f, 0.30f, 1.0f), // rose
		};
		return Palette[Index % UE_ARRAY_COUNT(Palette)];
	}
	return KeywordColorForTitle(RootTitle);
}

//------------------------------------------------------------------------------
// Phase 6: Smart wire rerouting (opt-in) — bend wires around intervening nodes
//------------------------------------------------------------------------------

// Marker stored in NodeComment on knots this pass creates. Knots don't show a comment
// bubble by default, so it's an invisible tag that lets a re-run clean up its own work.
static const TCHAR* GBPALAutoRouteTag = TEXT("BPAL_AutoRoute");

bool FBlueprintAutoLayout::SegmentIntersectsRect(float X0, float Y0, float X1, float Y1, float L, float T, float R, float B)
{
	// Liang–Barsky clip of the segment against the rect; true if any part lies inside.
	const float DX = X1 - X0;
	const float DY = Y1 - Y0;
	const float P[4] = { -DX, DX, -DY, DY };
	const float Q[4] = { X0 - L, R - X0, Y0 - T, B - Y0 };
	float U0 = 0.f, U1 = 1.f;

	for (int32 i = 0; i < 4; ++i)
	{
		if (FMath::IsNearlyZero(P[i]))
		{
			// Segment parallel to this edge — reject only if it starts outside the slab.
			if (Q[i] < 0.f) return false;
		}
		else
		{
			const float Tval = Q[i] / P[i];
			if (P[i] < 0.f)
			{
				if (Tval > U1) return false;
				if (Tval > U0) U0 = Tval;
			}
			else
			{
				if (Tval < U0) return false;
				if (Tval < U1) U1 = Tval;
			}
		}
	}
	return U0 <= U1;
}

bool FBlueprintAutoLayout::GetNodeRect(UEdGraphNode* Node, float& L, float& T, float& R, float& B) const
{
	if (!Node) return false;

	int32 W = Node->NodeWidth;
	int32 H = Node->NodeHeight;
	if (const FLayoutNodeInfo* Info = NodeInfoMap.Find(Node))
	{
		W = FMath::Max(W, Info->NodeWidth);
		H = FMath::Max(H, Info->NodeHeight);
	}
	if (W <= 0) W = Config.DefaultNodeWidth;
	if (H <= 0) H = Config.DefaultNodeHeight;

	L = (float)Node->NodePosX;
	T = (float)Node->NodePosY;
	R = L + (float)W;
	B = T + (float)H;
	return true;
}

float FBlueprintAutoLayout::EstimatePinY(UEdGraphNode* Node, UEdGraphPin* Pin) const
{
	if (!Node) return 0.f;

	const float Top = (float)Node->NodePosY;
	if (!Pin) return Top + 24.f;

	// Index of this pin among the visible pins of the same direction (top-to-bottom).
	int32 Index = 0;
	int32 Found = -1;
	for (UEdGraphPin* P : Node->Pins)
	{
		if (!P || P->bHidden || P->Direction != Pin->Direction) continue;
		if (P == Pin) { Found = Index; break; }
		++Index;
	}
	if (Found < 0) Found = 0;

	const float Header = 28.f;                                  // header/title band above the first pin row
	const float Row = (float)FMath::Max(Config.PinHeightEstimate, 1);
	float Y = Top + Header + Found * Row + Row * 0.5f;

	// Clamp within the node's vertical extent so the estimate never escapes the rect.
	float L, RT, RR, RB;
	if (GetNodeRect(Node, L, RT, RR, RB))
	{
		Y = FMath::Clamp(Y, RT + 4.f, RB - 4.f);
	}
	return Y;
}

UK2Node_Knot* FBlueprintAutoLayout::CreateRoutingKnot(UEdGraph* Graph, int32 X, int32 Y)
{
	if (!Graph) return nullptr;

	UK2Node_Knot* Knot = NewObject<UK2Node_Knot>(Graph);
	if (!Knot) return nullptr;

	Graph->AddNode(Knot, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	Knot->CreateNewGuid();
	Knot->AllocateDefaultPins();
	Knot->NodePosX = X;
	Knot->NodePosY = Y;
	Knot->NodeComment = GBPALAutoRouteTag;   // invisible tag for idempotent re-runs
	Knot->bCommentBubbleVisible = false;
	return Knot;
}

void FBlueprintAutoLayout::RemoveAutoRoutedKnots(UEdGraph* Graph)
{
	if (!Graph) return;

	TArray<UK2Node_Knot*> ToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Node))
		{
			if (Knot->NodeComment == GBPALAutoRouteTag)
			{
				ToRemove.Add(Knot);
			}
		}
	}

	for (UK2Node_Knot* Knot : ToRemove)
	{
		UEdGraphPin* In = Knot->GetInputPin();
		UEdGraphPin* Out = Knot->GetOutputPin();

		// Copy the link lists before breaking them (BreakAllPinLinks mutates LinkedTo).
		TArray<UEdGraphPin*> Sources = In ? In->LinkedTo : TArray<UEdGraphPin*>();
		TArray<UEdGraphPin*> Dests = Out ? Out->LinkedTo : TArray<UEdGraphPin*>();

		if (In) In->BreakAllPinLinks();
		if (Out) Out->BreakAllPinLinks();

		// Reconnect each upstream source directly to each downstream destination.
		for (UEdGraphPin* S : Sources)
		{
			for (UEdGraphPin* D : Dests)
			{
				if (S && D)
				{
					S->MakeLinkTo(D);
				}
			}
		}

		Graph->RemoveNode(Knot);
	}
}

void FBlueprintAutoLayout::MaterializeLaneKnots(UEdGraph* Graph)
{
	if (!Graph) return;

	for (const FLaneRoute& Route : PendingLaneRoutes)
	{
		UEdGraphPin* Src = Route.SrcPin;
		UEdGraphPin* Dst = Route.DstPin;
		if (!Src || !Dst || Route.Waypoints.Num() == 0) continue;
		if (!Src->LinkedTo.Contains(Dst)) continue;   // wire changed since capture; leave it alone

		// Replace the single source→dest wire with source → knot → … → knot → dest (one knot per
		// reserved lane waypoint), so the long edge renders as straight segments through its lane
		// instead of one curved spline. If knot creation fails, the original link is restored.
		Src->BreakLinkTo(Dst);
		UEdGraphPin* Prev = Src;
		for (const FIntPoint& WP : Route.Waypoints)
		{
			UK2Node_Knot* Knot = CreateRoutingKnot(Graph, WP.X, WP.Y);
			if (!Knot) break;
			if (UEdGraphPin* KIn = Knot->GetInputPin()) Prev->MakeLinkTo(KIn);
			Prev = Knot->GetOutputPin();
			if (!Prev) break;
		}
		if (Prev) Prev->MakeLinkTo(Dst);
	}
}

void FBlueprintAutoLayout::RerouteWiresAroundObstacles(UEdGraph* Graph)
{
	if (!Graph) return;

	// Snapshot the real (non-knot, non-comment) nodes and their rects in final positions.
	struct FObstacle
	{
		UEdGraphNode* Node = nullptr;
		float L = 0.f, T = 0.f, R = 0.f, B = 0.f;
	};
	TArray<FObstacle> RealNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || IsKnot(Node) || IsComment(Node)) continue;
		FObstacle O; O.Node = Node;
		if (GetNodeRect(Node, O.L, O.T, O.R, O.B))
		{
			RealNodes.Add(O);
		}
	}
	if (RealNodes.Num() < 3) return; // need a source, a destination, and at least one obstacle

	// Collect every direct real→real link first; mutating links while iterating Pins is unsafe.
	// Links that already pass through a knot are skipped (their owning node is a knot), so we
	// never re-route an already-routed wire.
	struct FWire
	{
		UEdGraphPin* Out = nullptr;
		UEdGraphPin* In = nullptr;
		UEdGraphNode* A = nullptr;
		UEdGraphNode* B = nullptr;
	};
	TArray<FWire> Wires;
	for (const FObstacle& AO : RealNodes)
	{
		UEdGraphNode* A = AO.Node;
		for (UEdGraphPin* Out : A->Pins)
		{
			if (!Out || Out->bHidden || Out->Direction != EGPD_Output) continue;
			for (UEdGraphPin* In : Out->LinkedTo)
			{
				if (!In) continue;
				UEdGraphNode* B = In->GetOwningNode();
				if (!B || B == A || IsKnot(B) || IsComment(B)) continue;
				Wires.Add({ Out, In, A, B });
			}
		}
	}

	for (const FWire& W : Wires)
	{
		float aL, aT, aR, aB, bL, bT, bR, bB;
		if (!GetNodeRect(W.A, aL, aT, aR, aB) || !GetNodeRect(W.B, bL, bT, bR, bB)) continue;

		// Approximate the wire's endpoints: right edge of A at the out-pin's Y, left edge of B
		// at the in-pin's Y. Pin Y is estimated from pin ordering (no widget geometry needed).
		const float SrcX = aR;
		const float SrcY = EstimatePinY(W.A, W.Out);
		const float DstX = bL;
		const float DstY = EstimatePinY(W.B, W.In);

		// Only forward wires with enough horizontal room to clip a node. This naturally
		// excludes the short pure-node → consumer wires (gap < min length).
		if (DstX - SrcX < (float)Config.RerouteMinWireLength) continue;

		// Find obstacles strictly between A and B that the straight wire actually crosses.
		TArray<FObstacle> Obstacles;
		for (const FObstacle& C : RealNodes)
		{
			if (C.Node == W.A || C.Node == W.B) continue;
			if (C.R <= SrcX || C.L >= DstX) continue; // not horizontally within the wire span
			if (SegmentIntersectsRect(SrcX, SrcY, DstX, DstY, C.L, C.T, C.R, C.B))
			{
				Obstacles.Add(C);
			}
		}
		if (Obstacles.Num() == 0) continue;

		Obstacles.Sort([](const FObstacle& X, const FObstacle& Y) { return X.L < Y.L; });

		// Route the whole wire on a single side (above/below) — whichever needs the smaller
		// detour from the wire's midline — so it reads as one clean bend, not a zigzag.
		const float MidY = (SrcY + DstY) * 0.5f;
		float TopMost = TNumericLimits<float>::Max();
		float BotMost = TNumericLimits<float>::Lowest();
		for (const FObstacle& C : Obstacles)
		{
			TopMost = FMath::Min(TopMost, C.T);
			BotMost = FMath::Max(BotMost, C.B);
		}
		const float Margin = (float)Config.RerouteObstacleMargin;
		const float AboveY = TopMost - Margin;
		const float BelowY = BotMost + Margin;
		const bool bAbove = FMath::Abs(AboveY - MidY) <= FMath::Abs(BelowY - MidY);
		const float RouteY = bAbove ? AboveY : BelowY;

		// One knot per obstacle, at the obstacle's horizontal center, all sharing RouteY.
		TArray<UK2Node_Knot*> Knots;
		for (const FObstacle& C : Obstacles)
		{
			const int32 KX = FMath::RoundToInt((C.L + C.R) * 0.5f);
			const int32 KY = FMath::RoundToInt(RouteY);
			if (UK2Node_Knot* K = CreateRoutingKnot(Graph, KX, KY))
			{
				Knots.Add(K);
			}
		}
		if (Knots.Num() == 0) continue;

		// Rewire: A.out → k1.in, k1.out → k2.in, …, kn.out → B.in.
		W.Out->BreakLinkTo(W.In);
		UEdGraphPin* PrevOut = W.Out;
		for (UK2Node_Knot* K : Knots)
		{
			UEdGraphPin* KIn = K->GetInputPin();
			UEdGraphPin* KOut = K->GetOutputPin();
			if (!KIn || !KOut) continue;
			PrevOut->MakeLinkTo(KIn);
			PrevOut = KOut;
		}
		PrevOut->MakeLinkTo(W.In);
	}
}
