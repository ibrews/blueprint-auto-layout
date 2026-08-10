// acp-dist-tools v5 — vendored 2026-08-10 from native/ACPLicense.h
// Do not edit here — edit in acp-dist-tools and re-run sync_dist_tools.sh.
// Product: BPAutoLayout   Lane: native

// ACPLicense.h — ACPL2 license checker template (native/C++ lane).
//
// acp-dist-tools vN — do not edit here, edit in acp-dist-tools and re-sync.
// (sync_dist_tools.sh stamps the real header + real _SECRET_NATIVE bytes when
// it vendors this file into a plugin repo — see that script and README.md.)
//
// THIS IS NOT DRM. Same deterrence-tier design as python/acp_license.py — see
// that file's header and README.md for the full rationale, and the two-secret
// design (this is the NATIVE-lane secret only; python/acp_license.py carries
// a separate one). The secret ships embedded in a compiled, symbol-stripped
// platform binary (.dylib/.dll), a meaningfully harder reverse-engineering
// target than Python .pyc, but still not real DRM — a sufficiently motivated
// attacker with a disassembler gets it back out eventually.
//
// ============================================================================
// BUILD-VERIFICATION STATUS — READ BEFORE VENDORING
// ============================================================================
// This header and its .cpp were written against Unreal Engine's known public
// API surface (FString, TArray, FHttpModule) but were NOT compiled or run
// against an actual UE project — acp-dist-tools has no UE project to build
// against. Treat this as a careful first draft, not verified-correct code.
// When you vendor this into a real plugin (Forage, BPAutoLayout, or
// URKPreviewer), budget time to actually compile it, and specifically:
//   - Verify the SHA-256/HMAC implementation in ACPLicense.cpp against a
//     known test vector (see tests/test_acp_license.py's test vectors in
//     this repo — the C++ HMAC output for a given key+message MUST match
//     Python's hmac.new(key, msg, hashlib.sha256).hexdigest() for the same
//     inputs; that's the cross-check to run first).
//   - Confirm FHttpModule usage in ACPUpdateCheck.cpp compiles against your
//     target engine version — HTTP module API has had minor signature
//     changes across UE releases.
// ============================================================================
//
// WHY A VENDORED SHA-256/HMAC INSTEAD OF UE's FSHA256
// ------------------------------------------------------
// UE's Core module exposes SHA hashing (Misc/SecureHash.h), but its exact
// surface (class names, static method signatures, whether HMAC is exposed at
// all vs. only raw hashing) has shifted across engine versions, and the three
// native-lane plugins this ships into may target different UE versions
// (5.6-5.8 per Alex's plugin roster). Since this repo cannot build-verify
// against any specific engine checkout, guessing at Epic's current header
// surface risks a confidently-wrong include/signature that only fails at
// compile time once vendored — silently, months from now, on whichever
// engine version happens to differ. A small, self-contained, public-domain-
// style SHA-256 implementation (ACPLicense.cpp) has zero engine-version
// coupling and a test-vector-verifiable, unchanging correctness contract.
// This is a deliberate choice, not a default — if a future vendoring finds
// UE's FSHA256 API stable and convenient for your target engine version,
// swapping to it is a reasonable simplification; just re-verify the HMAC
// output against the same test vectors first.
//
// DEV-MODE DETECTION (no _is_packaged() equivalent — see python/acp_license.py)
// ---------------------------------------------------------------------------
// A native module can't cheaply inspect "was I loaded from bytecode" the way
// Python can. Simplest correct option, and the one this template uses:
// license enforcement is gated behind a COMPILE-TIME flag, ACP_LICENSE_ENFORCE,
// that a plugin's Build.cs only defines for Shipping/packaged builds (see
// README.md "Onboarding a new plugin" for the exact Build.cs snippet). If
// ACP_LICENSE_ENFORCE is not defined — the default for Editor/Development
// builds compiled straight from a dev checkout — Check() always returns
// DevMode/OK without touching the filesystem. This means a plugin dev never
// needs a license file to iterate locally, same promise as the Python lane.
//
// Alex Coulombe Presents.
#pragma once

#include "CoreMinimal.h"

// This file is meant to be vendored directly into a plugin's own module
// (Private/Public folders) alongside its other source, not built as a
// separate shared module — so no cross-module DLL export is normally
// required. ACPLICENSE_API defaults to nothing; only define it yourself
// (e.g. to YOURMODULE_API) if you later split this into its own module and
// need cross-module linkage.
#ifndef ACPLICENSE_API
#define ACPLICENSE_API
#endif

namespace ACPLicense
{
	// ========================================================================
	// VENDOR-TIME CONFIGURATION — every consuming plugin edits these two
	// constants (sync_dist_tools.sh does this automatically; see README.md).
	// ========================================================================

	// e.g. TEXT("Forage"), TEXT("BPAutoLayout"), TEXT("URKPreviewer") — must
	// exactly match the --product value used at keygen time. This is a
	// LICENSING identifier, not a filesystem one — see PluginDirectoryName
	// below for the (possibly different) name UE's plugin system uses.
	static const TCHAR* const ProductId = TEXT("BPAutoLayout");

	// The plugin's actual UE-visible name — i.e. its .uplugin file's
	// basename (e.g. "BlueprintAutoLayout.uplugin" -> "BlueprintAutoLayout"),
	// NOT its FriendlyName ("Blueprint Anti-Pasta") and not necessarily
	// ProductId above. IPluginManager::FindPlugin() looks a plugin up by
	// this filesystem/descriptor name, so LicenseFilePath() below must use
	// THIS constant, never ProductId, to locate the plugin's base directory
	// — the two only coincide by convention, never guaranteed. Getting this
	// wrong makes FindPlugin() silently fail (returns nullptr), which
	// LicenseFilePath() maps to "file not found" — i.e. Check() would always
	// report EStatus::Missing even with a perfectly valid, correctly signed
	// license file sitting right next to the .uplugin. sync_dist_tools.sh
	// defaults this to ProductId's value unless --plugin-dir is passed
	// explicitly; pass it whenever the two names differ (they differ for
	// Blueprint Anti-Pasta: ProductId "BPAutoLayout" vs. plugin dir
	// "BlueprintAutoLayout").
	static const TCHAR* const PluginDirectoryName = TEXT("BlueprintAutoLayout");

	// Real random 48-byte secret, stamped in at vendor time by
	// sync_dist_tools.sh. NEVER hand-edit this to a placeholder and ship it.
	// Stored as a byte array (not an FString) since it's binary key material,
	// not text — avoids any encoding-conversion foot-gun.
	static const uint8 SecretNative[48] = {
		0x8d, 0xc4, 0x58, 0x68, 0xd8, 0x88, 0x03, 0xb0, 0x05, 0xda, 0x2b, 0x98,
		0xb6, 0x15, 0xa8, 0xa8, 0x3d, 0x23, 0xeb, 0xb8, 0x3e, 0x93, 0xee, 0x10,
		0x2b, 0xff, 0xc2, 0xda, 0x4e, 0x2a, 0xab, 0xf3, 0xcf, 0xc8, 0x77, 0xc9,
		0x11, 0x1b, 0xaa, 0x7b, 0x24, 0xbc, 0x35, 0xa6, 0x04, 0xc6, 0x0f, 0x74,
	};

	// The /plugins page lists every product's Educational/Commercial tiers plus a contact form
	// and mailto — a better landing than a bare email address, which told a user nothing about
	// pricing or tiers before they'd even written a message. No real checkout lives there yet
	// (still inquiry-based), but that's the site's own honest state, not something to route
	// around here.
	static const TCHAR* const Contact = TEXT("https://www.alexcoulombepresents.com/plugins");
	// A self-service evaluation is a local marker, not a signed ACPL2 tier.
	// It starts only when no .license file is present; see Check() below.
	static constexpr int32 TrialDays = 14;

	enum class EStatus : uint8
	{
		Ok,
		Missing,
		InvalidSignature,
		WrongProduct,
		Expired,
		Malformed,
		Trial,			// active local self-service trial
		TrialExpired,
		TrialUnavailable,
		DevMode,	// enforcement compiled out (ACP_LICENSE_ENFORCE not defined)
	};

	struct FLicenseInfo
	{
		FString Product;
		FString Licensee;
		FString Email;
		FString Tier;		// "edu"/"com" signed license, or local "trial"
		int32 Seats = 0;
		FString Expiry;	// ISO date, e.g. "2027-08-01"
		FString Signature;	// lowercase hex
	};

	struct FLicenseResult
	{
		bool bOk = false;
		EStatus Status = EStatus::Missing;
		FString Message;		// human-readable, safe to show directly in a UE notification
		TOptional<FLicenseInfo> Info;	// set whenever parsing succeeded, even if invalid/expired
	};

	/** Computes the HMAC-SHA256 signature UE::ACPL2 licenses use, as a
	 * lowercase hex string. Payload is
	 * "ACPL2|product|licensee|email|tier|seats|expiry" — matches
	 * python/acp_license.py::compute_signature() exactly so a license signed
	 * by keygen/make_license.py (Python) verifies identically here. */
	ACPLICENSE_API FString ComputeSignature(
		const FString& Product, const FString& Licensee, const FString& Email,
		const FString& Tier, int32 Seats, const FString& Expiry,
		const uint8* Secret, int32 SecretLen);

	/** Parses raw ACPL2 license file text into Info. Returns false (Info
	 * left untouched) on ANY malformed input — wrong header, wrong field
	 * count, bad tier, non-integer seats, unparseable date. Never throws:
	 * this codebase does not use C++ exceptions for control flow, matching
	 * UE convention and the "never crash the caller" contract. */
	ACPLICENSE_API bool ParseLicense(const FString& RawText, FLicenseInfo& OutInfo);

	/** Constant-time signature verification — deliberately NOT a plain
	 * FString::Equals()/== byte compare, which would short-circuit on the
	 * first mismatched byte and leak timing information an attacker could in
	 * principle use to binary-search a valid signature. See
	 * ACPLicense.cpp's ConstantTimeEquals() for the implementation. */
	ACPLICENSE_API bool VerifySignature(const FLicenseInfo& Info, const uint8* Secret, int32 SecretLen);

	/** Full check: reads <ProductId>.license from next to the plugin's
	 * .uplugin, verifies it, and evaluates edu (hard expiry vs. today) or
	 * com (update-window vs. this build's stamped BuildDate) semantics. When
	 * no license file exists, starts/resumes a 14-day local trial marker under
	 * the current OS user's settings directory. The marker is deliberately a
	 * deterrent, not DRM: deleting or editing it can reset a trial.
	 * NEVER crashes — any failure mode (missing file, bad signature, wrong
	 * product, expired, malformed, unreadable filesystem) returns a
	 * FLicenseResult with bOk=false and a friendly Message; the caller's job
	 * is to degrade every plugin action to that Message, never propagate an
	 * exception/ensure/check up into UE. */
	ACPLICENSE_API FLicenseResult Check();

	/** STUB — NOT WIRED TO ANYTHING REAL YET. Future hook point for a
	 * separate, authenticated entitlement flow (a website team is building
	 * /api/plugins/entitlement independently of this repo). Always returns
	 * false with bOutHasData=false in this stub — a real implementation must
	 * still never let "couldn't reach the server" be treated as "not
	 * entitled"; entitlement is an ADDITIONAL signal on top of the offline
	 * signature check, never a replacement for it. */
	ACPLICENSE_API bool CheckEntitlement(const FString& LicenseText, bool& bOutHasData);

} // namespace ACPLicense
