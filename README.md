# Blueprint Auto Layout

Pin-aware auto-layout for Unreal Engine Blueprint graphs. Press **Ctrl/Cmd+Shift+L** or use the **toolbar dropdown** → the algorithm rearranges your nodes into a readable left-to-right execution flow.

![A scrambled event graph cleaned up in a single Auto Layout pass](Docs/cleanup.png)

*One command on a randomly-scattered graph: each event lands on its own row with straight wires.* The wire handling up close:

![Before and after: straight execution spines with data providers clustered to their consumer](Docs/before-after.png)

**New in v0.6.0 — a real layered (Sugiyama) engine.** The layout is now computed by a proper layered-graph algorithm instead of a single-parent tree, so it handles the cases a tree can't model:

- **Cross-row connections, multi-consumer data, and long edges** no longer snake or hump. Nodes are ranked into columns by execution depth (data providers pulled to just left of what they feed), edges that span more than one column get **dummy waypoints** so they route straight through a reserved lane, and a crossing-minimization sweep orders each column.
- **Pin-aware Brandes-Köpf coordinate assignment** straightens the white execution spine on the actual pin Y — exec links drive the alignment, so the spine reads as one clean horizontal line and data wires bend to meet it (rather than the whole thing averaging into a diagonal).
- **No node ever lands on top of another** — columns reserve their own width and rows their own height.
- **New in v0.6.1 — long edges are knot-routed.** An edge that spans more than one column is rewired through reroute (knot) nodes along its reserved lane, so it draws as straight segments instead of one long curved spline. The knots are tagged and regenerated each layout, so they never accumulate (toggle in Settings).

The engine is implemented from the published papers (Sugiyama et al.; Brandes & Köpf 2002 + 2020 erratum) as a standalone, unit-tested core. Auto-grouping into named, keyword-colored comment boxes and opt-in knot rerouting remain on top of it.

## Actions

Toolbar dropdown (or keyboard shortcut):

- **Auto Layout Graph** — arranges the graph (straighten & move by default). Also bound to **Ctrl/Cmd+Shift+L**; the toolbar button runs this by default.
- **Auto Layout Selected** — arranges only the selected nodes, in place. Also bound to **Ctrl/Cmd+Shift+K**. (Greyed out in the dropdown when no nodes are selected.)
- **Auto Layout & Group Graph** — arranges it *and* wraps each event/function subtree in a comment box automatically named after its root, colored by keyword (Damage→red, Spawn→green, …).
- **Auto Layout (Route Wires)** — arranges it *and* inserts reroute (knot) nodes so any wires that still cross a node bend around it (the knot fallback).
- **Auto Layout, Group & Route** — all of the above.

## Handles

- **Branches** (`Branch`, `Switch`, multi-output exec nodes) — sibling paths stack vertically in their own lanes without overlap
- **Sequence nodes** — each `Then` output gets its own vertical lane (so wires to later outputs don't cross earlier subtrees)
- **Pure (data-only) nodes** — clustered into a reserved column to the left of their consumer, chained pure nodes included, and single-consumer providers nudged to straighten their wire
- **Multi-event graphs** — each event/function root gets its own row
- **Reroute (knot) nodes** — treated as wire bends, repositioned onto the wire (at pin height) instead of cluttering the flow
- **Comment boxes** — resized and repositioned to keep wrapping the nodes they contain after everything moves
- **Variable-width nodes** — true rendered node sizes are used, not assumed averages

Built as a single editor module with no runtime cost. One undo step per layout.

📖 **Full documentation in the [Wiki](https://github.com/ibrews/blueprint-auto-layout/wiki)** — installation, usage, algorithm internals, configuration, troubleshooting, and roadmap.

## Install

1. Clone (or copy) this repo into `<YourProject>/Plugins/blueprint-auto-layout/`.
2. Right-click your `.uproject` → **Generate Project Files**.
3. Build the project.
4. Launch the editor. The plugin loads automatically.

## Usage

In any Blueprint / Animation Blueprint / Macro graph:

- **Keyboard:** press **Ctrl/Cmd+Shift+L** to lay out the whole graph, or **Ctrl/Cmd+Shift+K** to lay out just the selected nodes. Both shortcuts are rebindable in **Editor Preferences → Keyboard Shortcuts → Blueprint Auto Layout**.
- **Toolbar:** click the **Auto Layout** combo button in the Blueprint editor toolbar to run Auto Layout Graph, or click the dropdown arrow to pick any of the five actions (see [Actions](#actions) above). Auto Layout Selected is greyed out when nothing is selected.

Either way the change is wrapped in a single transaction — one `Ctrl+Z` undoes the entire layout. The grouping and routing actions add nodes to the graph (group comment boxes / reroute knots); the plain, selected, and group-only actions never delete your nodes.

The grouping actions color each comment box by reading keywords in its root node's title, so a graph's groups read at a glance — `Damage`/`Hit`→red, `Destroy`/`Death`→dark red, `Spawn`/`Create`→green, `Begin`/`Init`→blue, `Tick`/`Update`→teal, `Input`/`Pressed`→amber, `Overlap`/`Collision`→violet. Names with no keyword get a stable color derived from the title:

![Group comment boxes auto-colored by keyword](Docs/grouped-colored.png)

## Settings

**Editor Preferences → Plugins → Blueprint Auto Layout:**

- **Use layered (Sugiyama) engine** *(default: on)* — the v0.6.0 layered-graph engine (ranking, dummy waypoints, crossing minimization, pin-aware Brandes-Köpf). Turn off to fall back to the original single-parent tree layout.
- **Knot-route long edges** *(default: on, layered engine only)* — rewire multi-column edges through reroute knots along their reserved lane so they draw as straight segments. Turn off to leave long edges as plain (curved) wires.
- **Wire handling** *(default: Straighten & move nodes)* — how the plain layout actions (and the shortcut) handle wires:
  - **Straighten & move nodes** — keep wires straight and move nodes out of the way (Sequence lanes + pin-aligned data wires).
  - **Reroute with knots** — keep nodes put and bend wires around obstacles with reroute knots.
  - **Off** — lay out nodes but don't straighten, re-lane, or reroute any wires.
  - (The dedicated **Auto Layout (Route Wires)** menu actions always insert knots regardless of this setting.)
- **Pin-align tolerance** *(default 120)* — the maximum distance a node may be nudged vertically to line up a connected pin into a straight wire. Lower = nodes stay closer to their flow position (more wires left slightly diagonal); higher = more wires straightened.
- **Comment color mode** — **Keyword (semantic)** (default) colors group comments by title keyword; **Cycling palette** steps through a fixed palette instead.
- **Spacing** — horizontal, vertical, branch/lane, and event spacing (graph units) for tuning layout density.

## Engine support

Verified on **UE 4.27.2, 5.4.4, 5.5.4, 5.6.1, 5.7.4, and 5.8.0 (Win64)**. Each version was compiled clean with `RunUAT BuildPlugin`.

UE 5.2 fails to compile on machines with MSVC 14.40+ due to a known incompatibility in UE 5.2's own engine headers (`ConcurrentLinearAllocator.h`) — not a plugin issue.
UE 5.3 is not covered (not installed on the verification machine).

## Things to Try

1. Open a messy event graph. Press **Ctrl/Cmd+Shift+L** (or click the **Auto Layout** toolbar button). Watch nodes snap to a clean flow.
2. Press **Ctrl+Z** once. The entire layout reverts — the action is a single undo step.
3. Find a graph with a `Sequence` node. Run auto-layout. Each `Then` output now gets its own vertical lane, so the wire to a later output no longer cuts across an earlier output's nodes.
4. Find a node fed by a variable `Get`. Run auto-layout — the `Get` is nudged so its wire becomes a straight horizontal into the consumer's pin, while staying clear of the execution wire above it.
5. Select a handful of nodes, then press **Ctrl/Cmd+Shift+K** (or open the **Auto Layout** toolbar dropdown → **Auto Layout Selected**). Only those nodes are arranged, in place; the rest of the graph is untouched.
6. Open a graph with multiple events (`BeginPlay`, custom events, etc). Run auto-layout. Each event becomes its own row.
7. Add a `Branch` node mid-flow. Run auto-layout. The True/False paths stack vertically in their own lanes; the `Then` path takes the upper lane.
8. Open an Animation Blueprint's Event Graph. The same actions and shortcut work — the plugin attaches to all `UEdGraphSchema_K2`-derived schemas.
9. On a graph with several events, run **Auto Layout & Group Graph**. Each event's subtree is laid out and wrapped in its own comment box, auto-named after the event — no typing required.
10. Switch **Editor Preferences → Plugins → Blueprint Auto Layout → Wire handling** to **Reroute with knots**, then run **Auto Layout Graph** on a dense graph: instead of moving nodes, wires that cross a node are bent around it with reroute knots. Run it again — the knot count stays the same (re-runs don't accumulate). Switch back to **Straighten & move nodes** for the default behavior.

## Algorithm overview

Layout is a **layered (Sugiyama-style) pipeline** on an engine-agnostic core (no Unreal types, so it is unit-tested standalone), fed by an Unreal adapter:

1. **Build & measure** — classify nodes as exec/pure/branch/sequence/root (tagging reroute knots and comment boxes separately), read each node's **actual rendered size** from the open graph panel when available, gather every link (tracing through reroute knots to the real pins on the far side), and record which nodes each comment box currently wraps.
2. **Rank** — assign each node a column by longest-path over all links, so execution depth flows left-to-right; pure data sources (e.g. a variable `Get`) are pulled right to sit just left of what they feed.
3. **Dummies** — any edge spanning more than one column is split into a chain of dummy waypoints, reserving a straight lane so long wires don't cut across nodes.
4. **Order** — a median/barycenter sweep orders the nodes within each column to minimize wire crossings (components kept separate).
5. **Coordinates** — X from cumulative column widths; Y from **pin-aware Brandes-Köpf** — vertices align to their median *exec* neighbor on the connecting pin Y, forming straight blocks (the white spine), compacted with per-row minimum separation so nothing overlaps.
6. **Apply & finish** — write positions in a single transaction, then drop each reroute knot onto its wire (at pin height) and resize/reposition each comment box around its members.

When **Auto Layout & Group Graph** is used, a new comment box is then created around each root subtree, named after the root node's title and colored by matching keywords. When a **Route Wires** action is used, a final phase inserts reroute (knot) nodes on any wire that still crosses an intervening node; those knots are tagged so a re-run regenerates rather than accumulates.

The legacy single-parent tree packer is retained as a fallback (toggle in Settings). Spacing, the wire-handling mode, the pin-align tolerance, and comment color mode are exposed in **Editor Preferences → Plugins → Blueprint Auto Layout**; the remaining tuning constants live in `FBlueprintLayoutConfig` in `BlueprintAutoLayout.h`.

## Status

v0.6.7 — public, MIT licensed. Not yet on Fab.

**v0.6.7 — toolbar combo dropdown.** The single **Auto Layout** toolbar button is now a split combo button. Left-clicking still runs Auto Layout Graph; clicking the arrow opens a dropdown with all five actions — Auto Layout Graph, Auto Layout Selected (greyed out when nothing is selected), Auto Layout & Group Graph, Auto Layout (Route Wires), and Auto Layout, Group & Route.

**v0.6.6 — UE 4.27 support.** Added UE 4.27 compatibility guards and fixed a self-referential `BPAL_STYLE_SETNAME` macro that broke UE 5.x builds. Verified on UE 4.27.2, 5.4.4, 5.5.4, 5.6.1, 5.7.4, and 5.8.0 Win64.

**v0.6.5 — UE 5.4–5.8 verification.** Added version guards for `ResizeNode`/`BuildSettingsVersion` API differences across engine versions. Verified on UE 5.4.4, 5.5.4, 5.6.1, 5.7.4, and 5.8.0 Win64.

**v0.6.4 — straight first wire off each event.** Root events are the only un-anchored nodes in the layered result, so each is now snapped vertically to line its exec-output pin up with the first node it triggers — removing the slight downward slope that used to come off an event. (Where two events converge on one node, the merge still slopes for one of them — that's unavoidable.)

**v0.6.3 — refreshed documentation screenshots** to show the current layered-engine output (no functional change).

**v0.6.2 — coordinate fix.** A node fed by a long data chain (e.g. `Set Relative Location`, driven by a `Timeline` and a `float × float`) used to land far above its trigger, with the execution wire sweeping up to reach it. The Y assignment now anchors the execution spine to each node's predecessor (so a sink sits on its trigger's lane, exec wire straight) and pulls single-consumer data providers — including chained ones — onto the consumer's pins, so a node and its variables cluster together instead of the variables floating high.

Known limitations:
- **Long (multi-column) edges are routed through reroute knots** (v0.6.1) so they draw as straight segments rather than one curved spline. Pin positions feeding the lanes are *estimated* (not read from the live widget), so on complex multi-pin nodes (e.g. Timeline) a knotted lane can sit slightly off the exact pin and leave a gentle bend.
- **The layered engine ranks by longest path over all links** (no network-simplex balancing yet), so data-heavy graphs can be wider than strictly necessary; the exec spine stays straight regardless.
- No asset-action ("layout all graphs in this BP") — operates on the visible graph only.
- Material/Niagara/Behavior-Tree graphs aren't handled yet — Blueprint graphs only.

## License

MIT. Copyright (c) 2026 Alex Coulombe.
