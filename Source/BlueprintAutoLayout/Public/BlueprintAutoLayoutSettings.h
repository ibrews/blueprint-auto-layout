// Copyright (c) 2026 Alex Coulombe. Licensed under the MIT License.
// BlueprintAutoLayoutSettings.h - Editor preferences for the Blueprint Auto Layout plugin.

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

/**
 * Editor preferences for Blueprint Auto Layout. Appears under
 * Editor Preferences → Plugins → Blueprint Auto Layout.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Blueprint Auto Layout"))
class UBlueprintAutoLayoutSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * When enabled, the plain "Auto Layout Graph" and "Auto Layout & Group Graph" actions also
	 * reroute wires around obstacle nodes (inserting reroute knots), as if you'd run the dedicated
	 * "Route Wires" actions. The explicit routing actions always route regardless of this setting.
	 * Off by default, since routing mutates the graph by adding nodes.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Wire Routing",
		meta = (DisplayName = "Route wires by default"))
	bool bRouteWiresByDefault = false;

	/** How the auto-grouping actions color the comment box created around each subtree. */
	UPROPERTY(EditAnywhere, config, Category = "Grouping",
		meta = (DisplayName = "Comment color mode"))
	EBPALCommentColorMode CommentColorMode = EBPALCommentColorMode::KeywordSemantic;

	// Show under Editor Preferences (per-user), categorized with other plugins.
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
