// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayout.cpp - Pin-aware Blueprint graph layout algorithm.

#include "BlueprintAutoLayout.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"

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

	// Record which nodes each comment currently wraps, while positions are still original.
	CaptureCommentMembership(Graph);

	// Phase 1: Build the layout tree from exec flow
	BuildLayoutTree(Graph);

	if (RootNodes.Num() == 0)
	{
		return 0;
	}

	// Phase 2: Calculate subtree heights (bottom-up, in pixels)
	CalculateSubtreeHeights();

	// Phase 3: Assign positions (top-down)
	AssignPositions(StartX, StartY);

	// Phase 4: Apply to actual nodes
	ApplyPositions();

	// Phase 5: Cosmetic post-passes — reroute nodes become wire bends, comments re-wrap their members
	PositionKnots();
	WrapComments();

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

	CaptureCommentMembership(Graph);

	BuildLayoutTree(Graph, &SpecificRoots);

	if (RootNodes.Num() == 0)
	{
		return 0;
	}

	CalculateSubtreeHeights();
	AssignPositions(StartX, StartY);
	ApplyPositions();
	PositionKnots();
	WrapComments();

	return NodeInfoMap.Num();
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

	Info->NodeWidth = Width;
	Info->NodeHeight = Height;
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

	if (Node->bIsBranchNode && Node->ExecChildren.Num() >= 2)
	{
		// Branch node: children are stacked vertically (parallel paths)
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

	if (Node->bIsBranchNode && Node->ExecChildren.Num() >= 2)
	{
		// Branch: children go vertically stacked
		// Sort children by pin name (Then before Else)
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
				if (UEdGraphNode* S = InPin->LinkedTo[0]->GetOwningNode())
				{
					// Right edge / vertical centre of the upstream node.
					SrcX = S->NodePosX + FMath::Max(S->NodeWidth, 0);
					SrcY = S->NodePosY + FMath::Max(S->NodeHeight, 0) * 0.5f;
					bHaveSrc = true;
				}
			}
			if (OutPin && OutPin->LinkedTo.Num() > 0 && OutPin->LinkedTo[0])
			{
				if (UEdGraphNode* D = OutPin->LinkedTo[0]->GetOwningNode())
				{
					// Left edge / vertical centre of the downstream node.
					DstX = D->NodePosX;
					DstY = D->NodePosY + FMath::Max(D->NodeHeight, 0) * 0.5f;
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
		Comment->ResizeNode(FVector2f((float)NewWidth, (float)NewHeight));
	}
}
