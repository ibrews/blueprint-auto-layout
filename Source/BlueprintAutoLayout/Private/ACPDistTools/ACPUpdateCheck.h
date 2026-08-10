// acp-dist-tools v2 — vendored 2026-08-06 from update/ACPUpdateCheck.h
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
	static const TCHAR* const DefaultBaseUrl = TEXT("https://updates.alexcoulombepresents.com");

	static constexpr float RequestTimeoutSeconds = 6.0f;

	struct FUpdateResult
	{
		bool bAvailable = false;
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
	 * on "no update available" (bAvailable=false in every failure case,
	 * never an exception, never a silently-dropped callback). Pass an empty
	 * FString for BaseUrl to use DefaultBaseUrl. */
	ACPLICENSE_API void CheckForUpdateAsync(
		TFunction<void(const FUpdateResult&)> OnResult,
		const FString& BaseUrl = FString(),
		const FString& ProductIdOverride = FString(),
		const FString& InstalledVersionOverride = FString());

} // namespace ACPUpdateCheck
