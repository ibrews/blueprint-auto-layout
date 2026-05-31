# Blueprint Auto Layout

Pin-aware auto-layout for Unreal Engine Blueprint graphs. Right-click on empty graph space → **Auto Layout Graph** (or press **Ctrl/Cmd+Shift+L**) → the algorithm rearranges your nodes into a readable left-to-right execution flow.

![A scrambled event graph cleaned up in a single Auto Layout pass](Docs/cleanup.png)

*One command on a randomly-scattered graph: each event lands on its own row with straight wires.* The v0.5.5 wire handling up close:

![Before and after: straight wires by default, nodes moved out of the way](Docs/before-after.png)

**New in v0.5.5 — straight wires by default.** Instead of bending a wire around a node, the layout keeps the wire a clean straight line and moves the obstructing node out of its way:

- **Sequence outputs get their own lanes.** Each `Then_0`, `Then_1`, … output is placed in its own vertical lane, so a wire to a later output never has to cross an earlier output's subtree.
- **Data wires are straightened to their pins.** A variable `Get` (or other single-consumer pure node) is nudged vertically so its output pin lines up with the input pin it feeds — turning a diagonal wire into a clean horizontal one — while staying clear of the execution wire so the white exec line stays straight and unobstructed.

Knot-based wire rerouting is still available as a fallback (for wires that genuinely can't be straightened, e.g. a node shared by several consumers).

## Actions

Right-click empty graph space → **Layout →**:

- **Auto Layout Graph** — arranges the graph (straighten & move by default). Also bound to **Ctrl/Cmd+Shift+L** and the toolbar button.
- **Auto Layout Selected** — arranges only the selected nodes, in place. Also bound to **Ctrl/Cmd+Shift+K**. (Appears when nodes are selected.)
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
- **Toolbar:** click the **Auto Layout** button in the Blueprint editor toolbar.
- **Right-click** empty space → **Layout →** and pick an action (see [Actions](#actions) above). The **Auto Layout Selected** entry appears when you have nodes selected.

Either way the change is wrapped in a single transaction — one `Ctrl+Z` undoes the entire layout. The grouping and routing actions add nodes to the graph (group comment boxes / reroute knots); the plain, selected, and group-only actions never delete your nodes.

The grouping actions color each comment box by reading keywords in its root node's title, so a graph's groups read at a glance — `Damage`/`Hit`→red, `Destroy`/`Death`→dark red, `Spawn`/`Create`→green, `Begin`/`Init`→blue, `Tick`/`Update`→teal, `Input`/`Pressed`→amber, `Overlap`/`Collision`→violet. Names with no keyword get a stable color derived from the title:

![Group comment boxes auto-colored by keyword](Docs/grouped-colored.png)

## Settings

**Editor Preferences → Plugins → Blueprint Auto Layout:**

- **Wire handling** *(default: Straighten & move nodes)* — how the plain layout actions (and the shortcut) handle wires:
  - **Straighten & move nodes** — keep wires straight and move nodes out of the way (Sequence lanes + pin-aligned data wires).
  - **Reroute with knots** — keep nodes put and bend wires around obstacles with reroute knots.
  - **Off** — lay out nodes but don't straighten, re-lane, or reroute any wires.
  - (The dedicated **Auto Layout (Route Wires)** menu actions always insert knots regardless of this setting.)
- **Pin-align tolerance** *(default 120)* — the maximum distance a node may be nudged vertically to line up a connected pin into a straight wire. Lower = nodes stay closer to their flow position (more wires left slightly diagonal); higher = more wires straightened.
- **Comment color mode** — **Keyword (semantic)** (default) colors group comments by title keyword; **Cycling palette** steps through a fixed palette instead.
- **Spacing** — horizontal, vertical, branch/lane, and event spacing (graph units) for tuning layout density.

## Engine support

Currently developed and verified against **Unreal Engine 5.7**.

Intended future support: 5.5, 5.6, 5.8 (the plugin uses only stable `UEdGraph` and `K2Node_*` public API, so the multi-version matrix should be straightforward — that's a separate verification pass).

## Things to Try

1. Open a messy event graph. Press **Ctrl/Cmd+Shift+L** (or right-click → **Auto Layout Graph**). Watch nodes snap to a clean flow.
2. Press **Ctrl+Z** once. The entire layout reverts — the action is a single undo step.
3. Find a graph with a `Sequence` node. Run auto-layout. Each `Then` output now gets its own vertical lane, so the wire to a later output no longer cuts across an earlier output's nodes.
4. Find a node fed by a variable `Get`. Run auto-layout — the `Get` is nudged so its wire becomes a straight horizontal into the consumer's pin, while staying clear of the execution wire above it.
5. Select a handful of nodes, then press **Ctrl/Cmd+Shift+K** (or right-click → **Auto Layout Selected**). Only those nodes are arranged, in place; the rest of the graph is untouched.
6. Open a graph with multiple events (`BeginPlay`, custom events, etc). Run auto-layout. Each event becomes its own row.
7. Add a `Branch` node mid-flow. Run auto-layout. The True/False paths stack vertically in their own lanes; the `Then` path takes the upper lane.
8. Open an Animation Blueprint's Event Graph. The same actions and shortcut work — the plugin attaches to all `UEdGraphSchema_K2`-derived schemas.
9. On a graph with several events, run **Auto Layout & Group Graph**. Each event's subtree is laid out and wrapped in its own comment box, auto-named after the event — no typing required.
10. Switch **Editor Preferences → Plugins → Blueprint Auto Layout → Wire handling** to **Reroute with knots**, then run **Auto Layout Graph** on a dense graph: instead of moving nodes, wires that cross a node are bent around it with reroute knots. Run it again — the knot count stays the same (re-runs don't accumulate). Switch back to **Straighten & move nodes** for the default behavior.

## Algorithm overview

Layout proceeds in these phases:

1. **Build** the layout tree: classify nodes as exec/pure/branch/sequence/root (and tag reroute knots and comment boxes separately), measure each node's size — reading the **actual rendered size** from the open graph panel when available — traverse exec flow from each root *tracing through reroute nodes*, collect pure-node providers for each consumer (including chained pure → pure → exec chains), and record which nodes each comment box currently wraps.
2. **Measure** subtree heights from the leaves upward (in pixels), accounting for branch/lane spacing.
3. **Assign** positions top-down: exec subtrees flow left-to-right; branches *and Sequence nodes* stack their children into separate vertical lanes; each consumer reserves a horizontal lane for its pure-node column.
4. **Apply** the calculated positions to the actual `UEdGraphNode`s via a single transaction.
5. **Straighten** (the default wire handling): nudge each single-consumer pure provider so its output pin lines up with the consumer input pin it feeds — making the data wire a straight horizontal — while keeping the provider clear of the consumer's incoming execution corridor so the exec wire stays straight and unobstructed.
6. **Finish** with two cosmetic passes: drop each reroute (knot) node onto its wire (at pin height, so a knot on a straight wire stays straight), and resize/reposition each comment box to wrap its members in their new positions.

When **Auto Layout & Group Graph** is used, one extra step runs afterward: a new comment box is created around each root subtree, named after the root node's title and colored by matching keywords.

When a **Route Wires** action is used, a final phase tests each direct node→node wire against the rects of the nodes between its endpoints and splits any wire that would cut across an intervening node with reroute (knot) nodes — the fallback for wires that couldn't be straightened. Those knots are tagged so a re-run removes and regenerates them rather than accumulating.

Spacing, the wire-handling mode, the pin-align tolerance, and comment color mode are exposed in **Editor Preferences → Plugins → Blueprint Auto Layout**; the remaining tuning constants live in `FBlueprintLayoutConfig` in `BlueprintAutoLayout.h`.

## Status

v0.5.5 — public, MIT licensed. Not yet on Fab.

Known limitations:
- **Straightening handles single-consumer data wires and Sequence lanes** — it doesn't yet do full channel-based edge routing, so very dense graphs may still have some wire overlap (use the **Reroute with knots** fallback there).
- No asset-action ("layout all graphs in this BP") — operates on the visible graph only.
- Most fine-grained tuning constants still require a source edit; the most useful knobs (wire handling, pin-align tolerance, spacing, comment color) are exposed in Editor Preferences (see **Settings**).

## License

MIT. Copyright (c) 2026 Alex Coulombe.
