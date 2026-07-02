// Copyright (c) 2021 LocalizeDirect AB

#include "GridlyLocalizationServiceProvider.h"

#include "GridlyEditor.h"
#include "GridlyExporter.h"
#include "GridlyGameSettings.h"
#include "GridlyLocalizedText.h"
#include "GridlyLocalizedTextConverter.h"
#include "GridlyStyle.h"
#include "GridlyTask_DownloadLocalizedTexts.h"
#include "HttpModule.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "ILocalizationServiceModule.h"
#include "LocalizationCommandletTasks.h"
#include "LocalizationModule.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IMainFrameModule.h"
#include "Internationalization/Culture.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include <filesystem>
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "GridlyCultureConverter.h"
#include "LocalizationConfigurationScript.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IContentBrowserSingleton.h"
#include "Interfaces/IHttpRequest.h"
#include "Internationalization/Text.h"
#include "LocalizationTargetTypes.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/StringTableCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"


#if LOCALIZATION_SERVICES_WITH_SLATE
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#endif

#define LOCTEXT_NAMESPACE "Gridly"

static FName ProviderName("Gridly");

#include "Styling/AppStyle.h" // Ensure this header is included

class FGridlyLocalizationTargetEditorCommands final : public TCommands<FGridlyLocalizationTargetEditorCommands>
{
public:
	FGridlyLocalizationTargetEditorCommands() :
		TCommands<FGridlyLocalizationTargetEditorCommands>("GridlyLocalizationTargetEditor",
			NSLOCTEXT("Gridly", "GridlyLocalizationTargetEditor", "Gridly Localization Target Editor"), NAME_None,
			FAppStyle::GetAppStyleSetName()) // Replace FEditorStyle with FAppStyle
	{
	}

	TSharedPtr<FUICommandInfo> ImportAllCulturesForTargetFromGridly;
	TSharedPtr<FUICommandInfo> ExportNativeCultureForTargetToGridly;
	TSharedPtr<FUICommandInfo> ExportTranslationsForTargetToGridly;
	TSharedPtr<FUICommandInfo> DownloadSourceChangesFromGridly;

	/** Initialize commands */
	virtual void RegisterCommands() override;
};


void FGridlyLocalizationTargetEditorCommands::RegisterCommands()
{
	UI_COMMAND(ImportAllCulturesForTargetFromGridly, "Import from Gridly",
		"Imports translations for all cultures of this target to Gridly.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportNativeCultureForTargetToGridly, "Export to Gridly",
		"Exports native culture and source text of this target to Gridly.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportTranslationsForTargetToGridly, "Export All to Gridly",
		"Exports source text and all translations of this target to Gridly.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DownloadSourceChangesFromGridly, "Download Source Changes",
		"Downloads source changes from Gridly and updates string tables with CSV import.", EUserInterfaceActionType::Button, FInputChord());
}

FGridlyLocalizationServiceProvider::FGridlyLocalizationServiceProvider()
{
}

void FGridlyLocalizationServiceProvider::Init(bool bForceConnection)
{
	FGridlyLocalizationTargetEditorCommands::Register();
}

void FGridlyLocalizationServiceProvider::Close()
{
}

FText FGridlyLocalizationServiceProvider::GetStatusText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Status"), LOCTEXT("Unknown", "Unknown / not implemented"));

	return FText::Format(LOCTEXT("GridlyStatusText", "Gridly status: {Status}"), Args);
}

bool FGridlyLocalizationServiceProvider::IsEnabled() const
{
	return true;
}

bool FGridlyLocalizationServiceProvider::IsAvailable() const
{
	return true; // Check for server availability
}

const FName& FGridlyLocalizationServiceProvider::GetName(void) const
{
	return ProviderName;
}

const FText FGridlyLocalizationServiceProvider::GetDisplayName() const
{
	return LOCTEXT("GridlyLocalizationService", "Gridly Localization Service");
}

ELocalizationServiceOperationCommandResult::Type FGridlyLocalizationServiceProvider::GetState(
	const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds,
	TArray<TSharedRef<ILocalizationServiceState, ESPMode::ThreadSafe>>& OutState,
	ELocalizationServiceCacheUsage::Type InStateCacheUsage)
{
	return ELocalizationServiceOperationCommandResult::Succeeded;
}

DEFINE_LOG_CATEGORY_STATIC(LogGridlyLocalizationServiceProvider, Log, All);

ELocalizationServiceOperationCommandResult::Type FGridlyLocalizationServiceProvider::Execute(
	const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation,
	const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds,
	ELocalizationServiceOperationConcurrency::Type InConcurrency /*= ELocalizationServiceOperationConcurrency::Synchronous*/,
	const FLocalizationServiceOperationComplete& InOperationCompleteDelegate /*= FLocalizationServiceOperationComplete()*/)
{
	const TSharedRef<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadOperation =
		StaticCastSharedRef<FDownloadLocalizationTargetFile>(InOperation);
	const FString TargetCulture = DownloadOperation->GetInLocale();

	// Resolve the per-target name from the operation's target GUID so the task can pick the
	// correct Gridly connection from UGridlyGameSettings::TargetConnections.
	const FGuid TargetGuid = DownloadOperation->GetInTargetGuid();
	FString ResolvedTargetName;
	auto FindTargetByGuid = [&TargetGuid](const ULocalizationTargetSet* TargetSet) -> ULocalizationTarget*
	{
		if (!TargetSet)
		{
			return nullptr;
		}
		for (ULocalizationTarget* T : TargetSet->TargetObjects)
		{
			if (T && T->Settings.Guid == TargetGuid)
			{
				return T;
			}
		}
		return nullptr;
	};
	if (ULocalizationTarget* MatchingTarget = FindTargetByGuid(ULocalizationSettings::GetGameTargetSet()))
	{
		ResolvedTargetName = MatchingTarget->Settings.Name;
	}
	else if (ULocalizationTarget* MatchingTarget2 = FindTargetByGuid(ULocalizationSettings::GetEngineTargetSet()))
	{
		ResolvedTargetName = MatchingTarget2->Settings.Name;
	}

	UGridlyTask_DownloadLocalizedTexts* Task = UGridlyTask_DownloadLocalizedTexts::DownloadLocalizedTexts(nullptr);
	Task->TargetName = ResolvedTargetName;

	// On success
	Task->OnSuccessDelegate.BindLambda(
		[this, DownloadOperation, InOperationCompleteDelegate, TargetCulture](const TArray<FPolyglotTextData>& PolyglotTextDatas)
		{
			/*
			if (PolyglotTextDatas.Num() > 0)
			{
			*/
			const FString AbsoluteFilePathAndName = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectDir() / DownloadOperation->GetInRelativeOutputFilePathAndName());

			bool writeProc = FGridlyLocalizedTextConverter::WritePoFile(PolyglotTextDatas, TargetCulture, AbsoluteFilePathAndName);
			// Callback for successful write
			InOperationCompleteDelegate.Execute(DownloadOperation, ELocalizationServiceOperationCommandResult::Succeeded);
			/*
			}
			else
			{
				// Handle parse failure
				DownloadOperation->SetOutErrorText(LOCTEXT("GridlyErrorParse", "Failed to parse downloaded content"));
				InOperationCompleteDelegate.Execute(DownloadOperation, ELocalizationServiceOperationCommandResult::Failed);
			}
			*/
		});

	// On fail
	Task->OnFailDelegate.BindLambda(
		[DownloadOperation, InOperationCompleteDelegate](const TArray<FPolyglotTextData>& PolyglotTextDatas, const FGridlyResult& Error)
		{
			// Handle download failure
			DownloadOperation->SetOutErrorText(FText::FromString(Error.Message));
			InOperationCompleteDelegate.Execute(DownloadOperation, ELocalizationServiceOperationCommandResult::Failed);
		});

	// Activate the task
	Task->Activate();

	return ELocalizationServiceOperationCommandResult::Succeeded;
}



bool FGridlyLocalizationServiceProvider::CanCancelOperation(
	const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation) const
{
	return false;
}

void FGridlyLocalizationServiceProvider::CancelOperation(
	const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation)
{
}

void FGridlyLocalizationServiceProvider::Tick()
{
}

#if LOCALIZATION_SERVICES_WITH_SLATE
void FGridlyLocalizationServiceProvider::CustomizeSettingsDetails(IDetailCategoryBuilder& DetailCategoryBuilder) const
{
	const FText GridlySettingsInfoText = LOCTEXT("GridlySettingsInfo", "Use Project Settings to configure Gridly");
	FDetailWidgetRow& PublicKeyRow = DetailCategoryBuilder.AddCustomRow(GridlySettingsInfoText);
	PublicKeyRow.ValueContent()[SNew(STextBlock).Text(GridlySettingsInfoText)];
	PublicKeyRow.ValueContent().HAlign(EHorizontalAlignment::HAlign_Fill);
}

void FGridlyLocalizationServiceProvider::CustomizeTargetDetails(
	IDetailCategoryBuilder& DetailCategoryBuilder, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget) const
{
	// Not implemented
}

void FGridlyLocalizationServiceProvider::CustomizeTargetToolbar(
	TSharedRef<FExtender>& MenuExtender, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget) const
{
	const TSharedRef<FUICommandList> CommandList = MakeShareable(new FUICommandList());

	MenuExtender->AddToolBarExtension("LocalizationService", EExtensionHook::First, CommandList,
		FToolBarExtensionDelegate::CreateRaw(const_cast<FGridlyLocalizationServiceProvider*>(this),
			&FGridlyLocalizationServiceProvider::AddTargetToolbarButtons, LocalizationTarget, CommandList));
}

void FGridlyLocalizationServiceProvider::CustomizeTargetSetToolbar(
	TSharedRef<FExtender>& MenuExtender, TWeakObjectPtr<ULocalizationTargetSet> LocalizationTargetSet) const
{
	// Not implemented
}

void FGridlyLocalizationServiceProvider::AddTargetToolbarButtons(FToolBarBuilder& ToolbarBuilder,
	TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, TSharedRef<FUICommandList> CommandList)
{
	// Don't add toolbar buttons if target is engine

	if (!LocalizationTarget->IsMemberOfEngineTargetSet())
	{
		const bool bIsTargetSet = false;
		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().ImportAllCulturesForTargetFromGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::ImportAllCulturesForTargetFromGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(FGridlyLocalizationTargetEditorCommands::Get().ImportAllCulturesForTargetFromGridly,
			NAME_None,
			TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FGridlyStyle::GetStyleSetName(), "Gridly.ImportAction"));

		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().ExportNativeCultureForTargetToGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::ExportNativeCultureForTargetToGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(
			FGridlyLocalizationTargetEditorCommands::Get().ExportNativeCultureForTargetToGridly, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FGridlyStyle::GetStyleSetName(),
				"Gridly.ExportAction"));

		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().ExportTranslationsForTargetToGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::ExportTranslationsForTargetToGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(
			FGridlyLocalizationTargetEditorCommands::Get().ExportTranslationsForTargetToGridly, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FGridlyStyle::GetStyleSetName(),
				"Gridly.ExportAllAction"));

		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().DownloadSourceChangesFromGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::DownloadSourceChangesFromGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(
			FGridlyLocalizationTargetEditorCommands::Get().DownloadSourceChangesFromGridly, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FGridlyStyle::GetStyleSetName(),
				"Gridly.ImportAction"));
	}
}
#endif	  // LOCALIZATION_SERVICES_WITH_SLATE

void FGridlyLocalizationServiceProvider::ImportAllCulturesForTargetFromGridly(
	TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmText",
			"All local translations to non-native languages will be overwritten. Are you sure you wish to update?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		TArray<FString> Cultures;

		for (int i = 0; i < LocalizationTarget->Settings.SupportedCulturesStatistics.Num(); i++)
		{
			
			if (i != LocalizationTarget->Settings.NativeCultureIndex)
			{
				const FCultureStatistics CultureStats = LocalizationTarget->Settings.SupportedCulturesStatistics[i];
				Cultures.Add(CultureStats.CultureName);
			}
		}

		CurrentCultureDownloads.Append(Cultures);
		SuccessfulDownloads = 0;

		const float AmountOfWork = CurrentCultureDownloads.Num();
		ImportAllCulturesForTargetFromGridlySlowTask = MakeShareable(new FScopedSlowTask(AmountOfWork,
			LOCTEXT("ImportAllCulturesForTargetFromGridlyText", "Importing all cultures for target from Gridly")));

		ImportAllCulturesForTargetFromGridlySlowTask->MakeDialog();

		for (const FString& CultureName : Cultures)
		{
			ILocalizationServiceProvider& Provider = ILocalizationServiceModule::Get().GetProvider();
			TSharedRef<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadTargetFileOp =
				ILocalizationServiceOperation::Create<FDownloadLocalizationTargetFile>();
			DownloadTargetFileOp->SetInTargetGuid(LocalizationTarget->Settings.Guid);
			DownloadTargetFileOp->SetInLocale(CultureName);

			FString Path = FPaths::ProjectSavedDir() / "Temp" / "Game" / LocalizationTarget->Settings.Name / CultureName /
				LocalizationTarget->Settings.Name + ".po";
			FPaths::MakePathRelativeTo(Path, *FPaths::ProjectDir());
			DownloadTargetFileOp->SetInRelativeOutputFilePathAndName(Path);

			// Check the file length and delete if it is empty
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (PlatformFile.FileExists(*Path))
			{
				int64 FileSize = PlatformFile.FileSize(*Path);
				if (FileSize <= 0)
				{
					PlatformFile.DeleteFile(*Path);
					UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("Deleted empty file: %s"), *Path);
					continue;
				}
			}

			auto OperationCompleteDelegate = FLocalizationServiceOperationComplete::CreateRaw(this,
				&FGridlyLocalizationServiceProvider::OnImportCultureForTargetFromGridly, bIsTargetSet);

			Provider.Execute(DownloadTargetFileOp, TArray<FLocalizationServiceTranslationIdentifier>(),
				ELocalizationServiceOperationConcurrency::Synchronous, OperationCompleteDelegate);

			ImportAllCulturesForTargetFromGridlySlowTask->EnterProgressFrame(1.f);
		}

		ImportAllCulturesForTargetFromGridlySlowTask.Reset();
	}
}





void FGridlyLocalizationServiceProvider::OnImportCultureForTargetFromGridly(const FLocalizationServiceOperationRef& Operation,
	ELocalizationServiceOperationCommandResult::Type Result, bool bIsTargetSet)
{
	TSharedPtr<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadLocalizationTargetOp = StaticCastSharedRef<
		FDownloadLocalizationTargetFile>(Operation);

	CurrentCultureDownloads.Remove(DownloadLocalizationTargetOp->GetInLocale());

	if (Result == ELocalizationServiceOperationCommandResult::Succeeded)
	{
		SuccessfulDownloads++;
	}
	else
	{
		const FText ErrorMessage = DownloadLocalizationTargetOp->GetOutErrorText();
		UE_LOG(LogGridlyEditor, Error, TEXT("%s"), *ErrorMessage.ToString());
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage.ToString()));
	}

	if (CurrentCultureDownloads.Num() == 0 && SuccessfulDownloads > 0)
	{
		const FString TargetName = FPaths::GetBaseFilename(DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

		const auto Target = ILocalizationModule::Get().GetLocalizationTargetByName(TargetName, false);
		const FString AbsoluteFilePathAndName = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

		UE_LOG(LogGridlyEditor, Log, TEXT("Loading from file: %s"), *AbsoluteFilePathAndName);

		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();

		if (!bIsTargetSet)
		{

			//here we call the gather
			LocalizationCommandletTasks::ImportTextForTarget(MainFrameParentWindow.ToSharedRef(), Target,
				FPaths::GetPath(FPaths::GetPath(AbsoluteFilePathAndName)));

			Target->UpdateWordCountsFromCSV();
			Target->UpdateStatusFromConflictReport();



		}
	}
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateExportRequest(const TArray<FPolyglotTextData>& PolyglotTextDatas,
	const TSharedPtr<FLocTextHelper>& LocTextHelperPtr, bool bIncludeTargetTranslations,
	const FGridlyConnection& Connection)
{
	FString JsonString;
	FGridlyExporter::ConvertToJson(PolyglotTextDatas, bIncludeTargetTranslations, LocTextHelperPtr, JsonString);
	UE_LOG(LogGridlyEditor, Log, TEXT("Creating export request with %d entries"), PolyglotTextDatas.Num());

	const FString ApiKey = Connection.ExportApiKey;
	const FString ViewId = Connection.ExportViewId;

	FStringFormatNamedArguments Args;
	Args.Add(TEXT("ViewId"), *ViewId);
	const FString Url = FString::Format(TEXT("https://api.gridly.com/v1/views/{ViewId}/records"), Args);

	auto HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
	HttpRequest->SetContentAsString(JsonString);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetURL(Url);

	return HttpRequest;
}

void FGridlyLocalizationServiceProvider::ExportNativeCultureForTargetToGridly(
	TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmText",
			"This will overwrite your source strings on Gridly with the data in your UE project. Are you sure you wish to export?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		ULocalizationTarget* InLocalizationTarget = LocalizationTarget.Get();
		if (InLocalizationTarget)
		{
			FHttpRequestCompleteDelegate ReqDelegate = FHttpRequestCompleteDelegate::CreateRaw(this,
				&FGridlyLocalizationServiceProvider::OnExportNativeCultureForTargetToGridly);

			const FText SlowTaskText = LOCTEXT("ExportNativeCultureForTargetToGridlyText",
				"Exporting native culture for target to Gridly");

			ExportForTargetToGridly(InLocalizationTarget, ReqDelegate, SlowTaskText);
		}
	}
}

void FGridlyLocalizationServiceProvider::OnExportNativeCultureForTargetToGridly(FHttpRequestPtr HttpRequestPtr, FHttpResponsePtr HttpResponsePtr, bool bSuccess)
{
	UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();

	const bool bSyncRecords = GameSettings->bSyncRecords;
	if (bSuccess)
	{
		if (HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Ok || HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Created)
		{
			// Success: process the response and log the result
			const FString Content = HttpResponsePtr->GetContentAsString();
			const auto JsonStringReader = TJsonReaderFactory<TCHAR>::Create(Content);
			TArray<TSharedPtr<FJsonValue>> JsonValueArray;
			FJsonSerializer::Deserialize(JsonStringReader, JsonValueArray);
			ExportForTargetEntriesUpdated += JsonValueArray.Num();

			// Continue processing or log success...

			// Check if more requests are pending
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> NextRequest;
			if (ExportFromTargetRequestQueue.Dequeue(NextRequest))
			{
				NextRequest->ProcessRequest();
			}
			else
			{
				// Call FetchGridlyCSV here after all export operations are done
				if (bSyncRecords) {
					FetchGridlyCSV();
				}

				if (!IsRunningCommandlet())
				{
					FString Message = FString::Printf(TEXT("Number of entries updated: %llu"),
						ExportForTargetEntriesUpdated);  // Include deleted records

					UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
					ExportForTargetToGridlySlowTask.Reset();
				}

				bExportRequestInProgress = false;
				
			}
		}
		else
		{
			// Handle HTTP error
			const FString Content = HttpResponsePtr->GetContentAsString();
			const FString ErrorReason = FString::Printf(TEXT("Error: %d, reason: %s"), HttpResponsePtr->GetResponseCode(), *Content);
			UE_LOG(LogGridlyEditor, Error, TEXT("%s"), *ErrorReason);

			if (!IsRunningCommandlet())
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorReason));
				ExportForTargetToGridlySlowTask.Reset();
			}

			bExportRequestInProgress = false;
		}
	}
	else
	{
		// Handle failure
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GridlyConnectionError", "ERROR: Unable to connect to Gridly"));
			ExportForTargetToGridlySlowTask.Reset();
		}

		bExportRequestInProgress = false;
	}
	
}


void FGridlyLocalizationServiceProvider::ExportTranslationsForTargetToGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget,
	bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());
	UERecords.Empty();
	GridlyRecords.Empty();

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmText",
			"This will overwrite all your source strings AND translations on Gridly with the data in your UE project. Are you sure you wish to export?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		ULocalizationTarget* InLocalizationTarget = LocalizationTarget.Get();
		if (InLocalizationTarget)
		{
			FHttpRequestCompleteDelegate ReqDelegate = FHttpRequestCompleteDelegate::CreateRaw(this,
				&FGridlyLocalizationServiceProvider::OnExportTranslationsForTargetToGridly);

			const FText SlowTaskText = LOCTEXT("ExportTranslationsForTargetToGridlyText",
				"Exporting source text and translations for target to Gridly");

			ExportForTargetToGridly(InLocalizationTarget, ReqDelegate, SlowTaskText, true);
		}
	}
}

void FGridlyLocalizationServiceProvider::OnExportTranslationsForTargetToGridly(FHttpRequestPtr HttpRequestPtr, FHttpResponsePtr HttpResponsePtr, bool bSuccess)
{
	if (bSuccess)
	{
		if (HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Ok || HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Created)
		{
			// Success: process the response
			const FString Content = HttpResponsePtr->GetContentAsString();
			const auto JsonStringReader = TJsonReaderFactory<TCHAR>::Create(Content);
			TArray<TSharedPtr<FJsonValue>> JsonValueArray;
			FJsonSerializer::Deserialize(JsonStringReader, JsonValueArray);
			ExportForTargetEntriesUpdated += JsonValueArray.Num();

			// Continue processing or log success...

			// Check if more requests are pending
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> NextRequest;
			if (ExportFromTargetRequestQueue.Dequeue(NextRequest))
			{
				NextRequest->ProcessRequest();
			}
			else
			{
				// All export operations completed
				const FString Message = FString::Printf(TEXT("Number of entries updated: %llu"), ExportForTargetEntriesUpdated);
				UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);

				if (!IsRunningCommandlet())
				{
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
					ExportForTargetToGridlySlowTask.Reset();
				}

				bExportRequestInProgress = false;

				// Call FetchGridlyCSV here after all export operations are done
				FetchGridlyCSV();
			}
		}
		else
		{
			// Handle HTTP error
			const FString Content = HttpResponsePtr->GetContentAsString();
			const FString ErrorReason = FString::Printf(TEXT("Error: %d, reason: %s"), HttpResponsePtr->GetResponseCode(), *Content);
			UE_LOG(LogGridlyEditor, Error, TEXT("%s"), *ErrorReason);

			if (!IsRunningCommandlet())
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorReason));
				ExportForTargetToGridlySlowTask.Reset();
			}

			bExportRequestInProgress = false;
		}
	}
	else
	{
		// Handle failure
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GridlyConnectionError", "ERROR: Unable to connect to Gridly"));
			ExportForTargetToGridlySlowTask.Reset();
		}

		bExportRequestInProgress = false;
	}
}


void FGridlyLocalizationServiceProvider::ExportForTargetToGridly(ULocalizationTarget* InLocalizationTarget, FHttpRequestCompleteDelegate& ReqDelegate, const FText& SlowTaskText, bool bIncTargetTranslation)
{
	TArray<FPolyglotTextData> PolyglotTextDatas;
	TSharedPtr<FLocTextHelper> LocTextHelperPtr;
	UERecords.Empty();
	GridlyRecords.Empty();

	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	LastExportTargetName = InLocalizationTarget ? InLocalizationTarget->Settings.Name : FString();
	const FGridlyConnection Connection = GameSettings->ResolveConnectionForTarget(InLocalizationTarget);

	if (FGridlyLocalizedText::GetAllTextAsPolyglotTextDatas(InLocalizationTarget, PolyglotTextDatas, LocTextHelperPtr))
	{
		size_t TotalRequests = 0;

		while (PolyglotTextDatas.Num() > 0)
		{
			const size_t ChunkSize = FMath::Min(static_cast<int>(Connection.ExportMaxRecordsPerRequest), PolyglotTextDatas.Num());
			const TArray<FPolyglotTextData> ChunkPolyglotTextDatas(PolyglotTextDatas.GetData(), ChunkSize);
			PolyglotTextDatas.RemoveAt(0, ChunkSize);
			const auto HttpRequest = CreateExportRequest(ChunkPolyglotTextDatas, LocTextHelperPtr, bIncTargetTranslation, Connection);
			HttpRequest->OnProcessRequestComplete() = ReqDelegate;
			ExportFromTargetRequestQueue.Enqueue(HttpRequest);
			for (int i = 0; i < ChunkPolyglotTextDatas.Num(); i++)
			{
				const FString& Key = ChunkPolyglotTextDatas[i].GetKey();  // Access the correct array
				const FString& Namespace = ChunkPolyglotTextDatas[i].GetNamespace();  // Access the correct array
				
				UERecords.Add(FGridlyTypeRecord(Key, Namespace));
			}

			TotalRequests++;
		}

		ExportForTargetEntriesUpdated = 0;

		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
		if (ExportFromTargetRequestQueue.Dequeue(HttpRequest))
		{
			if (!IsRunningCommandlet())
			{
				ExportForTargetToGridlySlowTask = MakeShareable(new FScopedSlowTask(static_cast<float>(TotalRequests), SlowTaskText));
				ExportForTargetToGridlySlowTask->MakeDialog();
			}

			bExportRequestInProgress = true;
			HttpRequest->ProcessRequest();
		}
	}
}

bool FGridlyLocalizationServiceProvider::HasRequestsPending() const
{
	return !ExportFromTargetRequestQueue.IsEmpty() || bExportRequestInProgress;
}

FHttpRequestCompleteDelegate FGridlyLocalizationServiceProvider::CreateExportNativeCultureDelegate()
{
	return FHttpRequestCompleteDelegate::CreateRaw(this, &FGridlyLocalizationServiceProvider::OnExportNativeCultureForTargetToGridly);
}

void FGridlyLocalizationServiceProvider::FetchGridlyCSV()
{
	// Set the flag to true at the beginning of the process
	bHasDeletesPending = true;

	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const FGridlyConnection Connection = GameSettings->ResolveConnectionForTarget(LastExportTargetName);
	const FString ApiKey = Connection.ExportApiKey;
	const FString ViewId = Connection.ExportViewId;
	// URL for fetching the CSV from Gridly
	FStringFormatNamedArguments Args;
	Args.Add(TEXT("ViewId"), *ViewId);
	const FString GridlyURL = FString::Format(TEXT("https://api.gridly.com/v1/views/{ViewId}/export"), Args);

	// Create the HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(GridlyURL);

	// Set the required headers, including the authorization
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/csv"));

	// Bind a callback to handle the response
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGridlyLocalizationServiceProvider::OnGridlyCSVResponseReceived);

	// Send the request
	HttpRequest->ProcessRequest();
}

void FGridlyLocalizationServiceProvider::OnGridlyCSVResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to fetch Gridly CSV"));
		bHasDeletesPending = false; // Reset flag on failure
		return;
	}

	// Retrieve the response content (CSV data)
	FString CSVContent = Response->GetContentAsString();

	// Parse the CSV data to extract records
	ParseCSVAndCreateRecords(CSVContent);
}

void FGridlyLocalizationServiceProvider::ParseCSVAndCreateRecords(const FString& CSVContent)
{
	// Don't reset the flag here, it will be reset in DeleteRecordsFromGridly if there are no records to delete
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	
	const TCHAR QuoteChar = TEXT('"');
	const TCHAR Delimiter = TEXT(',');

	bool bInsideQuotes = false;
	FString CurrentField;
	TArray<FString> Fields;
	FString CurrentLine;

	// Buffer to store the accumulated lines in case of multi-line records
	TArray<FString> AccumulatedLines;

	int32 RecordIdColumnIndex = -1;
	int32 PathColumnIndex = -1;

	// First pass: determine which columns contain the Record ID and Path
	bool bFoundHeader = false;
	for (int32 i = 0; i < CSVContent.Len(); ++i)
	{
		TCHAR Char = CSVContent[i];

		if (bInsideQuotes)
		{
			if (Char == QuoteChar)
			{
				if (i + 1 < CSVContent.Len() && CSVContent[i + 1] == QuoteChar)
				{
					CurrentField += QuoteChar;
					++i;
				}
				else
				{
					bInsideQuotes = false;
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
		else
		{
			if (Char == QuoteChar)
			{
				bInsideQuotes = true;
			}
			else if (Char == Delimiter)
			{
				Fields.Add(CurrentField);
				CurrentField.Empty();
			}
			else if (Char == '\n' || Char == '\r')
			{
				// End of header line, process the column headers
				if (Fields.Num() > 0 || !CurrentField.IsEmpty())
				{
					Fields.Add(CurrentField);
					CurrentField.Empty();
				}

				if (!bFoundHeader)
				{
					for (int32 ColumnIndex = 0; ColumnIndex < Fields.Num(); ++ColumnIndex)
					{
						FString ColumnName = Fields[ColumnIndex].TrimQuotes();

						if (ColumnName.Equals(TEXT("Record ID"), ESearchCase::IgnoreCase))
						{
							RecordIdColumnIndex = ColumnIndex;
						}
						else if (ColumnName.Equals(TEXT("Path"), ESearchCase::IgnoreCase))
						{
							PathColumnIndex = ColumnIndex;
						}
					}

					bFoundHeader = true;
					Fields.Empty();
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
	}

	// Check if we found both necessary columns
	if (RecordIdColumnIndex == -1 || PathColumnIndex == -1)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to identify Record ID or Path columns in CSV."));
		bHasDeletesPending = false; // Reset flag if we can't identify the columns
		return;
	}

	// Second pass: parse the actual records
	bInsideQuotes = false;
	Fields.Empty();
	CurrentField.Empty();
	for (int32 i = 0; i < CSVContent.Len(); ++i)
	{
		TCHAR Char = CSVContent[i];

		if (bInsideQuotes)
		{
			if (Char == QuoteChar)
			{
				if (i + 1 < CSVContent.Len() && CSVContent[i + 1] == QuoteChar)
				{
					CurrentField += QuoteChar;
					++i;
				}
				else
				{
					bInsideQuotes = false;
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
		else
		{
			if (Char == QuoteChar)
			{
				bInsideQuotes = true;
			}
			else if (Char == Delimiter)
			{
				Fields.Add(CurrentField);
				CurrentField.Empty();
			}
			else if (Char == '\n' || Char == '\r')
			{
				if (Fields.Num() > 0 || !CurrentField.IsEmpty())
				{
					Fields.Add(CurrentField);
					CurrentField.Empty();
				}

				if (Fields.Num() > FMath::Max(RecordIdColumnIndex, PathColumnIndex))
				{
					FString RecordId = Fields[RecordIdColumnIndex].TrimQuotes();
					FString Path = Fields[PathColumnIndex].TrimQuotes();


					FGridlyTypeRecord NewRecord(RemoveNamespaceFromKey(RecordId), Path);

					if (NewRecord.Id != "Record ID") {
						GridlyRecords.Add(NewRecord);
					}
				}

				Fields.Empty();
			}
			else
			{
				CurrentField += Char;
			}
		}
	}

	// Handle the last line if needed
	if (Fields.Num() > 0 || !CurrentField.IsEmpty())
	{
		Fields.Add(CurrentField);
		if (Fields.Num() > FMath::Max(RecordIdColumnIndex, PathColumnIndex))
		{
			FString RecordId = Fields[RecordIdColumnIndex].TrimQuotes();
			FString Path = Fields[PathColumnIndex].TrimQuotes();


			FGridlyTypeRecord NewRecord(RemoveNamespaceFromKey(RecordId), Path);

			if (NewRecord.Id != "Record ID") {
				GridlyRecords.Add(NewRecord);
			}
		}
	}

	for (const FGridlyTypeRecord& Record : UERecords)
	{
		UE_LOG(LogTemp, Log, TEXT("UE Record ID: %s, Path: %s"), *Record.Id, *Record.Path);
	}
	

	// Log or further process the GridlyRecords array
	for (const FGridlyTypeRecord& Record : GridlyRecords)
	{
		UE_LOG(LogTemp, Log, TEXT("Gridly Record ID: %s, Path: %s"), *Record.Id, *Record.Path);
	}

	TArray<FString> RecordsToDelete;
	

	for (const FGridlyTypeRecord& GridlyRecord : GridlyRecords)
	{
		// Check if any UERecord has a matching path first
		bool PathFoundInUE = false;
		bool RecordIdFoundInUE = false;

		for (const FGridlyTypeRecord& UERecord : UERecords)
		{
			if (GridlyRecord.Path == UERecord.Path)
			{
				PathFoundInUE = true; // The path matches
				if (GridlyRecord.Id == UERecord.Id)
				{
					RecordIdFoundInUE = true; // The record ID matches as well for the same path
					break; // Both path and record ID match, no need to continue searching
				}
			}
		}

		// Only handle deletion if the path was found, but the ID was not found for that path or path not found in UE
		if ((PathFoundInUE && !RecordIdFoundInUE ) || !PathFoundInUE)
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("No match found for GridlyRecord: ID = %s, Path = %s. Adding to delete list."), *GridlyRecord.Id, *GridlyRecord.Path);

			// If the path is empty or used combine namespace and ID is false, we only add the record ID
			if (GridlyRecord.Path.Len() == 0 || !GameSettings->bUseCombinedNamespaceId)
			{
				RecordsToDelete.Add(GridlyRecord.Id);
			}
			// If the path starts with "blueprints/", add the ID with a comma prefix
			else if (GridlyRecord.Path.StartsWith(TEXT("blueprints/")))
			{
				RecordsToDelete.Add("," + GridlyRecord.Id);
			}
			else
			{
				// Otherwise, add the path and ID combination
				RecordsToDelete.Add(GridlyRecord.Path + "," + GridlyRecord.Id);
			}
		}
	}


	

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Number of Gridly records: %d"), GridlyRecords.Num());
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Number of UE records: %d"), UERecords.Num());


	// Optionally, pass this list for further processing
	if (RecordsToDelete.Num() > 0)
	{
		DeleteRecordsFromGridly(RecordsToDelete);
	}
	else
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("No records to delete."));
		bHasDeletesPending = false; // Reset flag if there are no records to delete
	}
}

void FGridlyLocalizationServiceProvider::DeleteRecordsFromGridly(const TArray<FString>& RecordsToDelete)
{
	const int32 MaxRecordsPerRequest = 1000;  // Maximum number of records per batch
	UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("DeleteRecordsFromGridly CALLED"));

	if (RecordsToDelete.Num() == 0)
	{
		bHasDeletesPending = false;
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("No records to delete."));
		return;
	}
	bHasDeletesPending = true;
	// Initialize the counters
	CompletedBatches = 0;  // Reset the counter for completed batches
	TotalBatchesToProcess = FMath::CeilToInt(static_cast<float>(RecordsToDelete.Num()) / MaxRecordsPerRequest);


	// Split the records into batches of MaxRecordsPerRequest
	int32 TotalRecords = RecordsToDelete.Num();
	int32 TotalBatches = FMath::CeilToInt(static_cast<float>(TotalRecords) / MaxRecordsPerRequest);
	CompletedBatches = 0;  // Initialize the completed batch counter
	TotalBatchesToProcess = TotalBatches;  // Track the total number of batches


	for (int32 BatchIndex = 0; BatchIndex < TotalBatches; BatchIndex++)
	{
		// Create a new array for each batch
		TArray<FString> BatchRecords;

		int32 StartIndex = BatchIndex * MaxRecordsPerRequest;
		int32 EndIndex = FMath::Min(StartIndex + MaxRecordsPerRequest, TotalRecords); // Ensure not to exceed total records

		// Manually append the batch records
		for (int32 i = StartIndex; i < EndIndex; ++i)
		{
			// Clean up the record ID to prevent duplication
			FString CleanRecordId = RecordsToDelete[i];
			// Remove any duplicate commas and spaces
			CleanRecordId = CleanRecordId.Replace(TEXT(",,"), TEXT(","));
			CleanRecordId = CleanRecordId.Replace(TEXT(" ,"), TEXT(","));
			CleanRecordId = CleanRecordId.Replace(TEXT(", "), TEXT(","));
			
			BatchRecords.Add(CleanRecordId);
		}

		// Convert the batch to JSON and send the request
		FString JsonPayload;
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> JsonIds;

		for (const FString& RecordId : BatchRecords)
		{
			JsonIds.Add(MakeShared<FJsonValueString>(RecordId));
		}

		JsonObject->SetArrayField(TEXT("ids"), JsonIds);

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonPayload);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

		// Log the JSON payload for debugging
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("JSON Payload: %s"), *JsonPayload);

		const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
		const FGridlyConnection Connection = GameSettings->ResolveConnectionForTarget(LastExportTargetName);
		const FString ApiKey = Connection.ExportApiKey;
		const FString ViewId = Connection.ExportViewId;

		FStringFormatNamedArguments Args;
		Args.Add(TEXT("ViewId"), *ViewId);
		const FString Url = FString::Format(TEXT("https://api.gridly.com/v1/views/{ViewId}/records"), Args);

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetVerb(TEXT("DELETE"));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
		HttpRequest->SetURL(Url);
		HttpRequest->SetContentAsString(JsonPayload);

		// Bind the response handler for each batch
		HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGridlyLocalizationServiceProvider::OnDeleteRecordsResponse);

		HttpRequest->ProcessRequest();

		// Track the number of records requested for deletion
		ExportForTargetEntriesDeleted += BatchRecords.Num();

		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Delete request sent for %d records."), BatchRecords.Num());
	}
}

void FGridlyLocalizationServiceProvider::OnDeleteRecordsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!Request.IsValid() || !Response.IsValid())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("Invalid HTTP request or response."));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Invalid HTTP request or response.")));
		bHasDeletesPending = false;  // Reset flag on invalid request/response
		return;
	}

	// Increment the completed batch counter
	CompletedBatches++;

	if (bWasSuccessful && Response->GetResponseCode() == EHttpResponseCodes::NoContent)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Successfully deleted records."));

		// Only show the success message when all batches are done
		if (CompletedBatches == TotalBatchesToProcess)
		{
			bHasDeletesPending = false;

			if (!IsRunningCommandlet())
			{
				// Show dialog only in editor mode
				FString Message = FString::Printf(TEXT("Number of entries deleted: %llu"), ExportForTargetEntriesDeleted);
				UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
			}
			else
			{
				UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Commandlet: Deleted %llu records from Gridly."), ExportForTargetEntriesDeleted);
			}
		}
	}
	else
	{
		// Handle any failure cases
		FString ErrorMessage = FString::Printf(TEXT("Failed to delete records. HTTP Code: %d, Response: %s"),
			Response->GetResponseCode(), *Response->GetContentAsString());

		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("%s"), *ErrorMessage);

		// Reset the flag when all batches are done, regardless of success or failure
		if (CompletedBatches == TotalBatchesToProcess)
		{
			bHasDeletesPending = false;
			
			if (!IsRunningCommandlet())
			{
				FString DialogMessage = FString::Printf(TEXT("Error during record deletion.\nHTTP Code: %d\nResponse: %s"),
					Response->GetResponseCode(), *Response->GetContentAsString());

				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogMessage));
			}
		}
	}
}

bool FGridlyLocalizationServiceProvider::HasDeleteRequestsPending() const
{
	return bHasDeletesPending;
}

void FGridlyLocalizationServiceProvider::DownloadSourceChangesFromGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmSourceChangesText",
			"🔄 Download Source Changes from Gridly\n\n"
			"This feature will:\n"
			"• Download source strings from Gridly per namespace\n"
			"• Generate CSV files for each string table\n"
			"• Store files in: [Project]/Saved/Temp/GridlySourceChanges/\n\n"
			"⚠️ WARNING: This may modify source strings in your localization files.\n"
			"Review all changes before committing to version control.\n\n"
			"Are you sure you wish to proceed?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		// Get the native culture for source strings
		FString NativeCulture;
		if (LocalizationTarget->Settings.SupportedCulturesStatistics.IsValidIndex(LocalizationTarget->Settings.NativeCultureIndex))
		{
			const FCultureStatistics CultureStats = LocalizationTarget->Settings.SupportedCulturesStatistics[LocalizationTarget->Settings.NativeCultureIndex];
			NativeCulture = CultureStats.CultureName;
		}
		else
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No native culture found for target: %s"), *LocalizationTarget->Settings.Name);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No native culture found for this localization target.")));
			return;
		}

		// Check if we have any supported cultures
		if (LocalizationTarget->Settings.SupportedCulturesStatistics.Num() == 0)
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No supported cultures found for target: %s"), *LocalizationTarget->Settings.Name);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No supported cultures found for this localization target.")));
			return;
		}

		// Download source changes from Gridly
		DownloadSourceChangesFromGridlyInternal(LocalizationTarget, NativeCulture);
	}
}

void FGridlyLocalizationServiceProvider::DownloadSourceChangesFromGridlyInternal(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, const FString& NativeCulture)
{
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const FGridlyConnection Connection = GameSettings->ResolveConnectionForTarget(LocalizationTarget.Get());
	const FString ApiKey = Connection.ImportApiKey;

	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No import API key configured"));
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No import API key configured.\n\nPlease configure the Gridly plugin settings:\n1. Go to Project Settings > Plugins > Gridly\n2. Set the Import API Key\n3. Add at least one Import View ID")));
		}
		bSourceChangesDownloadInProgress = false;
		return;
	}

	if (Connection.ImportFromViewIds.Num() == 0 || Connection.ImportFromViewIds[0].IsEmpty())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No import view ID configured"));
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No import view ID configured.\n\nPlease configure the Gridly plugin settings:\n1. Go to Project Settings > Plugins > Gridly\n2. Add at least one Import View ID")));
		}
		bSourceChangesDownloadInProgress = false;
		return;
	}

	// Reset pagination state for this download. We accumulate records across pages and only
	// process them once the final page has come back.
	CurrentSourceDownloadTarget = LocalizationTarget;
	CurrentSourceDownloadCulture = NativeCulture;
	AccumulatedSourceNamespaceRecords.Reset();
	CurrentSourceDownloadOffset = 0;
	SourceDownloadTotalCount = 0;
	SourceDownloadLimit = FMath::Max(1, Connection.ImportMaxRecordsPerRequest);
	bSourceChangesDownloadInProgress = true;

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🔄 Downloading source changes from Gridly for target: %s, culture: %s (page size %d)"),
		*LocalizationTarget->Settings.Name, *NativeCulture, SourceDownloadLimit);

	RequestSourceChangesPage(0);
}

void FGridlyLocalizationServiceProvider::RequestSourceChangesPage(int32 Offset)
{
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const FGridlyConnection Connection = GameSettings->ResolveConnectionForTarget(CurrentSourceDownloadTarget.Get());
	const FString ApiKey = Connection.ImportApiKey;
	const FString ViewId = Connection.ImportFromViewIds.Num() > 0 ? Connection.ImportFromViewIds[0] : FString();

	CurrentSourceDownloadOffset = Offset;

	const FString PaginationSettings = FGenericPlatformHttp::UrlEncode(
		FString::Printf(TEXT("{\"offset\":%d,\"limit\":%d}"), Offset, SourceDownloadLimit));

	const FString Url = FString::Printf(
		TEXT("https://api.gridly.com/v1/views/%s/records?page=%s"),
		*ViewId, *PaginationSettings);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
	HttpRequest->SetURL(Url);
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGridlyLocalizationServiceProvider::OnDownloadSourceChangesFromGridly);
	HttpRequest->ProcessRequest();

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("➡️ Requesting source changes page: offset=%d limit=%d"), Offset, SourceDownloadLimit);
}

void FGridlyLocalizationServiceProvider::OnDownloadSourceChangesFromGridly(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to download source changes from Gridly"));
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ Failed to download source changes from Gridly. Please check your API key and view ID.")));
		}
		bSourceChangesDownloadInProgress = false;
		return;
	}

	const FString ResponseContent = Response->GetContentAsString();

	// Capture total count from the first page (Gridly returns it on every page; using the first
	// guarantees we don't loop forever if a later page somehow lies about the total).
	if (CurrentSourceDownloadOffset == 0)
	{
		const FString TotalCountHeader = Response->GetHeader(TEXT("X-Total-Count"));
		SourceDownloadTotalCount = FCString::Atoi(*TotalCountHeader);
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📥 Total records reported by Gridly: %d"), SourceDownloadTotalCount);
	}

	TArray<TSharedPtr<FJsonValue>> RecordsArray;
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(ResponseContent);

	if (!FJsonSerializer::Deserialize(JsonReader, RecordsArray))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to parse JSON response from Gridly"));
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ Failed to parse response from Gridly.")));
		}
		bSourceChangesDownloadInProgress = false;
		return;
	}

	// Accumulate this page's records into the running map. The actual CSV generation /
	// string-table import runs only after the last page (see end of this function).
	TMap<FString, TArray<FGridlySourceRecord>>& NamespaceRecords = AccumulatedSourceNamespaceRecords;
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();

	for (const TSharedPtr<FJsonValue>& RecordValue : RecordsArray)
	{
		const TSharedPtr<FJsonObject>* RecordObject;
		if (!RecordValue->TryGetObject(RecordObject) || !RecordObject->IsValid())
		{
			continue;
		}

		FGridlySourceRecord SourceRecord;
		
		// Extract record ID
		FString RecordId;
		if ((*RecordObject)->TryGetStringField(FString(TEXT("id")), RecordId))
		{
			SourceRecord.RecordId = RecordId;
		}

		// Extract path (namespace)
		FString Path;
		if ((*RecordObject)->TryGetStringField(FString(TEXT("path")), Path))
		{
			SourceRecord.Path = Path;
		}

		// Extract source text from the native culture column
		const TArray<TSharedPtr<FJsonValue>>* CellsArray;
		if ((*RecordObject)->TryGetArrayField(TEXT("cells"), CellsArray))
		{
			for (const TSharedPtr<FJsonValue>& CellValue : *CellsArray)
			{
				const TSharedPtr<FJsonObject>* CellObject;
				if (CellValue->TryGetObject(CellObject) && CellObject->IsValid())
				{
					FString ColumnId;
					FString Value;
					
					if ((*CellObject)->TryGetStringField(FString(TEXT("columnId")), ColumnId) && 
						(*CellObject)->TryGetStringField(FString(TEXT("value")), Value))
					{
						// Check if this is the source language column
						if (ColumnId.StartsWith(GameSettings->SourceLanguageColumnIdPrefix))
						{
							const FString GridlyCulture = ColumnId.RightChop(GameSettings->SourceLanguageColumnIdPrefix.Len());
							FString Culture;
							
							// Convert Gridly culture to UE culture
							if (FGridlyCultureConverter::ConvertFromGridly(TArray<FString>(), GridlyCulture, Culture))
							{
								// Check if this matches our native culture
								if (Culture == CurrentSourceDownloadCulture)
								{
									SourceRecord.SourceText = Value;
									break;
								}
							}
						}
					}
				}
			}
		}

		// Only add records that have valid data
		if (!SourceRecord.RecordId.IsEmpty() && !SourceRecord.SourceText.IsEmpty())
		{
			FString Namespace = SourceRecord.Path;
			
			// Handle combined namespace key format
			if (GameSettings->bUseCombinedNamespaceId)
			{
				FString Key;
				if (SourceRecord.RecordId.Split(TEXT(","), &Namespace, &Key))
				{
					SourceRecord.RecordId = Key;
				}
			}

			// Clean up namespace
			Namespace = Namespace.Replace(TEXT(" "), TEXT(""));
			
			if (!Namespace.IsEmpty())
			{
				NamespaceRecords.FindOrAdd(Namespace).Add(SourceRecord);
			}
		}
	}

	const int32 PageRecordCount = RecordsArray.Num();
	const int32 NextOffset = CurrentSourceDownloadOffset + PageRecordCount;

	// Continue paginating while:
	//   - the server still says there are more records to fetch, AND
	//   - this page actually returned something (guard against an infinite loop if the API
	//     returns an empty page before we hit the reported total).
	const bool bMoreByCount = SourceDownloadTotalCount > 0 && NextOffset < SourceDownloadTotalCount;
	const bool bHasProgress = PageRecordCount > 0;

	if (bMoreByCount && bHasProgress)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📄 Fetched %d records (%d/%d). Requesting next page."),
			PageRecordCount, NextOffset, SourceDownloadTotalCount);
		RequestSourceChangesPage(NextOffset);
		return;
	}

	// If Gridly reported more records than we managed to fetch (bMoreByCount && !bHasProgress),
	// this is a partial dataset — do NOT let the deletion pass run against it, or we'd wipe
	// entries that are still present in Gridly but happened to land on a page we never got.
	const bool bDownloadCompletedFully = !bMoreByCount;
	if (!bDownloadCompletedFully)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning,
			TEXT("⚠️ Source-changes download stopped early: fetched %d/%d records. Deletion pass will be skipped."),
			NextOffset, SourceDownloadTotalCount);
	}
	else
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log,
			TEXT("✅ Finished downloading source changes. Total records received: %d, distinct namespaces: %d"),
			NextOffset, NamespaceRecords.Num());
	}

	ProcessSourceChangesForNamespaces(NamespaceRecords, bDownloadCompletedFully);
	AccumulatedSourceNamespaceRecords.Reset();
}

void FGridlyLocalizationServiceProvider::ProcessSourceChangesForNamespaces(const TMap<FString, TArray<FGridlySourceRecord>>& NamespaceRecords, bool bDownloadCompletedFully)
{
	if (!CurrentSourceDownloadTarget.IsValid())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("Invalid localization target for source changes processing"));
		bSourceChangesDownloadInProgress = false;
		return;
	}

	ULocalizationTarget* LocalizationTarget = CurrentSourceDownloadTarget.Get();
	const FString TargetName = LocalizationTarget->Settings.Name;

	// Create temporary directory for CSV files
	const FString TempDir = FPaths::ProjectSavedDir() / TEXT("Temp") / TEXT("GridlySourceChanges") / TargetName;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*TempDir))
	{
		PlatformFile.CreateDirectoryTree(*TempDir);
	}

	int32 ProcessedNamespaces = 0;
	int32 SucceededNamespaces = 0;
	TArray<FString> FailedNamespaces;
	// Namespaces that imported cleanly — only these are eligible for the deletion pass.
	TSet<FString> SucceededNamespaceNames;
	const int32 TotalNamespaces = NamespaceRecords.Num();

	for (const auto& NamespacePair : NamespaceRecords)
	{
		const FString& Namespace = NamespacePair.Key;
		const TArray<FGridlySourceRecord>& Records = NamespacePair.Value;

		ProcessedNamespaces++;
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📊 Processing namespace %d/%d: %s (%d records)"),
			ProcessedNamespaces, TotalNamespaces, *Namespace, Records.Num());

		// Generate CSV content
		FString CSVContent = TEXT("Key,SourceString\n");

		for (const FGridlySourceRecord& Record : Records)
		{
			FString EscapedSourceText = Record.SourceText;
			EscapedSourceText = EscapedSourceText.Replace(TEXT("\""), TEXT("\"\""));

			CSVContent += FString::Printf(TEXT("\"%s\",\"%s\"\n"), *Record.RecordId, *EscapedSourceText);
		}

		const FString CSVFilePath = TempDir / FString::Printf(TEXT("%s.csv"), *Namespace);

		if (!FFileHelper::SaveStringToFile(CSVContent, *CSVFilePath))
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to write CSV file for namespace '%s': %s"), *Namespace, *CSVFilePath);
			FailedNamespaces.Add(Namespace);
			continue;
		}

		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Generated CSV file for namespace '%s': %s"), *Namespace, *CSVFilePath);

		if (ImportCSVToStringTable(LocalizationTarget, Namespace, CSVFilePath))
		{
			++SucceededNamespaces;
			SucceededNamespaceNames.Add(Namespace);
		}
		else
		{
			FailedNamespaces.Add(Namespace);
		}
	}

	const bool bAllSucceeded = FailedNamespaces.Num() == 0;

	// Deletion pass. Runs only when the paginated download itself completed fully — a partial
	// dataset would falsely mark still-live Gridly entries as "missing". Individual per-namespace
	// import failures don't block the whole pass; the helper skips their namespaces internally.
	int32 DeletedEntries = 0;
	int32 AffectedTables = 0;
	FString BackupDir;
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const bool bDeletionRequested = GameSettings && GameSettings->bDeleteMissingRecordsOnDownload;
	const bool bDeletionRan = bDeletionRequested && bDownloadCompletedFully;
	if (bDeletionRequested && !bDownloadCompletedFully)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning,
			TEXT("⚠️ bDeleteMissingRecordsOnDownload is enabled but the download did not complete fully — skipping deletion to avoid data loss."));
	}
	else if (bDeletionRan)
	{
		DeleteMissingRecordsFromStringTables(NamespaceRecords, SucceededNamespaceNames, DeletedEntries, AffectedTables, BackupDir);
	}

	FString Message;
	if (bAllSucceeded)
	{
		Message = FString::Printf(
			TEXT("✅ Source changes processing completed.\n\n📊 Imported %d / %d namespaces.\n📁 CSV files: %s\n\nNext steps:\n• Save the modified string table assets\n• Run 'Gather Text' from the Localization Dashboard"),
			SucceededNamespaces, TotalNamespaces, *TempDir);
	}
	else
	{
		Message = FString::Printf(
			TEXT("⚠️ Source changes processing finished with errors.\n\n📊 Imported %d / %d namespaces.\n❌ Failed (%d): %s\n\n📁 CSV files: %s\n\nCheck the Output Log for details (search for ❌). Common causes:\n• StringTableSavePath must start with a valid mount root (e.g. '/Game/Localization/StringTables', not 'Content/...').\n• Namespace contains characters invalid in an asset name."),
			SucceededNamespaces, TotalNamespaces, FailedNamespaces.Num(), *FString::Join(FailedNamespaces, TEXT(", ")), *TempDir);
	}

	if (bDeletionRan)
	{
		Message += FString::Printf(
			TEXT("\n\n🗑️ Removed %d stale entries across %d string table(s) (entries no longer present in Gridly)."),
			DeletedEntries, AffectedTables);

		if (!BackupDir.IsEmpty())
		{
			Message += FString::Printf(TEXT("\n💾 Backup of affected string tables: %s"), *BackupDir);
		}
		else
		{
			Message += TEXT("\n⚠️ No backup written — StringTableBackupPath was empty.");
		}
	}

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("%s"), *Message);
	if (!IsRunningCommandlet())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	}
	bSourceChangesDownloadInProgress = false;
}

bool FGridlyLocalizationServiceProvider::ImportCSVToStringTable(ULocalizationTarget* LocalizationTarget, const FString& Namespace, const FString& CSVFilePath)
{
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📄 CSV file ready for import: %s"), *CSVFilePath);
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🏷️ Namespace: %s, Target: %s"), *Namespace, *LocalizationTarget->Settings.Name);
	
	// Load the full file so the parser can handle quoted fields that span multiple physical lines.
	// LoadFileToStringArray splits on every CR/LF and would corrupt records whose SourceString contains a newline.
	FString CSVContent;
	if (!FFileHelper::LoadFileToString(CSVContent, *CSVFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to read CSV file: %s"), *CSVFilePath);
		return false;
	}

	TArray<TArray<FString>> Records;
	ParseCSVRecords(CSVContent, Records);

	if (Records.Num() < 2) // Need header + at least one data row
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ CSV file is empty or has no data rows: %s"), *CSVFilePath);
		return false;
	}

	const TArray<FString>& HeaderFields = Records[0];
	if (HeaderFields.Num() < 2)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Invalid CSV header format"));
		return false;
	}

	if (!HeaderFields[0].Contains(TEXT("Key")) || !HeaderFields[1].Contains(TEXT("SourceString")))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ CSV header must contain 'Key' and 'SourceString' columns"));
		return false;
	}

	TMap<FString, FString> KeyValuePairs;
	for (int32 i = 1; i < Records.Num(); ++i)
	{
		const TArray<FString>& Fields = Records[i];
		if (Fields.Num() < 2)
		{
			continue;
		}

		const FString& Key = Fields[0];
		const FString& Value = Fields[1];

		if (!Key.IsEmpty() && !Value.IsEmpty())
		{
			KeyValuePairs.Add(Key, Value);
		}
	}

	if (KeyValuePairs.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ No valid key-value pairs found in CSV: %s"), *CSVFilePath);
		return false;
	}

	// Get the localization target's manifest path
	const FString ConfigFilePath = LocalizationConfigurationScript::GetGatherTextConfigPath(LocalizationTarget);
	const FString SectionName = TEXT("CommonSettings");
	
	FString SourcePath;
	if (!GConfig->GetString(*SectionName, TEXT("SourcePath"), SourcePath, ConfigFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No source path specified in config"));
		return false;
	}
	
	FString ManifestName;
	if (!GConfig->GetString(*SectionName, TEXT("ManifestName"), ManifestName, ConfigFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No manifest name specified in config"));
		return false;
	}
	
	// Determine manifest path
	const FString ConfigFullPath = FPaths::ConvertRelativePathToFull(ConfigFilePath);
	const FString EngineFullPath = FPaths::ConvertRelativePathToFull(FPaths::EngineConfigDir());
	const bool bIsEngineManifest = ConfigFullPath.StartsWith(EngineFullPath);
	
	FString ManifestPath;
	if (bIsEngineManifest)
	{
		ManifestPath = FPaths::Combine(*FPaths::EngineDir(), *SourcePath, *ManifestName);
	}
	else
	{
		ManifestPath = FPaths::Combine(*FPaths::ProjectDir(), *SourcePath, *ManifestName);
	}
	
	ManifestPath = FPaths::ConvertRelativePathToFull(ManifestPath);

	// Import into string table using the passed localization target
	bool bSuccess = ImportKeyValuePairsToStringTable(LocalizationTarget, Namespace, KeyValuePairs);
	
	if (bSuccess)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Successfully imported %d entries for namespace '%s'"), 
		KeyValuePairs.Num(), *Namespace);
	}
	else
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to import entries for namespace '%s'"), *Namespace);
	}
	
	return bSuccess;
}



void FGridlyLocalizationServiceProvider::ParseCSVLine(const FString& Line, TArray<FString>& OutFields)
{
	OutFields.Empty();
	
	const TCHAR QuoteChar = TEXT('"');
	const TCHAR Delimiter = TEXT(',');
	
	bool bInsideQuotes = false;
	FString CurrentField;
	
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		TCHAR Char = Line[i];
		
		if (bInsideQuotes)
		{
			if (Char == QuoteChar)
			{
				if (i + 1 < Line.Len() && Line[i + 1] == QuoteChar)
				{
					CurrentField += QuoteChar;
					++i; // Skip the next quote
				}
				else
				{
					bInsideQuotes = false;
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
		else
		{
			if (Char == QuoteChar)
			{
				bInsideQuotes = true;
			}
			else if (Char == Delimiter)
			{
				OutFields.Add(CurrentField);
				CurrentField.Empty();
			}
			else
			{
				CurrentField += Char;
			}
		}
	}
	
	// Add the last field
	OutFields.Add(CurrentField);
}

void FGridlyLocalizationServiceProvider::ParseCSVRecords(const FString& Content, TArray<TArray<FString>>& OutRecords)
{
	OutRecords.Empty();

	const TCHAR QuoteChar = TEXT('"');
	const TCHAR Delimiter = TEXT(',');

	TArray<FString> CurrentRecord;
	FString CurrentField;
	bool bInsideQuotes = false;
	bool bFieldStarted = false;

	auto FlushField = [&]()
	{
		CurrentRecord.Add(MoveTemp(CurrentField));
		CurrentField.Reset();
		bFieldStarted = false;
	};

	auto FlushRecord = [&]()
	{
		// Drop completely empty trailing records (e.g. a final blank line),
		// but keep records that have any field content or multiple fields.
		const bool bHasContent = CurrentRecord.Num() > 1 ||
			(CurrentRecord.Num() == 1 && !CurrentRecord[0].IsEmpty());
		if (bHasContent)
		{
			OutRecords.Add(MoveTemp(CurrentRecord));
		}
		CurrentRecord.Reset();
	};

	const int32 Len = Content.Len();
	for (int32 i = 0; i < Len; ++i)
	{
		const TCHAR Ch = Content[i];

		if (bInsideQuotes)
		{
			if (Ch == QuoteChar)
			{
				// Escaped quote ("") -> literal quote
				if (i + 1 < Len && Content[i + 1] == QuoteChar)
				{
					CurrentField.AppendChar(QuoteChar);
					++i;
				}
				else
				{
					bInsideQuotes = false;
				}
			}
			else
			{
				// Newlines inside quotes are part of the field, not a record separator.
				CurrentField.AppendChar(Ch);
			}
		}
		else
		{
			if (Ch == QuoteChar && !bFieldStarted)
			{
				bInsideQuotes = true;
				bFieldStarted = true;
			}
			else if (Ch == Delimiter)
			{
				FlushField();
			}
			else if (Ch == TEXT('\r') || Ch == TEXT('\n'))
			{
				// Collapse CRLF into one record terminator.
				if (Ch == TEXT('\r') && i + 1 < Len && Content[i + 1] == TEXT('\n'))
				{
					++i;
				}
				FlushField();
				FlushRecord();
			}
			else
			{
				CurrentField.AppendChar(Ch);
				bFieldStarted = true;
			}
		}
	}

	// Flush any trailing record without a terminating newline.
	if (bFieldStarted || CurrentRecord.Num() > 0)
	{
		FlushField();
		FlushRecord();
	}
}

bool FGridlyLocalizationServiceProvider::ImportKeyValuePairsToStringTable(ULocalizationTarget* LocalizationTarget, const FString& Namespace, const TMap<FString, FString>& KeyValuePairs)
{
	if (KeyValuePairs.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ No key-value pairs to import for namespace: %s"), *Namespace);
		return true;
	}

	UE_LOG(LogGridlyLocalizationServiceProvider, Display, TEXT("🔄 Importing %d entries for namespace '%s' using direct string table modification"), KeyValuePairs.Num(), *Namespace);

	if (!LocalizationTarget)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Invalid localization target"));
		return false;
	}

	// Find or create string table asset for this namespace
	UStringTable* StringTable = FindOrCreateStringTable(Namespace);
	if (!StringTable)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to find or create string table for namespace: %s"), *Namespace);
		return false;
	}
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Display, TEXT("🎯 Working with string table: %s"), *StringTable->GetPathName());

	// Import each key-value pair directly into the string table
	int32 ImportedCount = 0;
	int32 UpdatedCount = 0;
	int32 CreatedCount = 0;

	for (const auto& KeyValuePair : KeyValuePairs)
	{
		const FString& Key = KeyValuePair.Key;
		const FString& Value = KeyValuePair.Value;

		// Use the string table's mutable interface to set source strings
		FStringTable& MutableStringTable = StringTable->GetMutableStringTable().Get();
		
		// Check if entry already exists
		FString ExistingValue;
		bool bExists = MutableStringTable.GetSourceString(Key, ExistingValue);
		
		if (bExists)
		{
			// Update existing entry
			MutableStringTable.SetSourceString(Key, Value);
			UpdatedCount++;
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📝 Updated existing entry: %s = %s (was: %s)"), *Key, *Value, *ExistingValue);
		}
		else
		{
			// Create new entry
			MutableStringTable.SetSourceString(Key, Value);
			CreatedCount++;
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🆕 Created new entry: %s = %s"), *Key, *Value);
		}
		
		ImportedCount++;
	}

	// Mark the string table as modified and save it
	StringTable->Modify(true);
	StringTable->MarkPackageDirty();
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("💾 String table marked as dirty and modified: %s"), *StringTable->GetPathName());
	
	// Mark the string table as modified (user will save manually)
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📝 String table marked as modified: %s"), *StringTable->GetPathName());
	
	// Mark the asset as dirty so user knows it needs saving
	StringTable->MarkPackageDirty();

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Imported %d/%d entries for namespace '%s' (%d updated, %d created)"), 
		ImportedCount, KeyValuePairs.Num(), *Namespace, UpdatedCount, CreatedCount);
	
	return true;
}

UStringTable* FGridlyLocalizationServiceProvider::FindOrCreateStringTable(const FString& Namespace)
{
	// Try to find existing string table
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	
	FARFilter Filter;
	Filter.ClassPaths.Add(UStringTable::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	
	TArray<FAssetData> AssetList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetList);
	
	// Match strictly on asset name == namespace. The previous loose Contains("/%s") check
	// would match every asset in the project for short namespaces like "Game" (since all
	// project assets live under /Game/...), silently returning the wrong string table.
	// We also compare against AssetData.AssetName so we don't force-load every string
	// table in the project just to read its name.
	const FName NamespaceFName(*Namespace);
	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetName != NamespaceFName)
		{
			continue;
		}

		if (UStringTable* StringTable = Cast<UStringTable>(AssetData.GetAsset()))
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Display,
				TEXT("📋 Found existing string table: %s for namespace: %s"),
				*StringTable->GetPathName(), *Namespace);
			return StringTable;
		}
	}
	
	// Create a new string table asset for this namespace
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📋 Creating new string table for namespace: %s"), *Namespace);
	
	// Get the save path from plugin settings
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	FString PackagePath = GameSettings->StringTableSavePath;
	
	// Fallback to default path if setting is empty
	if (PackagePath.IsEmpty())
	{
		PackagePath = TEXT("/Game/Localization/StringTables");
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ StringTableSavePath is empty, using default path: %s"), *PackagePath);
	}
	
	// Normalize PackagePath: AssetRegistry requires a long package path starting with a mount root ('/Game/', '/Engine/', '/<Plugin>/').
	// A misconfigured StringTableSavePath (missing leading '/', backslashes, trailing '/') triggers
	// the PathTree assert `PathView[0] == '/'` inside FAssetRegistryModule::AssetCreated.
	PackagePath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		PackagePath.InsertAt(0, TEXT("/"));
	}
	while (PackagePath.Len() > 1 && PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath.LeftChopInline(1);
	}

	// Sanitize the namespace into a valid long-package object name (no path separators or invalid chars).
	FString AssetName = Namespace;
	AssetName.ReplaceInline(TEXT("\\"), TEXT("_"));
	AssetName.ReplaceInline(TEXT("/"), TEXT("_"));
	AssetName = ObjectTools::SanitizeObjectName(AssetName);

	const FString LongPackageName = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
	FText PackageNameError;
	if (!FPackageName::IsValidLongPackageName(LongPackageName, /*bIncludeReadOnlyRoots*/ false, &PackageNameError))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error,
			TEXT("❌ Invalid string table package path '%s' (namespace '%s'): %s. Check Project Settings → Plugins → Gridly → StringTableSavePath."),
			*LongPackageName, *Namespace, *PackageNameError.ToString());
		return nullptr;
	}

	// Ensure the on-disk content directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString OnDiskPath;
	if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath / TEXT(""), OnDiskPath))
	{
		if (!PlatformFile.DirectoryExists(*OnDiskPath))
		{
			PlatformFile.CreateDirectoryTree(*OnDiskPath);
		}
	}

	// Create the asset
	UPackage* Package = CreatePackage(*LongPackageName);
	if (!Package)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to create package for string table: %s"), *AssetName);
		return nullptr;
	}
	
	UStringTable* StringTable = NewObject<UStringTable>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!StringTable)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to create string table asset: %s"), *AssetName);
		return nullptr;
	}
	
	// Register the asset with the asset registry
	FAssetRegistryModule::AssetCreated(StringTable);
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Created new string table asset: %s"), *StringTable->GetPathName());
	return StringTable;
}

void FGridlyLocalizationServiceProvider::DeleteMissingRecordsFromStringTables(
	const TMap<FString, TArray<FGridlySourceRecord>>& NamespaceRecords,
	const TSet<FString>& SucceededNamespaces,
	int32& OutDeletedEntries,
	int32& OutAffectedTables,
	FString& OutBackupDir)
{
	OutDeletedEntries = 0;
	OutAffectedTables = 0;
	OutBackupDir.Empty();

	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	if (!GameSettings)
	{
		return;
	}

	// Prepare a per-run backup subfolder up front. If BackupPath is empty we still run the
	// deletion, but log a prominent warning — the user opted out of the safety net.
	FString BackupRootDir;
	FString ConfiguredBackupPath = GameSettings->StringTableBackupPath.TrimStartAndEnd();
	if (!ConfiguredBackupPath.IsEmpty())
	{
		// Normalize separators and resolve UE mount-point paths (e.g. "/Game/Backup" →
		// "<Project>/Content/Backup"). Users configuring the sibling StringTableSavePath field
		// use the /Game/... convention, so we accept the same form here. Anything that doesn't
		// resolve to a mount point is treated as a plain filesystem path.
		ConfiguredBackupPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		FString ResolvedRoot = ConfiguredBackupPath;
		if (ConfiguredBackupPath.StartsWith(TEXT("/")))
		{
			FString AsFilename;
			if (FPackageName::TryConvertLongPackageNameToFilename(ConfiguredBackupPath / TEXT(""), AsFilename))
			{
				ResolvedRoot = AsFilename;

				// Warn if the resolved location lives inside the project's Content directory:
				// the AssetRegistry will happily pick up copied .uasset files there and treat
				// them as duplicate assets, which is almost never what the user wants.
				const FString ContentDirAbs = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
				const FString ResolvedAbs = FPaths::ConvertRelativePathToFull(ResolvedRoot);
				if (ResolvedAbs.StartsWith(ContentDirAbs))
				{
					UE_LOG(LogGridlyLocalizationServiceProvider, Warning,
						TEXT("⚠️ StringTableBackupPath '%s' resolves inside the project Content directory (%s). This can confuse the AssetRegistry — consider picking a path outside /Content."),
						*ConfiguredBackupPath, *ResolvedAbs);
				}
			}
		}

		const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H%M%S"));
		BackupRootDir = FPaths::Combine(ResolvedRoot, FString::Printf(TEXT("GridlyDownloadBackup_%s"), *Timestamp));
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*BackupRootDir))
		{
			if (!PlatformFile.CreateDirectoryTree(*BackupRootDir))
			{
				UE_LOG(LogGridlyLocalizationServiceProvider, Error,
					TEXT("❌ Failed to create backup directory '%s'. Aborting deletion pass to avoid unrecoverable changes."), *BackupRootDir);
				return;
			}
		}
		OutBackupDir = FPaths::ConvertRelativePathToFull(BackupRootDir);
		UE_LOG(LogGridlyLocalizationServiceProvider, Display, TEXT("🗂️ Backing up string tables to: %s"), *OutBackupDir);
	}
	else
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning,
			TEXT("⚠️ StringTableBackupPath is empty — proceeding without backup. Set it in Project Settings → Plugins → Gridly for safety."));
	}

	// Build namespace -> expected keys map (only for succeeded namespaces).
	TMap<FString, TSet<FString>> ExpectedKeysByNamespace;
	for (const auto& NamespacePair : NamespaceRecords)
	{
		if (!SucceededNamespaces.Contains(NamespacePair.Key))
		{
			continue;
		}
		TSet<FString>& KeySet = ExpectedKeysByNamespace.FindOrAdd(NamespacePair.Key);
		for (const FGridlySourceRecord& Record : NamespacePair.Value)
		{
			if (!Record.RecordId.IsEmpty())
			{
				KeySet.Add(Record.RecordId);
			}
		}
	}

	if (ExpectedKeysByNamespace.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("ℹ️ Deletion pass: no eligible namespaces to prune."));
		return;
	}

	for (const auto& ExpectedPair : ExpectedKeysByNamespace)
	{
		const FString& Namespace = ExpectedPair.Key;
		const TSet<FString>& ExpectedKeys = ExpectedPair.Value;

		// Find the existing UStringTable. The import pass already created any table we needed,
		// so calling FindOrCreateStringTable here effectively finds the existing asset.
		UStringTable* StringTable = FindOrCreateStringTable(Namespace);
		if (!StringTable)
		{
			// bOnlyDeleteEntriesWhenStringTableExists=true: skip silently — never create a table
			// just to prune it. When false, we still can't prune "nothing", so also skip.
			UE_LOG(LogGridlyLocalizationServiceProvider, Verbose,
				TEXT("Deletion pass: no string table found for namespace '%s' — skipping."), *Namespace);
			continue;
		}

		FStringTable& MutableStringTable = StringTable->GetMutableStringTable().Get();

		// Collect current keys first so we can mutate while iterating safely.
		TArray<FString> ExistingKeys;
		MutableStringTable.EnumerateSourceStrings([&ExistingKeys](const FString& InKey, const FString& /*InSource*/) -> bool
		{
			ExistingKeys.Add(InKey);
			return true; // continue enumeration
		});

		TArray<FString> KeysToRemove;
		for (const FString& Key : ExistingKeys)
		{
			if (!ExpectedKeys.Contains(Key))
			{
				KeysToRemove.Add(Key);
			}
		}

		if (KeysToRemove.Num() == 0)
		{
			continue;
		}

		// Back up before modifying. If backup was configured but fails, bail on this table.
		if (!BackupRootDir.IsEmpty() && !BackupStringTablePackage(StringTable, BackupRootDir))
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error,
				TEXT("❌ Backup failed for '%s' — leaving %d stale entries untouched."),
				*StringTable->GetPathName(), KeysToRemove.Num());
			continue;
		}

		StringTable->Modify(true);
		for (const FString& Key : KeysToRemove)
		{
			MutableStringTable.RemoveSourceString(FTextKey(Key));
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🗑️ Removed stale entry '%s' from namespace '%s'"), *Key, *Namespace);
			++OutDeletedEntries;
		}
		StringTable->MarkPackageDirty();
		++OutAffectedTables;

		UE_LOG(LogGridlyLocalizationServiceProvider, Display,
			TEXT("🗑️ Pruned %d entries from '%s' (namespace '%s')"),
			KeysToRemove.Num(), *StringTable->GetPathName(), *Namespace);
	}
}

bool FGridlyLocalizationServiceProvider::BackupStringTablePackage(const UStringTable* StringTable, const FString& BackupRootDir) const
{
	if (!StringTable)
	{
		return false;
	}

	const UPackage* Package = StringTable->GetOutermost();
	if (!Package)
	{
		return false;
	}

	const FString LongPackageName = Package->GetName();

	// Resolve the on-disk .uasset path. Freshly created (unsaved) packages have no file yet —
	// there is nothing to lose, so treat that as success.
	FString SourceFilePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(LongPackageName, SourceFilePath, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning,
			TEXT("Backup: could not resolve on-disk path for package '%s'."), *LongPackageName);
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*SourceFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log,
			TEXT("Backup: no existing .uasset for '%s' (never saved) — nothing to back up."), *LongPackageName);
		return true;
	}

	// Mirror the package path under BackupRootDir so multiple tables don't collide.
	// Example: /Game/Localization/StringTables/UI -> <BackupRoot>/Game/Localization/StringTables/UI.uasset
	FString RelativePackagePath = LongPackageName;
	if (RelativePackagePath.StartsWith(TEXT("/")))
	{
		RelativePackagePath.RightChopInline(1);
	}
	const FString DestFilePath = FPaths::Combine(BackupRootDir, RelativePackagePath) + FPackageName::GetAssetPackageExtension();

	const FString DestDir = FPaths::GetPath(DestFilePath);
	if (!DestDir.IsEmpty() && !PlatformFile.DirectoryExists(*DestDir))
	{
		if (!PlatformFile.CreateDirectoryTree(*DestDir))
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Backup: failed to create dir '%s'."), *DestDir);
			return false;
		}
	}

	if (!PlatformFile.CopyFile(*DestFilePath, *SourceFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Backup: copy failed: '%s' -> '%s'"), *SourceFilePath, *DestFilePath);
		return false;
	}

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("💾 Backed up '%s' -> '%s'"), *SourceFilePath, *DestFilePath);
	return true;
}





FString FGridlyLocalizationServiceProvider::RemoveNamespaceFromKey(FString& InputString)
{

	// Find the first comma and chop the string from the right if a comma exists
	int32 CommaIndex;
	if (InputString.FindChar(TEXT(','), CommaIndex))
	{
		return InputString.RightChop(CommaIndex + 1);
	}

	// Return the string as-is if no comma is found
	return InputString;
}





#undef LOCTEXT_NAMESPACE