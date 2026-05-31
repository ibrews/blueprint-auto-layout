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

class SGraphPanel;
class UK2Node_Knot;

/**
 * How auto-generated group comment boxes pick their color. Mirrors the editor-preferences
 * enum (EBPALCommentColorMode) but is kept as a plain C++ enum here so this algorithm header
 * stays free of UObject reflection (it is embedded verbatim in other hosts).
 */
enum class ECommentColorMode : uint8
{
	KeywordSemantic,   // map the root title to a meaningful color, hash fallback
	CyclingPalette,    // step through a fixed palette
};

/**
 * Layout configuration options
 */
struct FBlueprintLayoutConfig
{
	// Padding/margins between nodes (added to actual node sizes). Generous by default so wires
	// have room to run between columns/rows and are less likely to cross over other nodes.
	int32 NodePaddingX = 110;          // Horizontal padding between nodes
	int32 NodePaddingY = 44;           // Vertical padding between nodes
	int32 BranchExtraPaddingY = 90;    // Extra vertical padding between branch paths
	int32 RootExtraPaddingY = 200;     // Extra padding between different event roots

	// Pure node positioning
	int32 PureNodeGapX = 30;           // Gap between pure nodes and their consumer
	int32 PureNodePaddingY = 20;       // Vertical padding between pure nodes
	int32 MaxPureNodesPerColumn = 4;   // Max pure nodes before starting new column

	// Comment box wrapping (applied after node layout)
	int32 CommentPadding = 36;         // Margin between a comment's edge and the nodes it wraps
	int32 CommentTitleHeight = 40;     // Extra headroom above wrapped nodes for the comment title bar

	// Fallback sizes when node dimensions aren't available
	int32 DefaultNodeWidth = 220;      // Default width if node reports 0
	int32 DefaultNodeHeight = 100;     // Default height if node reports 0
	int32 PinHeightEstimate = 26;      // Estimated height per pin for size calculation

	// Smart wire rerouting (opt-in, via the "route wires" actions). Inserts reroute (knot)
	// nodes so flow/data wires bend around nodes they would otherwise cut straight across.
	int32 RerouteObstacleMargin = 40;  // Vertical clearance above/below an obstacle node
	int32 RerouteMinWireLength = 60;   // Ignore wires shorter than this (no room to clip a node)

	// Coloring for the comment boxes the auto-grouping actions create.
	ECommentColorMode CommentColorMode = ECommentColorMode::KeywordSemantic;
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
	bool bIsKnot = false;         // Reroute (knot) node — treated as a wire bend, not a layout node
	bool bIsComment = false;      // Comment box — wrapped around its members after layout
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

	/**
	 * Like LayoutGraph, but additionally wraps each event/function subtree in a NEW comment box,
	 * automatically named after that subtree's root node (e.g. an event's name). No-op grouping
	 * when the graph has fewer than two roots (a single box around everything isn't useful).
	 */
	int32 LayoutAndGroupGraph(UEdGraph* Graph, int32 StartX = 0, int32 StartY = 0);

	/**
	 * Like LayoutGraph, but additionally reroutes wires that would cut across intervening
	 * nodes by inserting reroute (knot) nodes that bend the wire above/below each obstacle.
	 * This MUTATES the graph (adds knots). Re-running is idempotent: knots this pass created
	 * are tagged and removed/regenerated each time, so they don't accumulate.
	 */
	int32 LayoutAndRouteGraph(UEdGraph* Graph, int32 StartX = 0, int32 StartY = 0);

	/**
	 * LayoutAndGroupGraph + the wire-rerouting pass: lay out, wrap each subtree in an
	 * auto-named comment, then route wires around obstacle nodes.
	 */
	int32 LayoutGroupAndRouteGraph(UEdGraph* Graph, int32 StartX = 0, int32 StartY = 0);

	/** Get layout info for debugging */
	const TMap<UEdGraphNode*, FLayoutNodeInfo>& GetLayoutInfo() const { return NodeInfoMap; }

private:
	FBlueprintLayoutConfig Config;
	TMap<UEdGraphNode*, FLayoutNodeInfo> NodeInfoMap;
	TArray<FLayoutNodeInfo*> RootNodes;
	TArray<FLayoutNodeInfo*> AllExecNodes;      // Only exec-flow nodes
	TArray<FLayoutNodeInfo*> AllPureNodes;      // Only pure nodes
	TArray<FLayoutNodeInfo*> AllKnots;          // Reroute nodes (positioned as wire bends post-layout)
	TArray<FLayoutNodeInfo*> AllComments;       // Comment boxes (wrapped around members post-layout)
	TSet<FLayoutNodeInfo*> PositionedPureNodes; // Track which pure nodes are already positioned

	// The live graph panel for the graph being laid out (when its editor is open), used to read
	// each node's ACTUAL rendered size instead of estimating. Null when no editor is open.
	SGraphPanel* LiveGraphPanel = nullptr;

	// Comment membership captured BEFORE layout (geometric containment at invocation time),
	// so comments re-wrap the nodes they originally contained after those nodes move.
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> CommentMembers;

	// Phase 1: Build the layout tree
	void BuildLayoutTree(UEdGraph* Graph, const TArray<UEdGraphNode*>* SpecificRoots = nullptr);
	FLayoutNodeInfo* GetOrCreateNodeInfo(UEdGraphNode* Node);
	void ClassifyNode(FLayoutNodeInfo* Info);
	void CalculateNodeDimensions(FLayoutNodeInfo* Info);
	void ResolveLiveGraphPanel(UEdGraph* Graph);                 // find the open editor's panel (if any)
	bool TryGetRenderedNodeSize(UEdGraphNode* Node, int32& OutWidth, int32& OutHeight) const;
	void TraverseExecFlow(FLayoutNodeInfo* Current, int32 CurrentDepth);
	void CollectPureProviders(FLayoutNodeInfo* ExecNode);

	// Phase 2: Calculate subtree dimensions (in pixels)
	void CalculateSubtreeHeights();
	int32 CalculateHeight(FLayoutNodeInfo* Node);

	// Phase 3: Assign positions
	void AssignPositions(int32 StartX, int32 StartY);
	void PositionExecSubtree(FLayoutNodeInfo* Node, int32 X, int32 Y);
	void PositionPureNodesForConsumer(FLayoutNodeInfo* Consumer);
	// Horizontal lane a consumer's pure-node column needs, so it doesn't overlap the exec predecessor.
	int32 GetPureColumnWidth(FLayoutNodeInfo* Consumer) const;

	// Phase 4: Apply positions to actual nodes
	void ApplyPositions();

	// Phase 5: Reroute nodes and comment boxes (cosmetic post-passes)
	void CaptureCommentMembership(UEdGraph* Graph);  // before layout, while positions are original
	void PositionKnots();                            // place each reroute node on its wire
	void WrapComments();                             // resize/move comments around their members

	// Auto-grouping (opt-in): spawn a comment box per root subtree, named after the root.
	void CreateGroupComments(UEdGraph* Graph);
	void CollectSubtreeMembers(FLayoutNodeInfo* Node, TArray<UEdGraphNode*>& OutMembers, TSet<FLayoutNodeInfo*>& Visited) const;
	// Color for a group comment, per the configured ECommentColorMode (Index drives the cycling palette).
	FLinearColor ChooseCommentColor(const FString& RootTitle, int32 Index) const;
	static FLinearColor KeywordColorForTitle(const FString& Title);  // semantic color, hash-of-title fallback

	// Phase 6: Smart wire rerouting (opt-in) — insert knots so wires avoid intervening nodes.
	void RerouteWiresAroundObstacles(UEdGraph* Graph);    // scan real→real links, bend around obstacles
	void RemoveAutoRoutedKnots(UEdGraph* Graph);          // splice out + delete knots this pass created
	UK2Node_Knot* CreateRoutingKnot(UEdGraph* Graph, int32 X, int32 Y);  // tagged knot at a waypoint
	bool GetNodeRect(UEdGraphNode* Node, float& L, float& T, float& R, float& B) const;
	float EstimatePinY(UEdGraphNode* Node, UEdGraphPin* Pin) const;       // approximate a pin's Y in graph units
	// Liang–Barsky segment vs axis-aligned rect intersection (true if the segment touches the rect).
	static bool SegmentIntersectsRect(float X0, float Y0, float X1, float Y1, float L, float T, float R, float B);

	// Helpers
	bool IsExecPin(UEdGraphPin* Pin) const;
	bool IsPureNode(UEdGraphNode* Node) const;
	bool IsBranchNode(UEdGraphNode* Node) const;
	bool IsKnot(UEdGraphNode* Node) const;
	bool IsComment(UEdGraphNode* Node) const;

	// Trace exec/data links through reroute (knot) nodes to the real nodes on the far side.
	void GatherRealExecTargets(UEdGraphPin* OutputExecPin, TArray<UEdGraphNode*>& OutTargets, TSet<UEdGraphPin*>& Visited) const;
	void GatherRealPureSources(UEdGraphPin* InputDataPin, TSet<UEdGraphNode*>& Seen, TArray<UEdGraphNode*>& OutSources, TSet<UEdGraphPin*>& Visited) const;
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
