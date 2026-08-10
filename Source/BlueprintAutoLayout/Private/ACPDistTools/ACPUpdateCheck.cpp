// acp-dist-tools v5 — vendored 2026-08-10 from update/ACPUpdateCheck.cpp
// Do not edit here — edit in acp-dist-tools and re-run sync_dist_tools.sh.
// Product: BPAutoLayout   Lane: native

// ACPUpdateCheck.cpp — background update-availability check (native/C++ lane) implementation.
//
// acp-dist-tools vN — do not edit here, edit in acp-dist-tools and re-sync.
//
// See ACPUpdateCheck.h for design rationale and the BUILD-VERIFICATION
// STATUS notice. Requires the "HTTP" module as a dependency — add it to the
// consuming plugin's Build.cs PublicDependencyModuleNames/
// PrivateDependencyModuleNames if not already present.
//
// Alex Coulombe Presents.
#include "ACPUpdateCheck.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ACPUpdateCheck
{
	bool ParseSemVer(const FString& VersionStr, int32& OutMajor, int32& OutMinor, int32& OutPatch)
	{
		FString S = VersionStr.TrimStartAndEnd();
		if (S.StartsWith(TEXT("v")) || S.StartsWith(TEXT("V")))
		{
			S = S.RightChop(1);
		}
		TArray<FString> Parts;
		S.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/false);
		if (Parts.Num() != 3)
		{
			return false;
		}
		for (const FString& P : Parts)
		{
			if (P.IsEmpty() || !P.IsNumeric())
			{
				return false;
			}
		}
		OutMajor = FCString::Atoi(*Parts[0]);
		OutMinor = FCString::Atoi(*Parts[1]);
		OutPatch = FCString::Atoi(*Parts[2]);
		return true;
	}

	bool IsNewer(const FString& Candidate, const FString& Current)
	{
		int32 CMaj, CMin, CPatch, IMaj, IMin, IPatch;
		if (!ParseSemVer(Candidate, CMaj, CMin, CPatch) || !ParseSemVer(Current, IMaj, IMin, IPatch))
		{
			return false;
		}
		if (CMaj != IMaj) return CMaj > IMaj;
		if (CMin != IMin) return CMin > IMin;
		return CPatch > IPatch;
	}

	static FUpdateResult MakeResult(EStatus Status)
	{
		FUpdateResult R;
		R.Status = Status;
		R.bAvailable = (Status == EStatus::Available);
		return R;
	}

	void CheckForUpdateAsync(
		TFunction<void(const FUpdateResult&)> OnResult,
		const FString& BaseUrl,
		const FString& ProductIdOverride,
		const FString& InstalledVersionOverride)
	{
		if (!OnResult)
		{
			// Nothing to call back into — nothing to do. Not an error: a
			// caller passing an empty callback is presumably just firing
			// the check for its side effects on a future entitlement path,
			// which doesn't exist yet (see ACPLicense::CheckEntitlement).
			return;
		}

		const FString Base = BaseUrl.IsEmpty() ? FString(DefaultBaseUrl) : BaseUrl;
		const FString Pid = ProductIdOverride.IsEmpty() ? FString(ProductId) : ProductIdOverride;
		const FString InstalledVer = InstalledVersionOverride.IsEmpty() ? FString(InstalledVersion) : InstalledVersionOverride;

		FString Url = Base;
		if (Url.EndsWith(TEXT("/")))
		{
			Url.LeftChopInline(1);
		}
		Url += TEXT("/updates");

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetTimeout(RequestTimeoutSeconds);

		// Captured by value: Pid/InstalledVer are small FStrings, safe to
		// copy into the lambda; OnResult is captured by value too since
		// TFunction is copyable and the request's lifetime may outlive this
		// stack frame significantly (async HTTP).
		Request->OnProcessRequestComplete().BindLambda(
			[OnResult, Pid, InstalledVer](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
				{
					OnResult(MakeResult(EStatus::CheckFailed));
					return;
				}

				TSharedPtr<FJsonObject> Root;
				TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
				if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
				{
					OnResult(MakeResult(EStatus::CheckFailed));
					return;
				}

				const TSharedPtr<FJsonObject>* ProductsObj = nullptr;
				if (!Root->TryGetObjectField(TEXT("products"), ProductsObj) || !ProductsObj || !ProductsObj->IsValid())
				{
					OnResult(MakeResult(EStatus::CheckFailed));
					return;
				}

				const TSharedPtr<FJsonObject>* EntryObj = nullptr;
				if (!(*ProductsObj)->TryGetObjectField(Pid, EntryObj) || !EntryObj || !EntryObj->IsValid())
				{
					OnResult(MakeResult(EStatus::CheckFailed));
					return;
				}

				FString Latest;
				if (!(*EntryObj)->TryGetStringField(TEXT("latest"), Latest) || Latest.IsEmpty())
				{
					OnResult(MakeResult(EStatus::CheckFailed));
					return;
				}

				// Parse both sides explicitly (rather than only calling IsNewer) so a malformed
				// version string reports CheckFailed, not UpToDate — IsNewer alone can't tell
				// "genuinely current" from "couldn't compare" apart, both return false.
				int32 LatestMaj, LatestMin, LatestPatch, InstalledMaj, InstalledMin, InstalledPatch;
				if (!ParseSemVer(Latest, LatestMaj, LatestMin, LatestPatch) ||
					!ParseSemVer(InstalledVer, InstalledMaj, InstalledMin, InstalledPatch))
				{
					OnResult(MakeResult(EStatus::CheckFailed));
					return;
				}

				if (!IsNewer(Latest, InstalledVer))
				{
					OnResult(MakeResult(EStatus::UpToDate));
					return;
				}

				FUpdateResult Result = MakeResult(EStatus::Available);
				Result.Latest = Latest;
				(*EntryObj)->TryGetStringField(TEXT("notes_url"), Result.NotesUrl);
				(*EntryObj)->TryGetStringField(TEXT("min_ue"), Result.MinUe);
				OnResult(Result);
			});

		Request->ProcessRequest();
	}

} // namespace ACPUpdateCheck
