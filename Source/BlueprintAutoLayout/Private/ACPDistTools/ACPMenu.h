// acp-dist-tools v5 — vendored 2026-08-10 from native/ACPMenu.h
// Do not edit here — edit in acp-dist-tools and re-run sync_dist_tools.sh.
// Product: BPAutoLayout   Lane: native

// ACPMenu.h — shared ACP toolbar/menu launcher template (native/C++ lane).
//
// acp-dist-tools vN — do not edit here, edit in acp-dist-tools and re-sync.
// (sync_dist_tools.sh stamps the real header when it vendors this file into
// a plugin repo — see that script and README.md. Unlike ACPLicense.h, this
// file has no per-product secret to substitute — only ProductId-shaped call
// sites in the CALLING module's own StartupModule() need editing.)
//
// ONE shared "Alex Coulombe Presents" entry point across every installed ACP
// plugin in a project, instead of N separate unrelated toolbar/menu entries.
// Every ACP plugin (native or Python) that vendors this contract registers
// itself into the SAME UToolMenus objects by shared, hardcoded names — no
// runtime plugin discovery, no cross-plugin registry object, no dependency
// on load order. Whichever ACP plugin's StartupModule() runs first creates
// the shared submenu(s) (idempotently — checked via UToolMenus::FindMenu
// first); every plugin after that just finds the existing shared submenu(s)
// and adds its own row. On ShutdownModule(), each plugin removes only its
// own row — the shared submenu itself is never torn down (see
// UnregisterProduct's doc comment for why).
//
// ============================================================================
// BUILD-VERIFICATION STATUS — READ BEFORE VENDORING
// ============================================================================
// This header was written against Unreal Engine's known public UToolMenus /
// Slate API surface but was NOT compiled or run against an actual UE
// project — acp-dist-tools has no UE project to build against, same
// limitation as ACPLicense.h. Treat this as a careful first draft. Before
// trusting it in a shipped plugin:
//   - Compile against your target engine version (UToolMenus/FToolMenuEntry
//     signatures have had minor changes across 5.x releases).
//   - Live-test in a running editor that AddSubMenu/AddMenuEntry behave the
//     same way on a TOOLBAR-anchored UToolMenu as they do on a MENU-anchored
//     one — the toolbar-anchor loop below is written by direct analogy to
//     the already-proven menu-bar case, but that specific claim (toolbar
//     object supports the same submenu-creation call) has not itself been
//     confirmed live. See the TOOLBAR RELIABILITY note below either way.
// ============================================================================
//
// ============================================================================
// TOOLBAR RELIABILITY — READ BEFORE ASSUMING THE BUTTON RENDERS
// ============================================================================
// UnrealRenderManBridge's Python-lane menu.py already found, live in a 5.8
// editor: a toolbar registration call succeeding (no error, no exception)
// does NOT mean a toolbar button actually renders — toolbar anchor names
// drift across engine versions and some silently accept an entry without
// ever displaying it. This template attempts the SAME toolbar anchors
// URMBridge/SceneAudit already probe (LevelEditor.LevelEditorToolBar.
// PlayToolBar, .User, and the bare LevelEditor.LevelEditorToolBar), in that
// order, and every attempt is logged as "accepted", never "registered" — the
// Window-menu fallback below is the one anchor that's actually
// guaranteed-working precedent (URKLiveLinkPlugin and URKLiveLinkDiagnostics
// both already ship with LevelEditor.MainMenu.Window successfully). Always
// register the fallback; never skip it because a toolbar attempt "succeeded".
// ============================================================================
//
// ICON — WHY PYTHON-LANE PLUGINS MAY SHOW NO ICON
// -------------------------------------------------
// The ACP brand icon is a Slate FSlateStyleSet ("ACPStyle"), which only
// native/C++ code can register — Python's unreal.ToolMenuEntry.set_icon can
// only REFERENCE an already-registered style by name, it cannot register a
// new one. Whichever native ACP plugin (Forage, BPAutoLayout, URKPreviewer)
// loads first in a given project registers "ACPStyle" here (idempotent —
// checked via FSlateStyleRegistry::FindSlateStyle first) from its OWN
// vendored Resources/ACPIcon_*.png copies (every native plugin vendors an
// identical copy of these PNGs, so it does not matter which one wins the
// race). Python-lane plugins (URMBridge, SceneAudit) reference "ACPStyle" by
// name and degrade to no icon if it was never registered — e.g. a project
// with only Python-lane ACP plugins installed and no native one at all. This
// is a deliberate, documented limitation, not an oversight: never make icon
// registration a hard requirement anywhere in this contract, and never crash
// if FSlateStyleRegistry / the PNG files are unavailable for any reason.
//
// Alex Coulombe Presents.
#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Containers/Array.h"
#include "Framework/Commands/UIAction.h"	// FExecuteAction

#ifndef ACPMENU_API
#define ACPMENU_API
#endif

namespace ACPMenu
{
	// ========================================================================
	// SHARED CONTRACT CONSTANTS — the identical literal strings also appear in
	// python/acp_menu.py. The two lanes never call into each other directly;
	// they cooperate ONLY by targeting the same UToolMenus names, so changing
	// one of these here without changing the Python-lane equivalent silently
	// splits the launcher into two disconnected menus. If you change either,
	// change both, in the same acp-dist-tools commit.
	// ========================================================================

	// Name of the shared submenu created under EACH successful anchor (see
	// AnchorCandidates below) — e.g. "LevelEditor.MainMenu.Window.ACPSharedLauncher".
	// There is deliberately no single floating menu object shared across
	// anchors: UToolMenus' AddSubMenu-on-an-existing-menu pattern is the only
	// registration shape already proven to work in this codebase (see the
	// TOOLBAR RELIABILITY note), so each anchor gets its OWN submenu with
	// this same name/label, and RegisterProduct/UnregisterProduct simply
	// repeat their single-entry add/remove at every anchor that exists. Minor
	// duplication of effort, zero unproven API surface.
	static const TCHAR* const SubMenuName = TEXT("ACPSharedLauncher");
	static const TCHAR* const SubMenuLabel = TEXT("Alex Coulombe Presents");
	static const TCHAR* const SubMenuTooltip = TEXT("Alex Coulombe Presents plugins installed in this project");

	// Section within each ACPSharedLauncher submenu that per-product rows go
	// into, and the section holding the single static "find more plugins" row.
	static const TCHAR* const ProductsSectionName = TEXT("ACPProducts");
	static const TCHAR* const MoreSectionName = TEXT("ACPMore");

	// Fixed entry name for the "find more plugins" row, identical at every
	// anchor and from every plugin's setup call — re-adding it under the same
	// name is an idempotent overwrite, never a duplicate row.
	static const TCHAR* const FindMoreEntryName = TEXT("ACPFindMorePlugins");
	static const TCHAR* const FindMoreLabel = TEXT("Find more plugins at alexcoulombepresents.com/plugins");
	static const TCHAR* const FindMoreUrl = TEXT("https://www.alexcoulombepresents.com/plugins");

	// Owner every registration is scoped under (FToolMenuOwnerScoped), so
	// UnregisterProduct only ever touches entries THIS contract added —
	// mirrors BlueprintAutoLayoutModule.cpp's existing FToolMenuOwnerScoped
	// pattern.
	static const TCHAR* const OwnerName = TEXT("ACPSharedLauncher");

	// Guaranteed-working fallback anchor — the exact anchor
	// URKLiveLinkPlugin/URKLiveLinkDiagnostics already ship with
	// successfully. Always attempted first and never skipped.
	static const TCHAR* const FallbackAnchor = TEXT("LevelEditor.MainMenu.Window");

	/** Best-effort toolbar anchors, tried in this order after the fallback —
	 * identical set and order to URMBridge/SceneAudit's own menu.py, reused
	 * rather than re-derived so any future engine-version fix to that list
	 * only has to happen once. Never assume any of these succeed; see the
	 * TOOLBAR RELIABILITY note above. */
	ACPMENU_API TArray<FString> GetToolbarAnchorCandidates();

	// Style-set name Python-lane plugins reference by string; see the ICON
	// header comment — only native code actually registers it.
	static const TCHAR* const IconStyleSetName = TEXT("ACPStyle");
	static const TCHAR* const IconStyleName = TEXT("ACPStyle.ToolbarIcon");			// ~40px, toolbar/submenu-header use
	static const TCHAR* const IconStyleNameSmall = TEXT("ACPStyle.ToolbarIcon.Small");	// ~16px, per-product row use

	/** Idempotent: for every anchor (fallback + toolbar candidates) that
	 * currently exists in this editor, find-or-create its ACPSharedLauncher
	 * submenu, and register the ACPStyle icon set from THIS CALLER's own
	 * vendored Resources/ACPIcon_*.png files if no plugin has registered it
	 * yet. Safe to call from every product's StartupModule() in any order —
	 * repeat calls across plugins (and across the SAME plugin re-loading) are
	 * no-ops for anything that already exists. Never throws; an anchor that
	 * doesn't exist, or an icon PNG that fails to load, is silently skipped
	 * (logged via UE_LOG, not surfaced as a failure) rather than treated as
	 * an error — matches this whole contract's "never crash the host"
	 * philosophy. CallerPluginDirectoryName is used only to locate the
	 * caller's OWN Resources/ folder for the (possibly-a-no-op) icon
	 * registration step; pass the same value you'd pass to
	 * ACPLicense::PluginDirectoryName. */
	ACPMENU_API void EnsureSharedMenuExists(const FString& CallerPluginDirectoryName);

	/** Adds (or updates, if called again — e.g. a hot-reload) one row for
	 * ProductId/FriendlyName into every anchor's ACPSharedLauncher submenu
	 * that currently exists. OnOpen is invoked when the row is clicked —
	 * typically a lambda that calls FGlobalTabmanager::TryInvokeTab or
	 * equivalent; FExecuteAction (not FSimpleDelegate) because that's the
	 * delegate type FToolUIActionChoice/FUIAction actually take — see
	 * ToolMenuDelegates.h. Calls EnsureSharedMenuExists() internally first,
	 * so most StartupModule() implementations only need to call
	 * RegisterProduct. */
	ACPMENU_API void RegisterProduct(const FString& ProductId, const FString& FriendlyName,
		const FString& CallerPluginDirectoryName, const FExecuteAction& OnOpen);

	/** Removes ONLY this product's row, from every anchor. Call from
	 * ShutdownModule(). Deliberately does NOT tear down the shared
	 * submenu(s)/icon style even if this was the last registered product —
	 * detecting "am I the last one out" would require cross-plugin
	 * coordination this contract specifically avoids (see the file header);
	 * an empty-but-present "Alex Coulombe Presents" entry with only the
	 * static "find more plugins" row left is a fine steady state. */
	ACPMENU_API void UnregisterProduct(const FString& ProductId);

} // namespace ACPMenu
