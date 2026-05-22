# Blueprint Auto Layout

Pin-aware auto-layout for Unreal Engine Blueprint graphs. Right-click on empty graph space → **Auto Layout Graph** → the algorithm rearranges your nodes into a readable left-to-right execution flow.

Handles:

- Branches (`Branch`, `Switch`, multi-output exec nodes) — sibling paths stack vertically without overlap
- Pure (data-only) nodes — clustered to the left of their consumer in a column
- Multi-event graphs — each event/function root gets its own row
- Sequence nodes — laid out as sequential, not branching
- Variable-width nodes — true node sizes are used, not assumed averages

Built as a single editor module with no runtime cost. One menu entry, one undo step per layout.

## Install

This is currently a private development repo. The standard UE plugin install pattern applies:

1. Clone (or copy) this repo into `<YourProject>/Plugins/blueprint-auto-layout/`.
2. Right-click your `.uproject` → **Generate Project Files**.
3. Build the project.
4. Launch the editor. The plugin loads automatically.

## Usage

1. Open any Blueprint / Animation Blueprint / Macro graph.
2. Right-click on empty space in the graph view.
3. Choose **Layout → Auto Layout Graph**.

The change is wrapped in a single transaction — one `Ctrl+Z` undoes the entire layout.

## Engine support

Currently developed and verified against **Unreal Engine 5.7**.

Intended future support: 5.5, 5.6, 5.8 (the existing plugin uses only stable `UEdGraph` and `K2Node_*` public API, so the multi-version matrix should be straightforward — that's a separate verification pass).

## Things to Try

1. Open a messy event graph. Right-click empty space. **Auto Layout Graph**. Watch nodes snap to a clean flow.
2. Press **Ctrl+Z** once. The entire layout reverts — the action is a single undo step.
3. Open a graph with multiple events (`BeginPlay`, custom events, etc). Run auto-layout. Each event becomes its own row.
4. Add a `Branch` node mid-flow. Run auto-layout. The True/False paths stack vertically without overlap; the `Then` path takes the upper lane.
5. Open an Animation Blueprint's AnimGraph or Event Graph. Right-click empty space. The same **Auto Layout Graph** entry appears — the menu attaches to all `UEdGraphSchema_K2`-derived schemas.

## Algorithm overview

Layout proceeds in four phases:

1. **Build** the layout tree: classify nodes as exec/pure/branch/root, calculate true dimensions from pins, traverse exec flow from each root, collect pure-node providers for each exec consumer.
2. **Measure** subtree heights from the leaves upward (in pixels), accounting for branch spacing.
3. **Assign** positions top-down: exec subtrees flow left-to-right; branches stack their children vertically; pure nodes column to the left of their consumer.
4. **Apply** the calculated positions to the actual `UEdGraphNode`s via a single transaction.

Configuration constants (paddings, default sizes, pure-node column limits) live in `FBlueprintLayoutConfig` in `BlueprintAutoLayout.h`. They are not yet user-exposed.

## Status

This is a v0.1.0 development build under private development. Not yet on Fab. Limitations:

- One entry point only (graph-context-menu). No toolbar, hotkey, or settings panel yet.
- No "layout selected nodes" — operates on the whole graph.
- No asset-action ("layout all graphs in this BP") — operates on the visible graph only.

These are intentional v1 cuts to keep the review surface small.

## License

MIT. Copyright (c) 2026 Alex Coulombe.
