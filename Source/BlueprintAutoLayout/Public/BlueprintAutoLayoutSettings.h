// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayoutSettings.h - Editor preferences for the Blueprint Anti-Pasta plugin.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BlueprintAutoLayoutSettings.generated.h"

/** How auto-generated group comment boxes pick their color. */
UENUM()
enum class EBPALCommentColorMode : uint8
{
	/** Map the root node's title to a meaningful color (Damage→red, Spawn→green, …), hash fallback. */
	KeywordSemantic UMETA(DisplayName = "Keyword (semantic)"),
	/** Step through a fixed palette so adjacent groups read as distinct. */
	CyclingPalette  UMETA(DisplayName = "Cycling palette"),
};

/** How the layout handles wires that would otherwise cut across nodes. */
UENUM()
enum class EBPALWireHandling : uint8
{
	/**
	 * Default: keep wires as straight lines and move the nodes instead — Sequence outputs get
	 * their own vertical lanes (so a later output's wire doesn't cross an earlier subtree), and
	 * data providers are nudged so their output pin lines up with the consumer pin they feed.
	 */
	StraightenAndMove UMETA(DisplayName = "Straighten & move nodes (default)"),
	/**
	 * Fallback: keep nodes where the base layout put them and bend wires around obstacles by
	 * inserting reroute (knot) nodes. Useful when a node genuinely can't be moved (e.g. a wire
	 * shared by several consumers). Mutates the graph by adding knot nodes.
	 */
	RerouteWithKnots UMETA(DisplayName = "Reroute with knots"),
	/** Off: lay out nodes but don't straighten, re-lane, or reroute any wires. */
	Off UMETA(DisplayName = "Off (don't touch wires)"),
};

/**
 * Editor preferences for Blueprint Anti-Pasta. Appears under
 * Editor Preferences → Plugins → Blueprint Anti-Pasta.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Blueprint Anti-Pasta"))
class UBlueprintAutoLayoutSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Use the layered (Sugiyama) layout engine — ranks nodes into columns, inserts dummy waypoints
	 * on long edges, minimizes wire crossings, and straightens the execution spine on pin Y with
	 * pin-aware Brandes-Köpf. Handles DAGs (cross-row links, multi-consumer data, long edges) that
	 * the legacy tree packer can't. Turn this off to fall back to the original single-parent tree
	 * layout. On by default.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Layout Engine",
		meta = (DisplayName = "Use layered (Sugiyama) engine"))
	bool bUseLayeredEngine = true;

	/**
	 * Route long edges through reroute (knot) nodes (layered engine only). An edge that spans more
	 * than one column is rewired through knots along its reserved lane so it draws as straight
	 * segments instead of one curved spline. The knots are tagged and regenerated on each layout, so
	 * they don't accumulate. Turn off to leave long edges as plain (curved) wires. On by default.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Layout Engine",
		meta = (DisplayName = "Knot-route long edges", EditCondition = "bUseLayeredEngine"))
	bool bMaterializeLongEdges = true;

	/**
	 * How the plain "Auto Layout Graph" / "Auto Layout & Group Graph" actions (and the keyboard
	 * shortcut) handle wires. "Straighten & move nodes" is the default: straight lines, nodes moved
	 * out of the way. "Reroute with knots" bends wires around obstacles instead. The dedicated
	 * "Route Wires" menu actions always insert knots regardless of this setting.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Wire Handling",
		meta = (DisplayName = "Wire handling"))
	EBPALWireHandling WireHandling = EBPALWireHandling::StraightenAndMove;

	/**
	 * Maximum distance (graph units) a node may be nudged vertically to line up a connected pin
	 * into a straight wire. Lower values keep nodes closer to their flow position but leave more
	 * wires slightly diagonal; higher values straighten more wires. Only used when "Wire handling"
	 * is "Straighten & move nodes".
	 */
	UPROPERTY(EditAnywhere, config, Category = "Wire Handling",
		meta = (DisplayName = "Pin-align tolerance", ClampMin = "0", ClampMax = "600", UIMin = "0", UIMax = "400"))
	int32 StraightenMaxNudge = 120;

	/** How the auto-grouping actions color the comment box created around each subtree. */
	UPROPERTY(EditAnywhere, config, Category = "Grouping",
		meta = (DisplayName = "Comment color mode"))
	EBPALCommentColorMode CommentColorMode = EBPALCommentColorMode::KeywordSemantic;

	/** Horizontal gap between a node and the next column of nodes (graph units). */
	UPROPERTY(EditAnywhere, config, Category = "Spacing",
		meta = (DisplayName = "Horizontal spacing", ClampMin = "0", ClampMax = "1000", UIMin = "20", UIMax = "400"))
	int32 HorizontalSpacing = 110;

	/** Vertical gap between stacked nodes (graph units). */
	UPROPERTY(EditAnywhere, config, Category = "Spacing",
		meta = (DisplayName = "Vertical spacing", ClampMin = "0", ClampMax = "1000", UIMin = "10", UIMax = "300"))
	int32 VerticalSpacing = 44;

	/** Extra vertical gap between the parallel paths of a branch / the lanes of a Sequence. */
	UPROPERTY(EditAnywhere, config, Category = "Spacing",
		meta = (DisplayName = "Branch / lane spacing", ClampMin = "0", ClampMax = "1000", UIMin = "0", UIMax = "400"))
	int32 BranchSpacing = 90;

	/** Extra vertical gap between separate event/function graphs. */
	UPROPERTY(EditAnywhere, config, Category = "Spacing",
		meta = (DisplayName = "Event spacing", ClampMin = "0", ClampMax = "2000", UIMin = "0", UIMax = "600"))
	int32 EventSpacing = 200;

	// Show under Editor Preferences (per-user), categorized with other plugins.
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
