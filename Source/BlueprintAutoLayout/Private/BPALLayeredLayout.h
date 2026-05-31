// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BPALLayeredLayout.h - Engine-agnostic layered (Sugiyama) graph layout core.
//
// Pure C++/STL, ZERO Unreal types: this lets the core be unit-tested standalone with a plain
// clang++ harness (no editor rebuild) and embedded verbatim into other build hosts. The Unreal
// side (FBlueprintAutoLayout) builds an FLayeredGraph from a UEdGraph, runs Solve(), and writes
// the resulting coordinates back onto the real nodes.
//
// Pipeline mirrors the classic layered-drawing pipeline (Sugiyama et al.; Gansner/dot):
//   1. AssignRanks    - longest-path layering, with DFS cycle-breaking (reverse back-edges).
//   2. InsertDummies  - split every edge that spans >1 rank into a chain of unit-width dummies,
//                       so long edges get their own reserved straight lane (fixes S-wires/humps).
//   3. OrderWithinRanks - median heuristic, alternating up/down sweeps; keep the min-crossing order.
//   4. AssignCoordinates - X = cumulative rank-column widths; Y (the within-rank cross axis) by a
//                       PORT-AWARE priority method so wires line up on PIN Y, not node center.
//
// Coordinate convention matches Unreal blueprint graphs: exec flows LEFT->RIGHT, so a "rank" is a
// vertical column (increasing X), and nodes within a rank are stacked along Y. All positions are
// top-left corners in graph units, the same space as UEdGraphNode::NodePos{X,Y}.

#pragma once

#include <vector>
#include <cstdint>

namespace bpal
{
	/** Tuning knobs for the layered layout. Defaults are sensible for blueprint graphs. */
	struct FLayeredConfig
	{
		float RankSpacingX = 120.f;   // horizontal gap between rank columns (added to the rank's max node width)
		float NodeSpacingY = 40.f;    // vertical gap between two nodes in the same rank
		float DummyWidth   = 1.f;     // virtual width of a dummy (long-edge waypoint) node
		float DummyHeight  = 1.f;     // virtual height of a dummy node
		int   OrderingSweeps = 12;    // max alternating median sweeps for crossing reduction
		int   CoordSweeps    = 8;     // priority-method passes for the Y coordinate assignment
	};

	/** A node to be laid out. Real nodes come from the editor graph; dummies are inserted by the core. */
	struct FLayeredVertex
	{
		int   Id = -1;          // stable index into FLayeredGraph::Vertices
		float Width = 0.f;
		float Height = 0.f;
		bool  bIsDummy = false; // true for long-edge waypoints inserted by InsertDummies()
		bool  bPullRight = false; // source vertex to pull toward its consumer (e.g. a variable Get)

		// --- filled in by Solve() ---
		int   Rank = -1;        // column index (0 = leftmost / roots)
		int   Order = -1;       // position within the rank, top (0) to bottom
		float X = 0.f;          // assigned top-left X (graph units)
		float Y = 0.f;          // assigned top-left Y (graph units)
	};

	/**
	 * A directed edge. PortFromY/PortToY are the Y-offsets of the connected pins measured from the
	 * top of the source / destination node (graph units). They drive pin-accurate straightening:
	 * the core tries to make (Source.Y + PortFromY) == (Dest.Y + PortToY). Pass <0 to mean "node
	 * center" (the core substitutes Height/2). bExec is informational (exec edges weigh more heavily
	 * during straightening so the white exec spine is prioritized over data wires).
	 */
	struct FLayeredEdge
	{
		int   From = -1;
		int   To   = -1;
		float PortFromY = -1.f;
		float PortToY   = -1.f;
		bool  bExec = false;
	};

	/**
	 * The layout problem + solution. Build it (AddVertex/AddEdge), call Solve(), then read back each
	 * vertex's Rank/Order/X/Y. Original-edge -> dummy-chain mapping is exposed via GetEdgeChain() so
	 * the Unreal side can optionally materialize long edges as reroute knots.
	 */
	class FLayeredGraph
	{
	public:
		explicit FLayeredGraph(const FLayeredConfig& InConfig = FLayeredConfig()) : Config(InConfig) {}

		/** Add a real node; returns its vertex id. */
		int AddVertex(float Width, float Height);

		/** Add a directed edge between two previously-added vertex ids. */
		void AddEdge(int From, int To, float PortFromY = -1.f, float PortToY = -1.f, bool bExec = false);

		/**
		 * Pre-assign a vertex's rank (column). When ANY seed rank is set, the core skips its own
		 * longest-path ranking and uses the supplied ranks (normalized so the minimum becomes 0).
		 * The Unreal adapter uses this to keep exec-flow depth as the rank seed and pull pure data
		 * nodes to just left of their consumers, rather than letting longest-path shove sources left.
		 */
		void SetSeedRank(int Vertex, int Rank);

		/**
		 * Flag a source vertex (no incoming edges) to be pulled rightward to just-left-of its
		 * nearest consumer after longest-path ranking, instead of resting at column 0. Used for
		 * pure data providers (e.g. a variable Get) so they sit beside what they feed. Ignored for
		 * vertices that have incoming edges (their rank is fixed by their providers).
		 */
		void SetPullTowardConsumers(int Vertex);

		/** Run the full pipeline. Safe to call once. */
		void Solve();

		const std::vector<FLayeredVertex>& Vertices() const { return Vertices_; }
		int NumRanks() const { return NumRanks_; }

		/**
		 * For an original edge index (order of AddEdge calls), the ordered list of dummy vertex ids
		 * inserted along it (empty for short edges). Useful for knot materialization.
		 */
		const std::vector<int>& GetEdgeChain(int OriginalEdgeIndex) const;

	private:
		FLayeredConfig Config;
		std::vector<FLayeredVertex> Vertices_;
		std::vector<FLayeredEdge>   Edges_;            // live edge set (rewired through dummies after InsertDummies)
		std::vector<std::vector<int>> EdgeChains_;     // per-original-edge dummy chains
		int OriginalEdgeCount_ = 0;
		int NumRanks_ = 0;
		bool bSeedRanks_ = false;                      // adapter supplied ranks; skip longest-path

		// pipeline stages
		void AssignRanks();
		void InsertDummies();
		void OrderWithinRanks();
		void AssignCoordinates();
		// Cross-axis (Y) placement via pin-aware Brandes-Köpf: aligns each vertex to its median
		// neighbor (preferring exec edges so the white spine drives the blocks), builds port-aligned
		// blocks (inner-shift), compacts them with min separation, and averages the up- and
		// down-aligned passes. Sets each vertex's Y (top). RankOrders is the solved within-rank order.
		void AssignYBrandesKopf(const std::vector<std::vector<int>>& RankOrders);

		// helpers
		std::vector<std::vector<int>> BuildRankOrders() const;  // [rank] -> vertex ids in current Order
		int CountCrossings(const std::vector<std::vector<int>>& RankOrders) const;
		float PortFromYOf(const FLayeredEdge& E) const;
		float PortToYOf(const FLayeredEdge& E) const;
	};

	// Free helper, exposed for the standalone test harness.
	int CountCrossingsBetween(const std::vector<int>& Upper, const std::vector<int>& Lower,
	                          const std::vector<FLayeredEdge>& Edges,
	                          const std::vector<int>& OrderOfVertex);
}
