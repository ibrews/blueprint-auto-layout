# Blueprint Auto Layout

Pin-aware auto-layout for Unreal Engine Blueprint graphs. Right-click on empty graph space → **Auto Layout Graph** → the algorithm rearranges your nodes into a readable left-to-right execution flow.

![Before and after running Auto Layout Graph](Docs/before-after.png)

Handles:

- Branches (`Branch`, `Switch`, multi-output exec nodes) — sibling paths stack vertically without overlap
- Pure (data-only) nodes — clustered into a reserved column to the left of their consumer, chained pure nodes included
- Multi-event graphs — each event/function root gets its own row
- Sequence nodes — laid out as sequential, not branching
- Reroute (knot) nodes — treated as wire bends, repositioned onto the wire they sit on instead of cluttering the flow
- Comment boxes — resized and repositioned to keep wrapping the nodes they contain after everything moves
- Variable-width nodes — true node sizes are used, not assumed averages

Built as a single editor module with no runtime cost. One undo step per layout.

📖 **Full documentation in the [Wiki](https://github.com/ibrews/blueprint-auto-layout/wiki)** — installation, usage, algorithm internals, configuration, troubleshooting, and roadmap.

## Install

1. Clone (or copy) this repo into `<YourProject>/Plugins/blueprint-auto-layout/`.
2. Right-click your `.uproject` → **Generate Project Files**.
3. Build the project.
4. Launch the editor. The plugin loads automatically.

## Usage

Right-click on empty space in any Blueprint / Animation Blueprint / Macro graph → **Layout → Auto Layout Graph**.

The change is wrapped in a single transaction — one `Ctrl+Z` undoes the entire layout.

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

## Algorithm overview

Layout proceeds in five phases:

1. **Build** the layout tree: classify nodes as exec/pure/branch/root (and tag reroute knots and comment boxes separately), measure each node's size — reading the **actual rendered size** from the open graph panel when available, falling back to a pin-based estimate — traverse exec flow from each root *tracing through reroute nodes*, collect pure-node providers for each consumer (including chained pure → pure → exec chains, also traced through knots), and record which nodes each comment box currently wraps.
2. **Measure** subtree heights from the leaves upward (in pixels), accounting for branch spacing.
3. **Assign** positions top-down: exec subtrees flow left-to-right; branches stack their children vertically; each consumer reserves a horizontal lane for its pure-node column so the column can't overlap the exec predecessor.
4. **Apply** the calculated positions to the actual `UEdGraphNode`s via a single transaction.
5. **Finish** with two cosmetic passes: drop each reroute (knot) node onto the midpoint of the wire it sits on, and resize/reposition each comment box to wrap its recorded members in their new positions.

Configuration constants (paddings, default sizes, pure-node column limits, comment padding) live in `FBlueprintLayoutConfig` in `BlueprintAutoLayout.h`. They are not yet user-exposed.

## Status

v0.3.1 — public, MIT licensed. Not yet on Fab.

Known limitations:
- **Context menu only** — no hotkey or toolbar button yet. (A `Ctrl+Shift+L` hotkey was attempted in 0.2.0 but pulled in 0.2.1; it needs a proper command-list extender, planned for a later release.)
- No "layout selected nodes" — operates on the whole graph.
- No asset-action ("layout all graphs in this BP") — operates on the visible graph only.
- No settings panel — configuration constants require a source code edit.

## License

MIT. Copyright (c) 2026 Alex Coulombe.
