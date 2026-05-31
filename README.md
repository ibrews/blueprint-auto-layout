# Blueprint Auto Layout

Pin-aware auto-layout for Unreal Engine Blueprint graphs. Right-click on empty graph space → **Auto Layout Graph** → the algorithm rearranges your nodes into a readable left-to-right execution flow.

![Before and after smart wire rerouting — flow wires bend around intervening nodes](Docs/before-after.png)

Four right-click actions:

- **Auto Layout Graph** — arranges the graph (default; doesn't add anything).
- **Auto Layout & Group Graph** — arranges it *and* wraps each event/function subtree in a comment box automatically named after its root, colored by keyword (Damage→red, Spawn→green, …).
- **Auto Layout (Route Wires)** — arranges it *and* inserts reroute (knot) nodes so flow/data wires bend around nodes they would otherwise cut straight across (the screenshot above).
- **Auto Layout, Group & Route** — all of the above: arrange, group into named comment boxes, and route wires.

Handles:

- Branches (`Branch`, `Switch`, multi-output exec nodes) — sibling paths stack vertically without overlap
- Pure (data-only) nodes — clustered into a reserved column to the left of their consumer, chained pure nodes included
- Multi-event graphs — each event/function root gets its own row
- Sequence nodes — laid out as sequential, not branching
- Reroute (knot) nodes — treated as wire bends, repositioned onto the wire they sit on instead of cluttering the flow
- Comment boxes — resized and repositioned to keep wrapping the nodes they contain after everything moves
- Variable-width nodes — true node sizes are used, not assumed averages
- Wire rerouting *(opt-in)* — flow/data wires that would cross an intervening node are split with reroute (knot) nodes that bend the wire above or below the obstacle

Built as a single editor module with no runtime cost. One undo step per layout.

📖 **Full documentation in the [Wiki](https://github.com/ibrews/blueprint-auto-layout/wiki)** — installation, usage, algorithm internals, configuration, troubleshooting, and roadmap.

## Install

1. Clone (or copy) this repo into `<YourProject>/Plugins/blueprint-auto-layout/`.
2. Right-click your `.uproject` → **Generate Project Files**.
3. Build the project.
4. Launch the editor. The plugin loads automatically.

## Usage

Right-click on empty space in any Blueprint / Animation Blueprint / Macro graph → **Layout →** and pick one of:

- **Auto Layout Graph** — rearranges the nodes.
- **Auto Layout & Group Graph** — rearranges the nodes, then creates a comment box around each event/function subtree, named after that root node (e.g. an event named `On Box Destroyed` → a comment titled "On Box Destroyed"). Existing comments are still re-wrapped; this only *adds* the per-subtree group boxes.
- **Auto Layout (Route Wires)** — rearranges the nodes, then inserts reroute (knot) nodes wherever a wire would cut straight across an intervening node, bending the wire above or below the obstacle (whichever side needs the smaller detour). Re-running is safe: the routing knots are tagged and regenerated each time, so they don't pile up.
- **Auto Layout, Group & Route** — does the layout, the per-subtree comment grouping, *and* the wire routing in one action.

Either way the change is wrapped in a single transaction — one `Ctrl+Z` undoes the entire layout. The two routing actions add nodes to the graph (the reroute knots); the plain and group-only actions never delete your nodes.

The grouping actions color each comment box by reading keywords in its root node's title, so a graph's groups read at a glance — `Damage`/`Hit`→red, `Destroy`/`Death`→dark red, `Spawn`/`Create`→green, `Begin`/`Init`→blue, `Tick`/`Update`→teal, `Input`/`Pressed`→amber, `Overlap`/`Collision`→violet. Names with no keyword get a stable color derived from the title (the same event name always gets the same color):

![Group comment boxes auto-colored by keyword](Docs/grouped-colored.png)

## Settings

**Editor Preferences → Plugins → Blueprint Auto Layout:**

- **Route wires by default** *(off by default)* — when on, the plain **Auto Layout Graph** and **Auto Layout & Group Graph** actions also reroute wires, so you don't have to reach for the dedicated routing actions. They always route regardless of this setting.
- **Comment color mode** — **Keyword (semantic)** (default) colors group comments by title keyword as described above; **Cycling palette** steps through a fixed palette instead.

## Engine support

Currently developed and verified against **Unreal Engine 5.7**.

Intended future support: 5.5, 5.6, 5.8 (the plugin uses only stable `UEdGraph` and `K2Node_*` public API, so the multi-version matrix should be straightforward — that's a separate verification pass).

## Things to Try

1. Open a messy event graph. Right-click empty space → **Auto Layout Graph**. Watch nodes snap to a clean flow.
2. Press **Ctrl+Z** once. The entire layout reverts — the action is a single undo step.
3. Open a graph with multiple events (`BeginPlay`, custom events, etc). Run auto-layout. Each event becomes its own row.
4. Add a `Branch` node mid-flow. Run auto-layout. The True/False paths stack vertically without overlap; the `Then` path takes the upper lane.
5. Open an Animation Blueprint's AnimGraph or Event Graph. The same **Auto Layout Graph** entry appears — the menu attaches to all `UEdGraphSchema_K2`-derived schemas.
6. Chain three Variable Get nodes feeding into a math expression. Run auto-layout. Each node in the chain positions correctly to the left of its consumer.
7. Wrap a group of nodes in a comment box, then drag them apart into a mess. Run auto-layout — the comment resizes and repositions to keep wrapping its nodes in their new spots.
8. Drop a reroute (knot) node into a wire. Run auto-layout — the knot lands on the wire as a bend instead of being treated as a node in the execution flow.
9. On a graph with several events, run **Auto Layout & Group Graph**. Each event's subtree is laid out and wrapped in its own comment box, auto-named after the event — no typing required.
10. Find a graph where a long flow wire skips past a few nodes (e.g. a `Sequence`'s second output reaching a far node). Run **Auto Layout (Route Wires)** — reroute knots appear and the wire bends below the nodes it used to cross. Run it again: the count of knots stays the same (re-runs don't accumulate). Press **Ctrl+Z** to undo the whole thing in one step.

## Algorithm overview

Layout proceeds in five phases:

1. **Build** the layout tree: classify nodes as exec/pure/branch/root (and tag reroute knots and comment boxes separately), measure each node's size — reading the **actual rendered size** from the open graph panel when available, falling back to a pin-based estimate — traverse exec flow from each root *tracing through reroute nodes*, collect pure-node providers for each consumer (including chained pure → pure → exec chains, also traced through knots), and record which nodes each comment box currently wraps.
2. **Measure** subtree heights from the leaves upward (in pixels), accounting for branch spacing.
3. **Assign** positions top-down: exec subtrees flow left-to-right; branches stack their children vertically; each consumer reserves a horizontal lane for its pure-node column so the column can't overlap the exec predecessor.
4. **Apply** the calculated positions to the actual `UEdGraphNode`s via a single transaction.
5. **Finish** with two cosmetic passes: drop each reroute (knot) node onto the midpoint of the wire it sits on, and resize/reposition each comment box to wrap its recorded members in their new positions.

When **Auto Layout & Group Graph** is used, one extra step runs afterward: for each root subtree (collected from the tree built in phase 1), a new comment box is created around its members, named after the root node's title, and colored by matching keywords in that title (with a stable hash-of-title fallback).

When a **Route Wires** action is used, a final phase runs after everything else: each direct node→node wire is tested against the rects of the nodes between its endpoints (a segment-vs-rectangle test), and any wire that would cut across an intervening node is split with reroute (knot) nodes that bend it above or below the obstacle — whichever side needs the smaller detour. Those knots are tagged so a re-run removes and regenerates them rather than accumulating.

Most configuration constants (paddings, default sizes, pure-node column limits, comment padding) live in `FBlueprintLayoutConfig` in `BlueprintAutoLayout.h` and require a source edit; the two most useful knobs — *route wires by default* and *comment color mode* — are exposed in **Editor Preferences → Plugins → Blueprint Auto Layout**.

## Status

v0.5.0 — public, MIT licensed. Not yet on Fab.

Known limitations:
- **Context menu only** — no hotkey or toolbar button yet. (A `Ctrl+Shift+L` hotkey was attempted in 0.2.0 but pulled in 0.2.1; it needs a proper command-list extender, planned for a later release.)
- No "layout selected nodes" — operates on the whole graph.
- No asset-action ("layout all graphs in this BP") — operates on the visible graph only.
- **Wire routing handles the common case** — one (or a chain of) reroute knot per crossed node, routed to one side. It doesn't yet do full channel-based edge routing, so very dense graphs may still have some wire overlap.
- Most configuration constants still require a source edit; only the two most useful knobs are exposed in Editor Preferences (see **Settings**).

## License

MIT. Copyright (c) 2026 Alex Coulombe.
