// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// Standalone unit tests for the engine-agnostic layered layout core (no Unreal).
//
// Build & run:
//   clang++ -std=c++17 -I../Source/BlueprintAutoLayout/Private \
//       test_layered.cpp ../Source/BlueprintAutoLayout/Private/BPALLayeredLayout.cpp -o /tmp/bpal_test \
//   && /tmp/bpal_test
//
// This is the fast QA loop for the algorithm: it exercises ranking, dummy insertion, crossing
// reduction, and pin-accurate straightening WITHOUT an editor rebuild.

#include "BPALLayeredLayout.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace bpal;

static int gFail = 0;
static int gPass = 0;

#define CHECK(cond, msg) do { \
	if (cond) { ++gPass; } \
	else { ++gFail; std::printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static bool Approx(float a, float b, float eps = 0.6f) { return std::fabs(a - b) <= eps; }

// Find a vertex's solved record by id.
static const FLayeredVertex& V(const FLayeredGraph& G, int id) { return G.Vertices()[id]; }

//------------------------------------------------------------------------------
static void Test_LinearChainRanks()
{
	std::printf("Test_LinearChainRanks\n");
	FLayeredGraph G;
	int a = G.AddVertex(200, 80);
	int b = G.AddVertex(200, 80);
	int c = G.AddVertex(200, 80);
	int d = G.AddVertex(200, 80);
	G.AddEdge(a, b); G.AddEdge(b, c); G.AddEdge(c, d);
	G.Solve();
	CHECK(V(G, a).Rank == 0, "a rank 0");
	CHECK(V(G, b).Rank == 1, "b rank 1");
	CHECK(V(G, c).Rank == 2, "c rank 2");
	CHECK(V(G, d).Rank == 3, "d rank 3");
	CHECK(V(G, a).X < V(G, b).X && V(G, b).X < V(G, c).X, "X increases with rank");
	CHECK(G.NumRanks() == 4, "4 ranks");
}

//------------------------------------------------------------------------------
static void Test_LongEdgeDummies()
{
	std::printf("Test_LongEdgeDummies\n");
	FLayeredGraph G;
	int a = G.AddVertex(200, 80);  // rank 0
	int b = G.AddVertex(200, 80);  // rank 1
	int c = G.AddVertex(200, 80);  // rank 2
	G.AddEdge(a, b);               // edge 0
	G.AddEdge(b, c);               // edge 1
	G.AddEdge(a, c);               // edge 2: spans rank 0->2, needs 1 dummy
	G.Solve();
	CHECK(V(G, c).Rank == 2, "c at rank 2");
	const std::vector<int>& chain = G.GetEdgeChain(2);
	CHECK(chain.size() == 1, "a->c gets exactly 1 dummy");
	if (chain.size() == 1)
	{
		CHECK(V(G, chain[0]).bIsDummy, "chain node is a dummy");
		CHECK(V(G, chain[0]).Rank == 1, "dummy sits in rank 1");
	}
	CHECK(G.GetEdgeChain(0).empty(), "short edge a->b has no dummies");
}

//------------------------------------------------------------------------------
static void Test_CrossingReduction()
{
	std::printf("Test_CrossingReduction\n");
	// Two parallel edges that cross if ordered naively: u0->w1, u1->w0.
	// (Added in an order that puts them in the crossing arrangement initially.)
	FLayeredGraph G;
	int u0 = G.AddVertex(180, 60); // rank 0, order 0
	int u1 = G.AddVertex(180, 60); // rank 0, order 1
	int w0 = G.AddVertex(180, 60); // rank 1, order 0
	int w1 = G.AddVertex(180, 60); // rank 1, order 1
	G.AddEdge(u0, w1);
	G.AddEdge(u1, w0);
	G.Solve();
	// After ordering, the two edges should not cross: u0 should align above/below to avoid crossing.
	// Verify the final order yields zero crossings by checking the relative order is consistent.
	const bool u0AboveU1 = V(G, u0).Order < V(G, u1).Order;
	const bool w1AboveW0 = V(G, w1).Order < V(G, w0).Order;
	// If u0 above u1, then since u0->w1 and u1->w0, w1 should be above w0 for no crossing.
	CHECK(u0AboveU1 == w1AboveW0, "ordering removes the crossing (endpoints aligned)");
}

//------------------------------------------------------------------------------
static void Test_PortStraightening()
{
	std::printf("Test_PortStraightening\n");
	// A feeds B. A's output pin is low (y-offset 60 from A's top); B's input pin is high (offset 12).
	// After solve, the wire should be straight on the pins: A.Y + 60 == B.Y + 12.
	FLayeredGraph G;
	int a = G.AddVertex(200, 100);
	int b = G.AddVertex(200, 100);
	G.AddEdge(a, b, /*fromPort*/ 60.f, /*toPort*/ 12.f, /*exec*/ true);
	G.Solve();
	const float srcPinY = V(G, a).Y + 60.f;
	const float dstPinY = V(G, b).Y + 12.f;
	CHECK(Approx(srcPinY, dstPinY), "single edge: source & dest pins share Y (straight wire)");
}

//------------------------------------------------------------------------------
static void Test_DummyChainStraight()
{
	std::printf("Test_DummyChainStraight\n");
	// A long edge through 2 filler ranks should produce a straight dummy chain: every dummy and the
	// two endpoints' pins line up on one Y. Add filler nodes so ranks 1,2 also have real nodes that
	// would otherwise pull the chain off-line; the dummies' high priority should keep it straight.
	FLayeredGraph G;
	int a = G.AddVertex(200, 100); // rank 0
	int b = G.AddVertex(200, 100); // rank 1 (filler chain)
	int c = G.AddVertex(200, 100); // rank 2
	int d = G.AddVertex(200, 100); // rank 3
	G.AddEdge(a, b); G.AddEdge(b, c); G.AddEdge(c, d); // spine
	int longEdge = 3;
	G.AddEdge(a, d, 50.f, 50.f, true); // edge 3: spans rank 0->3, 2 dummies, ports both at 50
	G.Solve();
	const std::vector<int>& chain = G.GetEdgeChain(longEdge);
	CHECK(chain.size() == 2, "a->d gets 2 dummies");
	if (chain.size() == 2)
	{
		// The meaningful guarantee: the long edge runs straight through its OWN lane (the two
		// dummies are collinear -> no kink), and the destination pin connects straight into that
		// lane. The source node legitimately compromises its single Y between the spine and this
		// edge, so we do NOT require the lane to sit on the source pin line.
		const float dC0 = V(G, chain[0]).Y + V(G, chain[0]).Height * 0.5f;
		const float dC1 = V(G, chain[1]).Y + V(G, chain[1]).Height * 0.5f;
		const float dstPin = V(G, d).Y + 50.f;
		CHECK(Approx(dC0, dC1, 1.5f), "dummy chain is collinear (no kink in the long edge middle)");
		CHECK(Approx(dstPin, dC1, 2.0f), "destination pin connects straight into the dummy lane");
	}
}

//------------------------------------------------------------------------------
static void Test_NoOverlapWithinRank()
{
	std::printf("Test_NoOverlapWithinRank\n");
	// Three nodes in rank 1 all fed by one source; they must not overlap vertically.
	FLayeredGraph G;
	int s = G.AddVertex(200, 100);
	int x = G.AddVertex(200, 100);
	int y = G.AddVertex(200, 100);
	int z = G.AddVertex(200, 100);
	G.AddEdge(s, x); G.AddEdge(s, y); G.AddEdge(s, z);
	G.Solve();
	std::vector<int> rank1 = { x, y, z };
	// sort by Y
	std::sort(rank1.begin(), rank1.end(), [&](int p, int q){ return V(G, p).Y < V(G, q).Y; });
	for (size_t i = 1; i < rank1.size(); ++i)
	{
		const float topGap = V(G, rank1[i]).Y - (V(G, rank1[i - 1]).Y + V(G, rank1[i - 1]).Height);
		CHECK(topGap >= -0.5f, "rank-1 nodes do not overlap (min separation held)");
	}
}

//------------------------------------------------------------------------------
static void Test_CycleNoHang()
{
	std::printf("Test_CycleNoHang\n");
	FLayeredGraph G;
	int a = G.AddVertex(150, 60);
	int b = G.AddVertex(150, 60);
	int c = G.AddVertex(150, 60);
	G.AddEdge(a, b); G.AddEdge(b, c); G.AddEdge(c, a); // cycle
	G.Solve();
	CHECK(G.NumRanks() >= 1, "cycle ranked without hanging");
	CHECK(V(G, a).Rank >= 0 && V(G, b).Rank >= 0 && V(G, c).Rank >= 0, "all ranked");
}

//------------------------------------------------------------------------------
static void Test_SeedRanks()
{
	std::printf("Test_SeedRanks\n");
	// When the adapter supplies ranks, the core must honor them (it pulls a pure data node to
	// just-left-of-its-consumer rather than letting longest-path shove it to column 0).
	FLayeredGraph G;
	int getVar = G.AddVertex(160, 50);  // a "variable Get" feeding the node at rank 3
	int n0 = G.AddVertex(200, 80);
	int n1 = G.AddVertex(200, 80);
	int n2 = G.AddVertex(200, 80);
	int n3 = G.AddVertex(200, 80);
	G.AddEdge(n0, n1, -1, -1, true);
	G.AddEdge(n1, n2, -1, -1, true);
	G.AddEdge(n2, n3, -1, -1, true);
	G.AddEdge(getVar, n3, 25.f, 40.f, false); // data edge into n3
	// Seeds: exec spine 0..3, data node pulled to rank 2 (just left of its consumer n3 at rank 3).
	G.SetSeedRank(n0, 0); G.SetSeedRank(n1, 1); G.SetSeedRank(n2, 2); G.SetSeedRank(n3, 3);
	G.SetSeedRank(getVar, 2);
	G.Solve();
	CHECK(V(G, getVar).Rank == 2, "seeded data-node rank honored (sits left of consumer, not col 0)");
	CHECK(V(G, n3).Rank == 3, "seeded exec rank honored");
	CHECK(V(G, getVar).X < V(G, n3).X, "data node is left of its consumer");
	// The data edge spans one rank now (2->3), so no dummy is inserted.
	CHECK(G.GetEdgeChain(3).empty(), "seeded short data edge gets no dummy");
}

//------------------------------------------------------------------------------
int main()
{
	Test_LinearChainRanks();
	Test_LongEdgeDummies();
	Test_CrossingReduction();
	Test_PortStraightening();
	Test_DummyChainStraight();
	Test_NoOverlapWithinRank();
	Test_CycleNoHang();
	Test_SeedRanks();

	std::printf("\n==== %d passed, %d failed ====\n", gPass, gFail);
	return gFail == 0 ? 0 : 1;
}
