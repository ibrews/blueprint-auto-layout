// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayout.cpp - Pin-aware Blueprint graph layout algorithm.

#include "BlueprintAutoLayout.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"

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
	PositionedPureNodes.Empty();

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
	PositionedPureNodes.Empty();

	BuildLayoutTree(Graph, &SpecificRoots);

	if (RootNodes.Num() == 0)
	{
		return 0;
	}

	CalculateSubtreeHeights();
	AssignPositions(StartX, StartY);
	ApplyPositions();

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

			if (Info->bIsPureNode)
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
				if (Info.bIsPureNode) continue;

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

	Info->bIsPureNode = IsPureNode(Info->Node);
	Info->bIsBranchNode = IsBranchNode(Info->Node);

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
		for (UEdGraphPin* LinkedPin : ExecOut->LinkedTo)
		{
			if (LinkedPin)
			{
				UEdGraphNode* ChildNode = LinkedPin->GetOwningNode();
				FLayoutNodeInfo* ChildInfo = GetOrCreateNodeInfo(ChildNode);

				if (!ChildInfo->bVisited && !ChildInfo->bIsPureNode)
				{
					ChildInfo->Parent = Current;
					Current->ExecChildren.Add(ChildInfo);
					Current->ChildPinNames.Add(ChildInfo, ExecOut->PinName);

					TraverseExecFlow(ChildInfo, CurrentDepth + 1);
				}
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
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin)
				{
					UEdGraphNode* SourceNode = LinkedPin->GetOwningNode();
					if (SourceNode && IsPureNode(SourceNode) && !Seen.Contains(SourceNode))
					{
						Seen.Add(SourceNode);
						if (FLayoutNodeInfo* PureInfo = NodeInfoMap.Find(SourceNode))
						{
							ExecNode->DataProviders.Add(PureInfo);
						}
					}
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
			PositionExecSubtree(Child, ChildX, ChildY);

			// Next child below this one's subtree (use actual subtree height + extra padding)
			ChildY += Child->SubtreeHeight + Config.BranchExtraPaddingY;
		}
	}
	else
	{
		// Sequential: children follow in same lane
		for (FLayoutNodeInfo* Child : Node->ExecChildren)
		{
			PositionExecSubtree(Child, ChildX, Y);
			// Subsequent sequential children continue using their actual width
			ChildX += Child->NodeWidth + Config.NodePaddingX;
		}
	}
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

		// Calculate position - use actual heights for Y spacing
		Pure->LayoutX = CurrentX - (Column * (MaxPureWidth + Config.NodePaddingX));
		Pure->LayoutY = ColumnStartY + (RowInColumn * (Config.DefaultNodeHeight + Config.NodePaddingY));

		// Move to next slot
		RowInColumn++;
		if (RowInColumn >= Config.MaxPureNodesPerColumn)
		{
			RowInColumn = 0;
			Column++;
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
