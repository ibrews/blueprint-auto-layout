// acp-dist-tools v5 — vendored 2026-08-10 from native/ACPMenu.cpp
// Do not edit here — edit in acp-dist-tools and re-run sync_dist_tools.sh.
// Product: BPAutoLayout   Lane: native

// ACPMenu.cpp — shared ACP toolbar/menu launcher template (native/C++ lane) implementation.
//
// acp-dist-tools vN — do not edit here, edit in acp-dist-tools and re-sync.
//
// See ACPMenu.h for the full design rationale and the TOOLBAR RELIABILITY /
// ICON notes. Every UToolMenus/FSlateStyleSet call below was checked against
// UE 5.8's actual installed engine source (Engine/Source/Developer/ToolMenus/
// Public/{ToolMenus,ToolMenu,ToolMenuSection,ToolMenuEntry,ToolMenuOwner,
// ToolMenuDelegates}.h and Engine/Source/Runtime/SlateCore/Public/Styling/
// {SlateStyle,SlateStyleRegistry}.h, plus a real minimal working
// FSlateStyleSet subclass — Engine/Source/Editor/WorldBrowser/Private/
// WorldBrowserStyle.cpp — as a usage-pattern reference) rather than written
// from memory. This still has NOT been compiled against a real UE PLUGIN
// project (only checked against the engine headers in isolation) — compile
// it and live-test in a running editor before trusting it; see ACPLicense.h's
// BUILD-VERIFICATION STATUS convention for why that distinction matters.
//
// Alex Coulombe Presents.
#include "ACPMenu.h"

#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenuEntry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"

DEFINE_LOG_CATEGORY_STATIC(LogACPMenu, Log, All);

namespace ACPMenu
{
	TArray<FString> GetToolbarAnchorCandidates()
	{
		return {
			TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"),
			TEXT("LevelEditor.LevelEditorToolBar.User"),
			TEXT("LevelEditor.LevelEditorToolBar"),
		};
	}

	namespace Internal
	{
		// Every anchor this contract attempts, fallback first — matches
		// python/acp_menu.py's ANCHOR_CANDIDATES order exactly. Both lanes
		// must agree on this order/set or they end up populating a
		// different subset of submenus for otherwise-identical products.
		static TArray<FString> GetAllAnchors()
		{
			TArray<FString> Anchors;
			Anchors.Add(FallbackAnchor);
			Anchors.Append(GetToolbarAnchorCandidates());
			return Anchors;
		}

		static bool IsToolbarAnchor(const FString& Anchor)
		{
			return Anchor != FallbackAnchor;
		}

		// A distinct FToolMenuOwner per product (not the shared OwnerName)
		// so RegisterProduct/UnregisterProduct can add/remove exactly one
		// product's entries via UnregisterOwnerByName, independent of every
		// other product and of the shared submenu structure itself.
		static FName GetProductOwnerName(const FString& ProductId)
		{
			return FName(*FString::Printf(TEXT("%s_%s"), OwnerName, *ProductId));
		}

		// Kept alive for the module's lifetime — FSlateStyleRegistry only
		// borrows a reference, it never takes ownership. Guarded by
		// FSlateStyleRegistry::FindSlateStyle so only the first native ACP
		// plugin to load in a given editor session actually constructs one;
		// every other native plugin (and every Python-lane plugin
		// referencing "ACPStyle" by name) just reuses it.
		static TUniquePtr<FSlateStyleSet> GACPStyle;

		static void EnsureIconStyleRegistered(const FString& CallerPluginDirectoryName)
		{
			if (FSlateStyleRegistry::FindSlateStyle(IconStyleSetName) != nullptr)
			{
				return; // Some ACP plugin — possibly this one, on a prior load — already did this.
			}

			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(CallerPluginDirectoryName);
			if (!Plugin.IsValid())
			{
				UE_LOG(LogACPMenu, Warning,
					TEXT("ACPMenu: could not locate plugin '%s' via IPluginManager to register the ACP icon — ")
					TEXT("continuing with no ACP icon (never a hard failure; see ACPMenu.h's ICON note)."),
					*CallerPluginDirectoryName);
				return;
			}

			const FString ResourcesDir = Plugin->GetBaseDir() / TEXT("Resources");
			const FString IconPath40 = ResourcesDir / TEXT("ACPIcon_40.png");
			const FString IconPath16 = ResourcesDir / TEXT("ACPIcon_16.png");
			if (!FPaths::FileExists(IconPath40) || !FPaths::FileExists(IconPath16))
			{
				UE_LOG(LogACPMenu, Warning,
					TEXT("ACPMenu: ACP icon PNGs not found under '%s' — continuing with no ACP icon. ")
					TEXT("(sync_dist_tools.sh should have copied resources/ACPIcon_*.png here — re-run it.)"),
					*ResourcesDir);
				return;
			}

			GACPStyle = MakeUnique<FSlateStyleSet>(IconStyleSetName);
			GACPStyle->SetContentRoot(ResourcesDir);
			GACPStyle->Set(IconStyleName, new FSlateImageBrush(IconPath40, FVector2D(40.f, 40.f)));
			GACPStyle->Set(IconStyleNameSmall, new FSlateImageBrush(IconPath16, FVector2D(16.f, 16.f)));
			FSlateStyleRegistry::RegisterSlateStyle(*GACPStyle);
			UE_LOG(LogACPMenu, Log, TEXT("ACPMenu: registered the ACPStyle icon set from '%s'."), *ResourcesDir);
		}

		static FToolMenuEntry MakeFindMoreEntry()
		{
			return FToolMenuEntry::InitMenuEntry(
				FName(FindMoreEntryName),
				FText::FromString(FindMoreLabel),
				FText::FromString(TEXT("Open the Alex Coulombe Presents plugins page in your browser")),
				FSlateIcon(),
				FExecuteAction::CreateStatic([]()
				{
					FPlatformProcess::LaunchURL(FindMoreUrl, nullptr, nullptr);
				}));
		}

		// Idempotent per anchor: finds the ACPSharedLauncher submenu under
		// Anchor if one already exists (from this plugin's own earlier call,
		// or another ACP plugin's), else creates it — sections + the static
		// "find more" row. Same shape as python/acp_menu.py's
		// _ensure_submenus, one anchor at a time. A missing/rejecting anchor
		// is logged as a note, never treated as an error — see the TOOLBAR
		// RELIABILITY note in ACPMenu.h.
		static UToolMenu* EnsureSubMenuAt(const FString& Anchor, TArray<FString>& OutNotes)
		{
			UToolMenus* ToolMenus = UToolMenus::Get();
			UToolMenu* Parent = ToolMenus->FindMenu(FName(*Anchor));
			if (Parent == nullptr)
			{
				OutNotes.Add(FString::Printf(TEXT("%s not found — skipped"), *Anchor));
				return nullptr;
			}

			const FString FullName = Anchor + TEXT(".") + SubMenuName;
			if (UToolMenu* Existing = ToolMenus->FindMenu(FName(*FullName)))
			{
				return Existing;
			}

			// UNVERIFIED for the toolbar case — see ACPMenu.h's TOOLBAR
			// RELIABILITY note: AddSubMenu is proven live only when Parent
			// is a MENU-type object (LevelEditor.MainMenu.Window). If a
			// toolbar-type Parent rejects this differently than a missing
			// menu does, that surfaces here as Sub == nullptr, logged below,
			// never a crash.
			UToolMenu* Sub = Parent->AddSubMenu(
				FToolMenuOwner(FName(OwnerName)), FName(ProductsSectionName), FName(SubMenuName),
				FText::FromString(SubMenuLabel), FText::FromString(SubMenuTooltip));
			if (Sub == nullptr)
			{
				OutNotes.Add(FString::Printf(TEXT("%s rejected the ACP submenu"), *Anchor));
				return nullptr;
			}

			Sub->FindOrAddSection(FName(ProductsSectionName));
			Sub->FindOrAddSection(FName(MoreSectionName)).AddEntry(MakeFindMoreEntry());

			const bool bIsToolbar = IsToolbarAnchor(Anchor);
			OutNotes.Add(FString::Printf(
				TEXT("ACP %s entry accepted at %s%s"),
				bIsToolbar ? TEXT("toolbar") : TEXT("menu-bar"),
				*Anchor,
				bIsToolbar ? TEXT(" (not confirmed visible — use the menu-bar entry if present)") : TEXT("")));
			return Sub;
		}

	} // namespace Internal

	void EnsureSharedMenuExists(const FString& CallerPluginDirectoryName)
	{
		Internal::EnsureIconStyleRegistered(CallerPluginDirectoryName);

		// EnsureSubMenuAt's FindOrAddSection(...).AddEntry(...) calls (the
		// static "find more plugins" row, and implicitly the sections
		// themselves) have no explicit owner parameter — FToolMenuSection::
		// AddEntry only takes the entry itself, so without an owner pushed
		// here it would silently inherit whatever owner scope the CALLING
		// plugin happens to have active at this exact moment (e.g. if this
		// runs from inside another module's own FToolMenuOwnerScoped block).
		// That plugin unloading later would then wipe the shared row along
		// with its own entries. Push the shared OwnerName explicitly so the
		// shared parts are always attributed to ACPSharedLauncher, never to
		// whichever plugin happened to trigger creation first.
		// Copy-init ("= expr"), not direct-init with parens — the parens
		// form here (`Type Var(FName(OwnerName));`) is the exact "most
		// vexing parse" shape (an inner TypeName(identifier) makes the
		// whole line parse as a function declaration, not a variable) that
		// bit RegisterProduct's original version of this same pattern.
		const FToolMenuOwner SharedOwner = FName(OwnerName);
		FToolMenuOwnerScoped OwnerScope(SharedOwner);

		TArray<FString> Notes;
		for (const FString& Anchor : Internal::GetAllAnchors())
		{
			Internal::EnsureSubMenuAt(Anchor, Notes);
		}
		for (const FString& Note : Notes)
		{
			UE_LOG(LogACPMenu, Log, TEXT("ACPMenu: %s"), *Note);
		}
	}

	void RegisterProduct(const FString& ProductId, const FString& FriendlyName,
		const FString& CallerPluginDirectoryName, const FExecuteAction& OnOpen)
	{
		EnsureSharedMenuExists(CallerPluginDirectoryName);

		UToolMenus* ToolMenus = UToolMenus::Get();

		// Scoped under a PER-PRODUCT owner (not the shared OwnerName) so
		// UnregisterProduct can cleanly sweep only this product's entry via
		// UnregisterOwnerByName — FToolMenuSection::RemoveEntry is private
		// (UToolMenus/UToolMenuSectionExtensions-only), so per-owner removal
		// is the actual available mechanism, not a stylistic choice. Named
		// local (not a temporary passed inline) to avoid the
		// FToolMenuOwnerScoped-vs-function-declaration "most vexing parse".
		const FToolMenuOwner ProductOwner = Internal::GetProductOwnerName(ProductId);
		FToolMenuOwnerScoped OwnerScope(ProductOwner);
		const FName EntryName(*FString::Printf(TEXT("ACPOpen_%s"), *ProductId));

		int32 RegisteredCount = 0;
		for (const FString& Anchor : Internal::GetAllAnchors())
		{
			const FString FullName = Anchor + TEXT(".") + SubMenuName;
			UToolMenu* Sub = ToolMenus->FindMenu(FName(*FullName));
			if (Sub == nullptr)
			{
				continue; // This anchor never got a submenu (see EnsureSharedMenuExists) — nothing to add to.
			}

			FSlateIcon Icon; // Empty by default — matches URKLiveLinkPlugin's existing FSlateIcon() fallback.
			if (FSlateStyleRegistry::FindSlateStyle(IconStyleSetName) != nullptr)
			{
				Icon = FSlateIcon(IconStyleSetName, IconStyleNameSmall);
			}

			FToolMenuEntry Entry = FToolMenuEntry::InitMenuEntry(
				EntryName,
				FText::FromString(FriendlyName),
				FText::FromString(FString::Printf(TEXT("Open %s"), *FriendlyName)),
				Icon,
				OnOpen);
			Sub->FindOrAddSection(FName(ProductsSectionName)).AddEntry(Entry);
			++RegisteredCount;
		}

		UE_LOG(LogACPMenu, Log, TEXT("ACPMenu: registered %s (%s) into %d ACP submenu(s)."),
			*ProductId, *FriendlyName, RegisteredCount);
	}

	void UnregisterProduct(const FString& ProductId)
	{
		// TryGet (not Get): ShutdownModule() can run after ToolMenus itself
		// has already torn down during editor exit — TryGet returns nullptr
		// in that case instead of resurrecting/recreating a dead singleton.
		UToolMenus* ToolMenus = UToolMenus::TryGet();
		if (ToolMenus == nullptr)
		{
			return;
		}

		// Sweeps every entry RegisterProduct added under this product's own
		// owner (across every anchor) in one call — see RegisterProduct's
		// comment on why per-owner removal, not FToolMenuSection::RemoveEntry
		// (private), is the mechanism here.
		ToolMenus->UnregisterOwnerByName(Internal::GetProductOwnerName(ProductId));
	}

} // namespace ACPMenu
