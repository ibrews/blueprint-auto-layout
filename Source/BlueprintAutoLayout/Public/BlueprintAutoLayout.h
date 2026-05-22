// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayout.h - Pin-aware Blueprint graph layout algorithm.
// Handles branches, loops, pure nodes, and complex execution flows.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "EdGraphSchema_K2.h"

/**
 * Layout configuration options
 */
struct FBlueprintLayoutConfig
{
	// Padding/margins between nodes (added to actual node sizes)
	int32 NodePaddingX = 50;           // Horizontal padding between nodes
	int32 NodePaddingY = 30;           // Vertical padding between nodes
	int32 BranchExtraPaddingY = 50;    // Extra vertical padding between branch paths
	int32 RootExtraPaddingY = 80;      // Extra padding between different event roots

	// Pure node positioning
	int32 PureNodeGapX = 30;           // Gap between pure nodes and their consumer
	int32 PureNodePaddingY = 20;       // Vertical padding between pure nodes
	int32 MaxPureNodesPerColumn = 4;   // Max pure nodes before starting new column

	// Fallback sizes when node dimensions aren't available
	int32 DefaultNodeWidth = 220;      // Default width if node reports 0
	int32 DefaultNodeHeight = 100;     // Default height if node reports 0
	int32 PinHeightEstimate = 26;      // Estimated height per pin for size calculation
};

/**
 * Internal node representation for layout calculations
 */
struct FLayoutNodeInfo
{
	UEdGraphNode* Node = nullptr;

	// Tree structure
	FLayoutNodeInfo* Parent = nullptr;
	TArray<FLayoutNodeInfo*> ExecChildren;     // Children via exec pins
	TArray<FLayoutNodeInfo*> DataProviders;    // Pure nodes that feed into this node

	// Actual node dimensions (calculated from node or estimated)
	int32 NodeWidth = 0;
	int32 NodeHeight = 0;

	// Layout data
	int32 Depth = 0;              // Distance from root in exec flow

	// Subtree measurements (in pixels)
	int32 SubtreeHeight = 0;      // Total height needed for this subtree
	int32 SubtreeWidth = 0;       // Total width needed for this subtree

	// Position (final calculated position)
	int32 LayoutX = 0;
	int32 LayoutY = 0;

	// Flags
	bool bIsBranchNode = false;
	bool bIsPureNode = false;
	bool bIsRootNode = false;
	bool bVisited = false;
	bool bPositioned = false;     // Has this node been positioned already?

	// For branch nodes: track which pin each child came from
	TMap<FLayoutNodeInfo*, FName> ChildPinNames;
};

/**
 * Main layout algorithm class
 */
class BLUEPRINTAUTOLAYOUT_API FBlueprintAutoLayout
{
public:
	FBlueprintAutoLayout(const FBlueprintLayoutConfig& InConfig = FBlueprintLayoutConfig())
		: Config(InConfig)
	{}

	/**
	 * Layout all nodes in a graph
	 */
	int32 LayoutGraph(UEdGraph* Graph, int32 StartX = 0, int32 StartY = 0);

	/**
	 * Layout a subtree starting from specific nodes
	 */
	int32 LayoutSubtree(UEdGraph* Graph, const TArray<UEdGraphNode*>& RootNodes, int32 StartX = 0, int32 StartY = 0);

	/** Get layout info for debugging */
	const TMap<UEdGraphNode*, FLayoutNodeInfo>& GetLayoutInfo() const { return NodeInfoMap; }

private:
	FBlueprintLayoutConfig Config;
	TMap<UEdGraphNode*, FLayoutNodeInfo> NodeInfoMap;
	TArray<FLayoutNodeInfo*> RootNodes;
	TArray<FLayoutNodeInfo*> AllExecNodes;      // Only exec-flow nodes
	TArray<FLayoutNodeInfo*> AllPureNodes;      // Only pure nodes
	TSet<FLayoutNodeInfo*> PositionedPureNodes; // Track which pure nodes are already positioned

	// Phase 1: Build the layout tree
	void BuildLayoutTree(UEdGraph* Graph, const TArray<UEdGraphNode*>* SpecificRoots = nullptr);
	FLayoutNodeInfo* GetOrCreateNodeInfo(UEdGraphNode* Node);
	void ClassifyNode(FLayoutNodeInfo* Info);
	void CalculateNodeDimensions(FLayoutNodeInfo* Info);
	void TraverseExecFlow(FLayoutNodeInfo* Current, int32 CurrentDepth);
	void CollectPureProviders(FLayoutNodeInfo* ExecNode);

	// Phase 2: Calculate subtree dimensions (in pixels)
	void CalculateSubtreeHeights();
	int32 CalculateHeight(FLayoutNodeInfo* Node);

	// Phase 3: Assign positions
	void AssignPositions(int32 StartX, int32 StartY);
	void PositionExecSubtree(FLayoutNodeInfo* Node, int32 X, int32 Y);
	void PositionPureNodesForConsumer(FLayoutNodeInfo* Consumer);

	// Phase 4: Apply positions to actual nodes
	void ApplyPositions();

	// Helpers
	bool IsExecPin(UEdGraphPin* Pin) const;
	bool IsPureNode(UEdGraphNode* Node) const;
	bool IsBranchNode(UEdGraphNode* Node) const;
	TArray<UEdGraphPin*> GetExecOutputPins(UEdGraphNode* Node) const;
	TArray<UEdGraphPin*> GetExecInputPins(UEdGraphNode* Node) const;
	TArray<UEdGraphNode*> GetPureInputNodes(UEdGraphNode* Node) const;
};

/**
 * Utility function to layout a graph with default settings
 */
inline int32 AutoLayoutBlueprintGraph(UEdGraph* Graph, int32 StartX = 0, int32 StartY = 0)
{
	FBlueprintAutoLayout Layout;
	return Layout.LayoutGraph(Graph, StartX, StartY);
}
