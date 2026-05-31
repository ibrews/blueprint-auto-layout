// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BPALLayeredLayout.cpp - Engine-agnostic layered (Sugiyama) layout core. See BPALLayeredLayout.h.

#include "BPALLayeredLayout.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <functional>

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

		int MaxRank = 0;
		for (const auto& V : Vertices_) MaxRank = std::max(MaxRank, V.Rank);
		NumRanks_ = MaxRank + 1;
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
			std::vector<int>& Chain = (e < OrigCount) ? EdgeChains_[e] : EdgeChains_.emplace_back();

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
		const int N = (int)Vertices_.size();
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

		// ---- Y: initial stacking within each rank in Order. ----
		for (int r = 0; r < NumRanks_; ++r)
		{
			float Y = 0.f;
			for (int v : RankOrders[r])
			{
				Vertices_[v].Y = Y;
				Y += Vertices_[v].Height + Config.NodeSpacingY;
			}
		}

		// Priority of a vertex: dummies pulled hardest (straight long edges); real by incident degree.
		std::vector<float> Priority(N, 0.f);
		std::vector<int> Degree(N, 0);
		for (const auto& E : Edges_) { Degree[E.From]++; Degree[E.To]++; }
		for (int v = 0; v < N; ++v)
			Priority[v] = Vertices_[v].bIsDummy ? 1000.f : std::max(1, Degree[v]) * 1.f;

		// For a vertex v in rank r, the desired Y aligning its PORTS to neighbors in `targetRank`.
		auto DesiredY = [&](int v, int targetRank) -> float
		{
			std::vector<float> wants;
			for (const auto& E : Edges_)
			{
				if (E.From == v && Vertices_[E.To].Rank == targetRank)
				{
					const float selfPort = PortFromYOf(E);
					const float nbPort = PortToYOf(E);
					wants.push_back(Vertices_[E.To].Y + nbPort - selfPort);
				}
				else if (E.To == v && Vertices_[E.From].Rank == targetRank)
				{
					const float selfPort = PortToYOf(E);
					const float nbPort = PortFromYOf(E);
					wants.push_back(Vertices_[E.From].Y + nbPort - selfPort);
				}
			}
			if (wants.empty()) return std::nanf("");
			std::sort(wants.begin(), wants.end());
			const size_t m = wants.size() / 2;
			return (wants.size() % 2) ? wants[m] : 0.5f * (wants[m - 1] + wants[m]);
		};

		// Weighted-L2 isotonic regression (Pool Adjacent Violators) of s_i toward target_i,
		// nondecreasing, with the substituted variable s_i = y_i - C_i (C = cumulative min offset).
		auto PlaceRankToward = [&](std::vector<int>& Rank, const std::vector<float>& TargetY, const std::vector<float>& Weight)
		{
			const int n = (int)Rank.size();
			if (n == 0) return;
			// cumulative minimum offsets C_i
			std::vector<float> C(n, 0.f);
			for (int i = 1; i < n; ++i)
				C[i] = C[i - 1] + Vertices_[Rank[i - 1]].Height + Config.NodeSpacingY;
			// substituted targets t_i = target_i - C_i
			std::vector<float> t(n);
			for (int i = 0; i < n; ++i) t[i] = TargetY[i] - C[i];

			// PAV blocks: value (weighted mean), weight, count
			struct FBlock { float val; float w; int count; };
			std::vector<FBlock> Blocks;
			for (int i = 0; i < n; ++i)
			{
				float w = std::max(Weight[i], 1e-3f);
				FBlock b{ t[i], w, 1 };
				Blocks.push_back(b);
				while (Blocks.size() > 1 && Blocks[Blocks.size() - 2].val > Blocks.back().val)
				{
					FBlock top = Blocks.back(); Blocks.pop_back();
					FBlock& prev = Blocks.back();
					const float tw = prev.w + top.w;
					prev.val = (prev.val * prev.w + top.val * top.w) / tw;
					prev.w = tw;
					prev.count += top.count;
				}
			}
			// expand
			std::vector<float> s(n);
			int i = 0;
			for (const FBlock& b : Blocks)
				for (int k = 0; k < b.count; ++k) s[i++] = b.val;
			for (int j = 0; j < n; ++j) Vertices_[Rank[j]].Y = s[j] + C[j];
		};

		for (int sweep = 0; sweep < Config.CoordSweeps; ++sweep)
		{
			const bool bDown = (sweep % 2) == 0;
			if (bDown)
			{
				for (int r = 1; r < NumRanks_; ++r)
				{
					std::vector<int>& Rank = RankOrders[r];
					std::vector<float> Tgt(Rank.size()), W(Rank.size());
					for (size_t i = 0; i < Rank.size(); ++i)
					{
						const float d = DesiredY(Rank[i], r - 1);
						if (std::isnan(d)) { Tgt[i] = Vertices_[Rank[i]].Y; W[i] = 1e-3f; }
						else { Tgt[i] = d; W[i] = Priority[Rank[i]]; }
					}
					PlaceRankToward(Rank, Tgt, W);
				}
			}
			else
			{
				for (int r = NumRanks_ - 2; r >= 0; --r)
				{
					std::vector<int>& Rank = RankOrders[r];
					std::vector<float> Tgt(Rank.size()), W(Rank.size());
					for (size_t i = 0; i < Rank.size(); ++i)
					{
						const float d = DesiredY(Rank[i], r + 1);
						if (std::isnan(d)) { Tgt[i] = Vertices_[Rank[i]].Y; W[i] = 1e-3f; }
						else { Tgt[i] = d; W[i] = Priority[Rank[i]]; }
					}
					PlaceRankToward(Rank, Tgt, W);
				}
			}
		}
	}
}
