# Blueprint Auto Layout

Pin-aware auto-layout for Unreal Engine Blueprint graphs. Right-click on empty graph space or press **Ctrl+Shift+L** → the algorithm rearranges your nodes into a readable left-to-right execution flow.

Handles:

- Branches (`Branch`, `Switch`, multi-output exec nodes) — sibling paths stack vertically without overlap
- Pure (data-only) nodes — clustered to the left of their consumer in a column, chained pure nodes included
- Multi-event graphs — each event/function root gets its own row
- Sequence nodes — laid out as sequential, not branching
- Variable-width nodes — true node sizes are used, not assumed averages

Built as a single editor module with no runtime cost. One undo step per layout.

## Install

1. Clone (or copy) this repo into `<YourProject>/Plugins/blueprint-auto-layout/`.
2. Right-click your `.uproject` → **Generate Project Files**.
3. Build the project.
4. Launch the editor. The plugin loads automatically.

## Usage

**Context menu:** Right-click on empty space in any Blueprint / Animation Blueprint / Macro graph → **Layout → Auto Layout Graph**.

**Hotkey:** `Ctrl+Shift+L` while a Blueprint graph is open. The binding appears in **Editor Preferences → Keyboard Shortcuts → Blueprint Auto Layout** and can be rebound.

The change is wrapped in a single transaction — one `Ctrl+Z` undoes the entire layout.

## Engine support

Currently developed and verified against **Unreal Engine 5.7**.

Intended future support: 5.5, 5.6, 5.8 (the plugin uses only stable `UEdGraph` and `K2Node_*` public API, so the multi-version matrix should be straightforward — that's a separate verification pass).

## Things to Try

1. Open a messy event graph. Press **Ctrl+Shift+L**. Watch nodes snap to a clean flow.
2. Press **Ctrl+Z** once. The entire layout reverts — the action is a single undo step.
3. Open a graph with multiple events (`BeginPlay`, custom events, etc). Run auto-layout. Each event becomes its own row.
4. Add a `Branch` node mid-flow. Run auto-layout. The True/False paths stack vertically without overlap; the `Then` path takes the upper lane.
5. Open an Animation Blueprint's AnimGraph or Event Graph. The same **Ctrl+Shift+L** hotkey works — the command attaches to all `UEdGraphSchema_K2`-derived schemas.
6. Chain three Variable Get nodes feeding into a math expression. Run auto-layout. Each node in the chain positions correctly to the left of its consumer.

## Algorithm overview

Layout proceeds in four phases:

1. **Build** the layout tree: classify nodes as exec/pure/branch/root, calculate true dimensions from pins, traverse exec flow from each root, collect pure-node providers for each consumer (including chained pure → pure → exec chains).
2. **Measure** subtree heights from the leaves upward (in pixels), accounting for branch spacing.
3. **Assign** positions top-down: exec subtrees flow left-to-right; branches stack their children vertically; pure nodes column to the left of their consumer.
4. **Apply** the calculated positions to the actual `UEdGraphNode`s via a single transaction.

Configuration constants (paddings, default sizes, pure-node column limits) live in `FBlueprintLayoutConfig` in `BlueprintAutoLayout.h`. They are not yet user-exposed.

## Status

v0.2.0 — available on [Fab](https://fab.com). MIT licensed.

Known limitations:
- No "layout selected nodes" — operates on the whole graph.
- No asset-action ("layout all graphs in this BP") — operates on the visible graph only.
- No settings panel — configuration constants require a source code edit.

## License

MIT. Copyright (c) 2026 Alex Coulombe.
