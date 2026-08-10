// acp-dist-tools v5 — vendored 2026-08-10 from update/ACPUpdateCheck.h
// Do not edit here — edit in acp-dist-tools and re-run sync_dist_tools.sh.
// Product: BPAutoLayout   Lane: native

// ACPUpdateCheck.h — background update-availability check (native/C++ lane).
//
// acp-dist-tools vN — do not edit here, edit in acp-dist-tools and re-sync.
//
// See update/acp_update_check.py for the full design rationale — this is the
// same contract (fetch GET <base>/updates, semver-compare against the
// installed version, fire a callback on availability) implemented against
// UE's FHttpModule instead of urllib.
//
// BUILD-VERIFICATION STATUS: written against UE's known public HTTP module
// API (FHttpModule::Get().CreateRequest(), IHttpRequest/IHttpResponse) but
// NOT compiled against a real UE project — see ACPLicense.h's header for the
// same caveat, which applies equally here. The HTTP module's request/response
// delegate signatures have had minor changes across engine versions; verify
// against your target engine's Http.h before shipping.
//
// Deliberately separate from ACPLicense.h/cpp: license validity and
// update-availability are independent concerns — see README.md.
//
// Alex Coulombe Presents.
#pragma once

#include "CoreMinimal.h"

#ifndef ACPLICENSE_API
#define ACPLICENSE_API
#endif

namespace ACPUpdateCheck
{
	// ========================================================================
	// VENDOR-TIME CONFIGURATION — every consuming plugin edits these three
	// (sync_dist_tools.sh does this automatically; see README.md).
	// ========================================================================
	static const TCHAR* const ProductId = TEXT("BPAutoLayout");             // must match ACPLicense::ProductId
	static const TCHAR* const InstalledVersion = TEXT("0.6.9");      // e.g. TEXT("1.4.0")
	// Matches the Python lane's UPDATE_BASE_URL (acp_update_check.py) — that one was verified live
	// 2026-08-06 (real entries for all five products). This native default used to be
	// "https://updates.alexcoulombepresents.com", a subdomain that has never resolved (DNS NXDOMAIN,
	// confirmed 2026-08-09) — every native-lane update check was silently hitting CheckFailed and
	// reporting the ambiguous "up to date, or couldn't reach the server" message, always, for every
	// consuming plugin, since there was no way to tell the two apart. Fixed both halves together —
	// see EStatus below.
	static const TCHAR* const DefaultBaseUrl = TEXT("https://www.alexcoulombepresents.com/api/plugins");

	static constexpr float RequestTimeoutSeconds = 6.0f;

	/** UpToDate and CheckFailed used to collapse into the same bAvailable=false result, which made
	 * "you're current" indistinguishable from "the check never actually completed" — see the
	 * DefaultBaseUrl note above for how that let a real, permanent bug (a dead default URL) hide
	 * for the plugin's whole life behind a message that sounded like reassurance. bAvailable is
	 * kept for source compatibility with existing call sites (true iff Status == Available); new
	 * code should read Status instead. */
	enum class EStatus : uint8
	{
		UpToDate,		// checked successfully; InstalledVersion is already current
		Available,		// checked successfully; a newer version exists (see Latest/NotesUrl/MinUe)
		CheckFailed,	// could not complete the check — network/DNS failure, non-200, malformed
						// JSON, missing "products"/product entry/"latest" field, or an unparseable
						// version string. Never treat this the same as UpToDate.
	};

	struct FUpdateResult
	{
		EStatus Status = EStatus::CheckFailed;
		bool bAvailable = false;	// == (Status == EStatus::Available)
		FString Latest;
		FString NotesUrl;
		FString MinUe;
	};

	/** Parses "X.Y.Z" (optional leading 'v') into (Major, Minor, Patch).
	 * Returns false (out params untouched) on anything else — callers must
	 * treat that as "can't compare, skip the notification". */
	ACPLICENSE_API bool ParseSemVer(const FString& VersionStr, int32& OutMajor, int32& OutMinor, int32& OutPatch);

	/** True iff Candidate is strictly newer than Current, by (major, minor,
	 * patch) tuple comparison. Returns false (not an error signal — callers
	 * distinguish "not newer" from "couldn't parse" via the bool return of
	 * ParseSemVer if they need to) if either string fails to parse. */
	ACPLICENSE_API bool IsNewer(const FString& Candidate, const FString& Current);

	/** Fire-and-forget: issues GET <BaseUrl>/updates on UE's HTTP module
	 * (fully async, no thread blocking — HTTP module callbacks fire on the
	 * game thread via the normal Slate/HTTP tick, so OnResult is always
	 * safe to touch UI/editor state directly, unlike the Python lane's
	 * background-thread callback). Calls OnResult exactly once, always —
	 * on success, on any HTTP/network failure, on a malformed manifest, or
	 * on "no update available" — check Result.Status to tell those apart
	 * (never an exception, never a silently-dropped callback). Pass an empty
	 * FString for BaseUrl to use DefaultBaseUrl. */
	ACPLICENSE_API void CheckForUpdateAsync(
		TFunction<void(const FUpdateResult&)> OnResult,
		const FString& BaseUrl = FString(),
		const FString& ProductIdOverride = FString(),
		const FString& InstalledVersionOverride = FString());

} // namespace ACPUpdateCheck
