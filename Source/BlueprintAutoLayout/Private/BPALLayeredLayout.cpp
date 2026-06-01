// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BPALLayeredLayout.cpp - Engine-agnostic layered (Sugiyama) layout core. See BPALLayeredLayout.h.

#include "BPALLayeredLayout.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <functional>
#include <set>
#include <limits>

namespace bpal
{
	//==========================================================================
	// Construction
	//==========================================================================

	int FLayeredGraph::AddVertex(float Width, float Height)
	{
		FLayeredVertex V;
		V.Id = (int)Vertices_.size();
		V.Width = Width > 0.f ? Width : 1.f;
		V.Height = Height > 0.f ? Height : 1.f;
		Vertices_.push_back(V);
		return V.Id;
	}

	void FLayeredGraph::AddEdge(int From, int To, float PortFromY, float PortToY, bool bExec)
	{
		if (From < 0 || To < 0 || From >= (int)Vertices_.size() || To >= (int)Vertices_.size() || From == To)
		{
			return;
		}
		FLayeredEdge E;
		E.From = From; E.To = To;
		E.PortFromY = PortFromY; E.PortToY = PortToY;
		E.bExec = bExec;
		Edges_.push_back(E);
	}

	void FLayeredGraph::SetSeedRank(int Vertex, int Rank)
	{
		if (Vertex < 0 || Vertex >= (int)Vertices_.size()) return;
		Vertices_[Vertex].Rank = Rank;
		bSeedRanks_ = true;
	}

	void FLayeredGraph::SetPullTowardConsumers(int Vertex)
	{
		if (Vertex < 0 || Vertex >= (int)Vertices_.size()) return;
		Vertices_[Vertex].bPullRight = true;
	}

	float FLayeredGraph::PortFromYOf(const FLayeredEdge& E) const
	{
		return E.PortFromY >= 0.f ? E.PortFromY : Vertices_[E.From].Height * 0.5f;
	}
	float FLayeredGraph::PortToYOf(const FLayeredEdge& E) const
	{
		return E.PortToY >= 0.f ? E.PortToY : Vertices_[E.To].Height * 0.5f;
	}

	const std::vector<int>& FLayeredGraph::GetEdgeChain(int OriginalEdgeIndex) const
	{
		static const std::vector<int> Empty;
		if (OriginalEdgeIndex < 0 || OriginalEdgeIndex >= (int)EdgeChains_.size())
		{
			return Empty;
		}
		return EdgeChains_[OriginalEdgeIndex];
	}

	//==========================================================================
	// Solve
	//==========================================================================

	void FLayeredGraph::Solve()
	{
		if (Vertices_.empty()) return;
		OriginalEdgeCount_ = (int)Edges_.size();
		EdgeChains_.assign(OriginalEdgeCount_, {});

		AssignRanks();
		InsertDummies();
		OrderWithinRanks();
		AssignCoordinates();
	}

	//==========================================================================
	// Stage 1: ranking (longest-path, with DFS cycle-breaking)
	//==========================================================================

	void FLayeredGraph::AssignRanks()
	{
		const int N = (int)Vertices_.size();

		// Adapter supplied ranks: normalize to a 0-based contiguous-min and compute the rank count,
		// skipping our own longest-path layering entirely.
		if (bSeedRanks_)
		{
			int MinR = 0x7fffffff, MaxR = -0x7fffffff;
			for (const auto& V : Vertices_) { MinR = std::min(MinR, V.Rank); MaxR = std::max(MaxR, V.Rank); }
			if (MinR > MaxR) { MinR = 0; MaxR = 0; }
			for (auto& V : Vertices_) V.Rank -= MinR;
			NumRanks_ = (MaxR - MinR) + 1;
			return;
		}

		// Build out-adjacency over the ORIGINAL edges (by edge index).
		std::vector<std::vector<int>> Out(N);   // vertex -> list of edge indices leaving it
		for (int e = 0; e < (int)Edges_.size(); ++e)
		{
			Out[Edges_[e].From].push_back(e);
		}

		// DFS cycle-break: any edge pointing at a vertex currently on the recursion stack is a
		// back-edge; orient it backwards (To->From) for ranking so the graph becomes a DAG.
		// We record orientation per edge as a sign; the original From/To (and ports) are untouched.
		std::vector<int> EdgeDir(Edges_.size(), +1);  // +1 = use From->To, -1 = reversed (To->From)
		std::vector<uint8_t> State(N, 0);             // 0 unvisited, 1 on-stack, 2 done

		// iterative DFS to avoid stack overflow on deep graphs
		struct FFrame { int v; size_t i; };
		for (int s = 0; s < N; ++s)
		{
			if (State[s] != 0) continue;
			std::vector<FFrame> Stack;
			Stack.push_back({s, 0});
			State[s] = 1;
			while (!Stack.empty())
			{
				FFrame& F = Stack.back();
				if (F.i < Out[F.v].size())
				{
					const int e = Out[F.v][F.i++];
					const int w = Edges_[e].To;
					if (State[w] == 1)
					{
						EdgeDir[e] = -1;   // back-edge: reverse for ranking
					}
					else if (State[w] == 0)
					{
						State[w] = 1;
						Stack.push_back({w, 0});
					}
				}
				else
				{
					State[F.v] = 2;
					Stack.pop_back();
				}
			}
		}

		// Oriented adjacency for longest-path layering.
		std::vector<std::vector<int>> OrientedSucc(N);
		std::vector<int> InDeg(N, 0);
		for (int e = 0; e < (int)Edges_.size(); ++e)
		{
			int a = Edges_[e].From, b = Edges_[e].To;
			if (EdgeDir[e] < 0) std::swap(a, b);
			OrientedSucc[a].push_back(b);
			InDeg[b]++;
		}

		// Kahn topological order.
		std::vector<int> Topo;
		Topo.reserve(N);
		std::vector<int> Queue;
		for (int v = 0; v < N; ++v) if (InDeg[v] == 0) Queue.push_back(v);
		std::vector<int> InDegLeft = InDeg;
		size_t qi = 0;
		while (qi < Queue.size())
		{
			const int v = Queue[qi++];
			Topo.push_back(v);
			for (int w : OrientedSucc[v])
			{
				if (--InDegLeft[w] == 0) Queue.push_back(w);
			}
		}
		// Any vertices not reached (shouldn't happen after cycle-break) get appended.
		if ((int)Topo.size() < N)
		{
			std::vector<uint8_t> InTopo(N, 0);
			for (int v : Topo) InTopo[v] = 1;
			for (int v = 0; v < N; ++v) if (!InTopo[v]) Topo.push_back(v);
		}

		// Longest-path rank: rank(v) = max(rank(pred)+1), sources = 0.
		for (auto& V : Vertices_) V.Rank = 0;
		for (int v : Topo)
		{
			for (int w : OrientedSucc[v])
			{
				if (Vertices_[w].Rank < Vertices_[v].Rank + 1)
				{
					Vertices_[w].Rank = Vertices_[v].Rank + 1;
				}
			}
		}

		// Pull flagged source vertices (no incoming edges) rightward to just-left-of their nearest
		// consumer, so e.g. a variable Get sits beside what it feeds instead of at column 0. Only
		// applies to true sources — any vertex with a provider keeps its longest-path rank.
		std::vector<int> InDegAll(N, 0);
		std::vector<std::vector<int>> SuccAll(N);
		for (const auto& E : Edges_) { InDegAll[E.To]++; SuccAll[E.From].push_back(E.To); }
		for (int pass = 0; pass < N; ++pass)
		{
			bool bChanged = false;
			for (int v = 0; v < N; ++v)
			{
				if (!Vertices_[v].bPullRight || InDegAll[v] != 0 || SuccAll[v].empty()) continue;
				int MinSucc = 0x7fffffff;
				for (int w : SuccAll[v]) MinSucc = std::min(MinSucc, Vertices_[w].Rank);
				if (MinSucc != 0x7fffffff && Vertices_[v].Rank != MinSucc - 1)
				{
					Vertices_[v].Rank = MinSucc - 1;
					bChanged = true;
				}
			}
			if (!bChanged) break;
		}

		// Normalize so the minimum rank is 0 (pull-right can push a source to -1) and count ranks.
		int MinRank = 0x7fffffff, MaxRank = -0x7fffffff;
		for (const auto& V : Vertices_) { MinRank = std::min(MinRank, V.Rank); MaxRank = std::max(MaxRank, V.Rank); }
		if (MinRank > MaxRank) { MinRank = 0; MaxRank = 0; }
		for (auto& V : Vertices_) V.Rank -= MinRank;
		NumRanks_ = (MaxRank - MinRank) + 1;
	}

	//==========================================================================
	// Stage 2: dummy nodes on edges spanning more than one rank
	//==========================================================================

	void FLayeredGraph::InsertDummies()
	{
		std::vector<FLayeredEdge> NewEdges;
		NewEdges.reserve(Edges_.size());

		const int OrigCount = OriginalEdgeCount_;
		for (int e = 0; e < (int)Edges_.size(); ++e)
		{
			const FLayeredEdge E = Edges_[e];
			const int r0 = Vertices_[E.From].Rank;
			const int r1 = Vertices_[E.To].Rank;
			const int span = std::abs(r1 - r0);

			if (span <= 1)
			{
				NewEdges.push_back(E);
				continue;
			}

			// Build a dummy chain across the intermediate ranks. Walk from r0 toward r1.
			const int step = (r1 > r0) ? +1 : -1;
			int prevVertex = E.From;
			float prevPort = PortFromYOf(E);              // port on the source end (real node)
			// emplace_back() returned void before C++17; use two-step for compatibility.
			if (e >= OrigCount) EdgeChains_.emplace_back();
			std::vector<int>& Chain = EdgeChains_[e];

			for (int r = r0 + step; r != r1; r += step)
			{
				const int d = AddVertex(Config.DummyWidth, Config.DummyHeight);
				Vertices_[d].bIsDummy = true;
				Vertices_[d].Rank = r;
				Chain.push_back(d);

				FLayeredEdge Seg;
				Seg.From = prevVertex; Seg.To = d;
				Seg.PortFromY = prevPort;                 // align to whatever the previous end wants
				Seg.PortToY = -1.f;                       // dummy center
				Seg.bExec = E.bExec;
				NewEdges.push_back(Seg);

				prevVertex = d;
				prevPort = -1.f;                          // subsequent segments leave the dummy center
			}

			// Final segment into the real destination.
			FLayeredEdge Last;
			Last.From = prevVertex; Last.To = E.To;
			Last.PortFromY = prevPort;
			Last.PortToY = PortToYOf(E);
			Last.bExec = E.bExec;
			NewEdges.push_back(Last);
		}

		Edges_.swap(NewEdges);
	}

	//==========================================================================
	// Stage 3: ordering within ranks (median heuristic, alternating sweeps)
	//==========================================================================

	std::vector<std::vector<int>> FLayeredGraph::BuildRankOrders() const
	{
		std::vector<std::vector<int>> RankOrders(NumRanks_);
		std::vector<int> ByVertex(Vertices_.size());
		for (int v = 0; v < (int)Vertices_.size(); ++v) ByVertex[v] = v;
		// stable sort by (rank, order)
		std::sort(ByVertex.begin(), ByVertex.end(), [&](int a, int b)
		{
			if (Vertices_[a].Rank != Vertices_[b].Rank) return Vertices_[a].Rank < Vertices_[b].Rank;
			return Vertices_[a].Order < Vertices_[b].Order;
		});
		for (int v : ByVertex) RankOrders[Vertices_[v].Rank].push_back(v);
		return RankOrders;
	}

	// Count crossings between two adjacent ranks given the current order index of every vertex.
	int CountCrossingsBetween(const std::vector<int>& Upper, const std::vector<int>& Lower,
	                          const std::vector<FLayeredEdge>& Edges,
	                          const std::vector<int>& OrderOfVertex)
	{
		// Collect edges touching this rank pair as (upperPos, lowerPos), regardless of direction.
		std::vector<uint8_t> InUpper(OrderOfVertex.size(), 0), InLower(OrderOfVertex.size(), 0);
		for (int v : Upper) InUpper[v] = 1;
		for (int v : Lower) InLower[v] = 1;

		std::vector<std::pair<int,int>> Pairs;
		for (const auto& E : Edges)
		{
			int a = E.From, b = E.To;
			if (InUpper[a] && InLower[b]) Pairs.push_back({OrderOfVertex[a], OrderOfVertex[b]});
			else if (InUpper[b] && InLower[a]) Pairs.push_back({OrderOfVertex[b], OrderOfVertex[a]});
		}
		// Sort by upper position; count inversions in lower positions (= crossings).
		std::sort(Pairs.begin(), Pairs.end(), [](const std::pair<int,int>& x, const std::pair<int,int>& y)
		{
			if (x.first != y.first) return x.first < y.first;
			return x.second < y.second;
		});
		int Crossings = 0;
		for (size_t i = 0; i < Pairs.size(); ++i)
			for (size_t j = i + 1; j < Pairs.size(); ++j)
				if (Pairs[i].second > Pairs[j].second) ++Crossings;
		return Crossings;
	}

	int FLayeredGraph::CountCrossings(const std::vector<std::vector<int>>& RankOrders) const
	{
		std::vector<int> OrderOfVertex(Vertices_.size(), 0);
		for (const auto& Rank : RankOrders)
			for (int pos = 0; pos < (int)Rank.size(); ++pos)
				OrderOfVertex[Rank[pos]] = pos;

		int Total = 0;
		for (int r = 0; r + 1 < NumRanks_; ++r)
			Total += CountCrossingsBetween(RankOrders[r], RankOrders[r + 1], Edges_, OrderOfVertex);
		return Total;
	}

	void FLayeredGraph::OrderWithinRanks()
	{
		const int N = (int)Vertices_.size();

		// Union-find to group disconnected components so they don't interleave.
		std::vector<int> UF(N);
		std::iota(UF.begin(), UF.end(), 0);
		std::function<int(int)> Find = [&](int x){ while (UF[x] != x){ UF[x] = UF[UF[x]]; x = UF[x]; } return x; };
		for (const auto& E : Edges_) { int a = Find(E.From), b = Find(E.To); if (a != b) UF[a] = b; }

		// Initial order: group by component, then by id, bucketed per rank.
		std::vector<int> ByVertex(N);
		std::iota(ByVertex.begin(), ByVertex.end(), 0);
		std::sort(ByVertex.begin(), ByVertex.end(), [&](int a, int b)
		{
			const int ca = Find(a), cb = Find(b);
			if (ca != cb) return ca < cb;
			return a < b;
		});
		std::vector<int> NextPos(NumRanks_, 0);
		for (int v : ByVertex) Vertices_[v].Order = NextPos[Vertices_[v].Rank]++;

		// Adjacency by rank-neighbor for median computation.
		// For each vertex: edges to rank-1 (uppers) and rank+1 (lowers) as neighbor vertex ids.
		auto NeighborsInRank = [&](int v, int targetRank)
		{
			std::vector<int> Result;
			for (const auto& E : Edges_)
			{
				if (E.From == v && Vertices_[E.To].Rank == targetRank) Result.push_back(E.To);
				else if (E.To == v && Vertices_[E.From].Rank == targetRank) Result.push_back(E.From);
			}
			return Result;
		};

		std::vector<std::vector<int>> RankOrders = BuildRankOrders();
		int BestCrossings = CountCrossings(RankOrders);
		std::vector<int> BestOrder(N);
		for (int v = 0; v < N; ++v) BestOrder[v] = Vertices_[v].Order;

		auto MedianOf = [](std::vector<float>& vals) -> float
		{
			if (vals.empty()) return -1.f;
			std::sort(vals.begin(), vals.end());
			const size_t m = vals.size() / 2;
			return (vals.size() % 2) ? vals[m] : 0.5f * (vals[m - 1] + vals[m]);
		};

		for (int sweep = 0; sweep < Config.OrderingSweeps; ++sweep)
		{
			const bool bDown = (sweep % 2) == 0;   // down: order rank r by neighbors in r-1
			if (bDown)
			{
				for (int r = 1; r < NumRanks_; ++r)
				{
					std::vector<int>& Rank = RankOrders[r];
					std::vector<float> Med(Rank.size());
					for (size_t i = 0; i < Rank.size(); ++i)
					{
						std::vector<float> vals;
						for (int nb : NeighborsInRank(Rank[i], r - 1)) vals.push_back((float)Vertices_[nb].Order);
						float m = MedianOf(vals);
						Med[i] = (m < 0.f) ? (float)Vertices_[Rank[i]].Order : m;
					}
					std::vector<size_t> idx(Rank.size());
					std::iota(idx.begin(), idx.end(), 0);
					std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b){ return Med[a] < Med[b]; });
					std::vector<int> NewRank(Rank.size());
					for (size_t i = 0; i < idx.size(); ++i) NewRank[i] = Rank[idx[i]];
					Rank = NewRank;
					for (int pos = 0; pos < (int)Rank.size(); ++pos) Vertices_[Rank[pos]].Order = pos;
				}
			}
			else
			{
				for (int r = NumRanks_ - 2; r >= 0; --r)
				{
					std::vector<int>& Rank = RankOrders[r];
					std::vector<float> Med(Rank.size());
					for (size_t i = 0; i < Rank.size(); ++i)
					{
						std::vector<float> vals;
						for (int nb : NeighborsInRank(Rank[i], r + 1)) vals.push_back((float)Vertices_[nb].Order);
						float m = MedianOf(vals);
						Med[i] = (m < 0.f) ? (float)Vertices_[Rank[i]].Order : m;
					}
					std::vector<size_t> idx(Rank.size());
					std::iota(idx.begin(), idx.end(), 0);
					std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b){ return Med[a] < Med[b]; });
					std::vector<int> NewRank(Rank.size());
					for (size_t i = 0; i < idx.size(); ++i) NewRank[i] = Rank[idx[i]];
					Rank = NewRank;
					for (int pos = 0; pos < (int)Rank.size(); ++pos) Vertices_[Rank[pos]].Order = pos;
				}
			}

			const int Crossings = CountCrossings(RankOrders);
			if (Crossings < BestCrossings)
			{
				BestCrossings = Crossings;
				for (int v = 0; v < N; ++v) BestOrder[v] = Vertices_[v].Order;
				if (Crossings == 0) break;
			}
		}

		for (int v = 0; v < N; ++v) Vertices_[v].Order = BestOrder[v];
	}

	//==========================================================================
	// Stage 4: coordinate assignment
	//   X = cumulative rank-column widths.
	//   Y = port-aware priority placement via weighted-L2 isotonic regression (PAV) per rank.
	//==========================================================================

	void FLayeredGraph::AssignCoordinates()
	{
		std::vector<std::vector<int>> RankOrders = BuildRankOrders();

		// ---- X: each rank is a column; left edges accumulate by max width + spacing. ----
		std::vector<float> ColX(NumRanks_, 0.f);
		float Cursor = 0.f;
		for (int r = 0; r < NumRanks_; ++r)
		{
			ColX[r] = Cursor;
			float MaxW = 0.f;
			for (int v : RankOrders[r]) MaxW = std::max(MaxW, Vertices_[v].Width);
			Cursor += MaxW + Config.RankSpacingX;
		}
		for (int r = 0; r < NumRanks_; ++r)
			for (int v : RankOrders[r]) Vertices_[v].X = ColX[r];

		// ---- Y (cross axis): pin-aware Brandes-Köpf. ----
		AssignYBrandesKopf(RankOrders);
	}

	//==========================================================================
	// Brandes-Köpf cross-axis (Y) coordinate assignment, port-aware + exec-preferred.
	//
	// Adapted from "Fast and Simple Horizontal Coordinate Assignment" (Brandes & Köpf, GD 2002;
	// 2020 erratum arXiv:2008.01252). Our layers run left->right, so the coordinate computed here
	// is Y and "layers" are ranks. Two adaptations over textbook BK:
	//   * exec-preferred alignment — a vertex aligns to its median EXEC neighbor when it has one, so
	//     the white exec spine forms the straight blocks (data wires bend), like GF's edge weights.
	//   * port-aware blocks — aligning v to neighbor u carries an inner-shift = the difference of
	//     their pin offsets, so a block is collinear on the connecting PINS, not on node centers.
	// We run the two vertical passes (align-to-upper, align-to-lower), both with the same horizontal
	// bias, and average them. (The left/right horizontal bias uses an order-reversal + coordinate
	// negation that is incompatible with non-zero port offsets, so it is omitted; the up/down average
	// already balances the layout while keeping pins aligned.)
	//==========================================================================
	void FLayeredGraph::AssignYBrandesKopf(const std::vector<std::vector<int>>& RankOrders)
	{
		const int N = (int)Vertices_.size();
		if (N == 0) return;

		std::vector<int> Pos(N, 0);
		for (const auto& Rank : RankOrders)
			for (int p = 0; p < (int)Rank.size(); ++p) Pos[Rank[p]] = p;

		// Port offset of an edge endpoint measured from that node's CENTER.
		auto fromCenterFrom = [&](const FLayeredEdge& E){ return PortFromYOf(E) - Vertices_[E.From].Height * 0.5f; };
		auto fromCenterTo   = [&](const FLayeredEdge& E){ return PortToYOf(E)   - Vertices_[E.To].Height   * 0.5f; };

		// Adjacency to adjacent ranks. For a neighbor pair we store the offset `Off` such that the
		// port-straight relationship is centerY(v) = centerY(nbr) + Off.
		struct FNbr { int Id; float Off; bool bExec; };
		std::vector<std::vector<FNbr>> Up(N), Down(N);   // Up: neighbor in rank-1; Down: rank+1
		for (const auto& E : Edges_)
		{
			const int a = E.From, b = E.To;
			const int ra = Vertices_[a].Rank, rb = Vertices_[b].Rank;
			if (std::abs(ra - rb) != 1) continue;               // BK works on proper (1-rank) segments
			const int lo = (ra < rb) ? a : b;                   // smaller-rank endpoint (left)
			const int hi = (ra < rb) ? b : a;                   // larger-rank endpoint (right)
			const float portLo = (lo == E.From) ? fromCenterFrom(E) : fromCenterTo(E);
			const float portHi = (hi == E.From) ? fromCenterFrom(E) : fromCenterTo(E);
			// hi's upper neighbor is lo: centerY(hi) = centerY(lo) + (portLo - portHi)
			Up[hi].push_back({ lo, portLo - portHi, E.bExec });
			// lo's lower neighbor is hi: centerY(lo) = centerY(hi) + (portHi - portLo)
			Down[lo].push_back({ hi, portHi - portLo, E.bExec });
		}

		// ---- type-1 conflicts (non-inner segment crossing an inner/dummy-dummy segment) ----
		std::set<long long> Conflicts;
		auto key = [&](int up, int lo){ return (long long)up * N + lo; };
		for (int ri = 0; ri + 1 < NumRanks_; ++ri)
		{
			const std::vector<int>& Lupper = RankOrders[ri];
			const std::vector<int>& Llower = RankOrders[ri + 1];
			int k0 = 0, l = 0;
			const int nLow = (int)Llower.size();
			for (int l1 = 0; l1 < nLow; ++l1)
			{
				const int v = Llower[l1];
				int innerUpPos = -1;
				if (Vertices_[v].bIsDummy)
					for (const FNbr& nb : Up[v])
						if (Vertices_[nb.Id].bIsDummy) { innerUpPos = Pos[nb.Id]; break; }
				const bool bLast = (l1 == nLow - 1);
				if (bLast || innerUpPos >= 0)
				{
					const int k1 = bLast ? (int)Lupper.size() - 1 : innerUpPos;
					while (l <= l1)
					{
						const int vl = Llower[l];
						for (const FNbr& nb : Up[vl])
						{
							const int ku = Pos[nb.Id];
							if (ku < k0 || ku > k1) Conflicts.insert(key(nb.Id, vl));
						}
						++l;
					}
					k0 = k1;
				}
			}
		}

		// One BK pass for a given vertical direction; returns centerY per vertex.
		auto runOnce = [&](bool bToUpper) -> std::vector<float>
		{
			std::vector<int> Root(N), Align(N);
			std::vector<float> Inner(N, 0.f);
			for (int v = 0; v < N; ++v) { Root[v] = v; Align[v] = v; }

			for (int step = 0; step < NumRanks_; ++step)
			{
				const int ri = bToUpper ? step : (NumRanks_ - 1 - step);
				const std::vector<int>& L = RankOrders[ri];
				int prevPos = -1;   // left-biased: aligned neighbor position must strictly increase
				for (int k = 0; k < (int)L.size(); ++k)
				{
					const int v = L[k];
					const std::vector<FNbr>& AllN = bToUpper ? Up[v] : Down[v];
					if (AllN.empty()) continue;
					// exec-preferred: align along exec neighbours if any, else all.
					std::vector<FNbr> Nbrs;
					for (const FNbr& nb : AllN) if (nb.bExec) Nbrs.push_back(nb);
					if (Nbrs.empty()) Nbrs = AllN;
					std::sort(Nbrs.begin(), Nbrs.end(), [&](const FNbr& A, const FNbr& B){ return Pos[A.Id] < Pos[B.Id]; });

					const int d = (int)Nbrs.size();
					const int m0 = (d - 1) / 2, m1 = d / 2;   // lower / upper median
					for (int mm = m0; mm <= m1; ++mm)
					{
						if (Align[v] != v) break;             // already aligned this vertex
						const FNbr& nb = Nbrs[mm];
						const int up = bToUpper ? nb.Id : v;   // upper-rank vertex of this segment
						const int lo = bToUpper ? v : nb.Id;   // lower-rank vertex
						if (Pos[nb.Id] > prevPos && !Conflicts.count(key(up, lo)))
						{
							Align[nb.Id] = v;
							Root[v] = Root[nb.Id];
							Align[v] = Root[v];
							Inner[v] = Inner[nb.Id] + nb.Off;
							prevPos = Pos[nb.Id];
						}
					}
				}
			}

			// horizontal compaction (places each block root; members add their inner shift)
			std::vector<int> Sink(N);
			std::vector<float> Shift(N, std::numeric_limits<float>::max());
			std::vector<float> X(N, std::nanf(""));
			for (int v = 0; v < N; ++v) Sink[v] = v;

			std::function<void(int)> place = [&](int vRoot)
			{
				if (!std::isnan(X[vRoot])) return;
				X[vRoot] = 0.f;
				int w = vRoot;
				do
				{
					const int ri = Vertices_[w].Rank;
					const int k = Pos[w];
					if (k > 0)
					{
						const int predV = RankOrders[ri][k - 1];
						const int u = Root[predV];
						place(u);
						if (Sink[vRoot] == vRoot) Sink[vRoot] = Sink[u];
						const float baseSep = (Vertices_[predV].Height + Vertices_[w].Height) * 0.5f + Config.NodeSpacingY;
						const float sep = baseSep + Inner[predV] - Inner[w];   // port-aware separation
						if (Sink[vRoot] != Sink[u])
							Shift[Sink[u]] = std::min(Shift[Sink[u]], X[vRoot] - X[u] - sep);
						else
							X[vRoot] = std::max(X[vRoot], X[u] + sep);
					}
					w = Align[w];
				} while (w != vRoot);
			};
			for (int v = 0; v < N; ++v) if (Root[v] == v) place(v);

			std::vector<float> Center(N, 0.f);
			for (int v = 0; v < N; ++v)
			{
				float c = X[Root[v]];
				if (Shift[Sink[Root[v]]] < std::numeric_limits<float>::max()) c += Shift[Sink[Root[v]]];
				Center[v] = c + Inner[v];
			}
			return Center;
		};

		// Use the align-to-predecessor (up) pass only: each node aligns to its median exec UPPER
		// neighbor, so each event's Y propagates rightward into a straight exec spine and a sink
		// (e.g. Set Relative Location) anchors to its predecessor instead of floating. Averaging in
		// an align-to-consumer (down) pass drifts exec SINKS — which have no consumer to anchor to —
		// toward compaction-top, dragging the node and its providers far above the spine and bending
		// the exec wire. (Diagnosed v0.6.2: a sink landed ~700px above its Timeline trigger.)
		std::vector<float> Center = runOnce(true);

		float MinTop = std::numeric_limits<float>::max();
		for (int v = 0; v < N; ++v)
		{
			Vertices_[v].Y = Center[v] - Vertices_[v].Height * 0.5f;   // store top
			MinTop = std::min(MinTop, Vertices_[v].Y);
		}
		for (int v = 0; v < N; ++v) Vertices_[v].Y -= MinTop;       // normalize top to 0
	}
}
