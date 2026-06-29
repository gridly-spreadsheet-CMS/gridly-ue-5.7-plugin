// Copyright Epic Games, Inc. All Rights Reserved.

#include "GridlyImportExportCommandlet.h"
#include "GridlyLocalizationServiceProvider.h"
#include "Modules/ModuleManager.h"
#include "ILocalizationServiceModule.h"
#include "LocalizationModule.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "LocalizationConfigurationScript.h"
#include "LocalizationCommandletExecution.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include <GridlyGameSettings.h>

DEFINE_LOG_CATEGORY_STATIC(LogGridlyImportExportCommandlet, Log, All);

#define LOCTEXT_NAMESPACE "GridlyImportExportCommandlet"

/**
*	UGridlyImportExportCommandlet
*/
int32 UGridlyImportExportCommandlet::Main(const FString& Params)
{
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("=== GridlyImportExportCommandlet Main() called ==="));
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("GridlyImportExportCommandlet started with params: %s"), *Params);
	
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Debug: Log all parsed parameters
	UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("Parsed parameters:"));
	for (const auto& Param : ParamVals)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("  %s = %s"), *Param.Key, *Param.Value);
	}
	
	UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("Commandlet execution started successfully"));

	// Load localization module and its dependencies
	FModuleManager::Get().LoadModule(TEXT("LocalizationDashboard"));
	
	UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("LocalizationDashboard module loaded"));
	
	// Debug: Check if we have localization targets
	const TArray<ULocalizationTarget*> DebugLocalizationTargets = ULocalizationSettings::GetGameTargetSet()->TargetObjects;
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Found %d localization targets"), DebugLocalizationTargets.Num());
	
	for (int32 i = 0; i < DebugLocalizationTargets.Num(); i++)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Target %d: %s"), i, *DebugLocalizationTargets[i]->GetName());
	}

	ILocalizationServiceModule& LocServiceModule = ILocalizationServiceModule::Get();
	LocServiceModule.SetProvider("Gridly");

	ILocalizationServiceProvider& LocServProvider = ILocalizationServiceModule::Get().GetProvider();

	const bool bCanUseGridly = LocServProvider.IsEnabled() && LocServProvider.IsAvailable() && LocServProvider.GetName().ToString() == TEXT("Gridly");

	FGridlyLocalizationServiceProvider* GridlyProvider = bCanUseGridly ? static_cast<FGridlyLocalizationServiceProvider*>(&LocServProvider) : nullptr;
	if (!GridlyProvider)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("Unable to retrieve Gridly Provider."));
		return -1;
	}

	// Set config
	FString ConfigPath;
	if (const FString* ConfigParamVal = ParamVals.Find(FString(TEXT("Config"))))
	{
		ConfigPath = FConfigCacheIni::NormalizeConfigIniPath(*ConfigParamVal);
	}
	else
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No config specified."));
		return -1;
	}

	// Reading ExportAllGameTarget argument
	FString* ExportAllGameTargetPtr = ParamVals.Find(FString(TEXT("ExportAllGameTarget")));

	// Set config section
	FString SectionName;
	if (const FString* ConfigSectionParamVal = ParamVals.Find(FString(TEXT("Section"))))
	{
		SectionName = *ConfigSectionParamVal;
	}
	else
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No config section specified."));
		return -1;
	}

	bool bDoImport = false;
	GConfig->GetBool(*SectionName, TEXT("bImportLoc"), bDoImport, ConfigPath);

	bool bDoExport = false;
	GConfig->GetBool(*SectionName, TEXT("bExportLoc"), bDoExport, ConfigPath);

	bool bDoDownloadSourceChanges = false;
	GConfig->GetBool(*SectionName, TEXT("bDownloadSourceChanges"), bDoDownloadSourceChanges, ConfigPath);

	if (!bDoImport && !bDoExport && !bDoDownloadSourceChanges)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No operation detected. Use bExportLoc, bImportLoc, or bDownloadSourceChanges in config section."));
		return -1;
	}

	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("=== CONFIGURATION ==="));
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Operations: Import=%s, Export=%s, DownloadSourceChanges=%s"), 
		bDoImport ? TEXT("true") : TEXT("false"),
		bDoExport ? TEXT("true") : TEXT("false"),
		bDoDownloadSourceChanges ? TEXT("true") : TEXT("false"));
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Config Path: %s"), *ConfigPath);
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Section Name: %s"), *SectionName);

	const TArray<ULocalizationTarget*> LocalizationTargets = ULocalizationSettings::GetGameTargetSet()->TargetObjects;
	UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("Found %d localization targets"), LocalizationTargets.Num());
	
	if (LocalizationTargets.Num() == 0)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("No localization targets found!"));
		return 0;
	}
	
	//ULocalizationTarget* FirstLocTarget = LocalizationTargets.Num() > 0 ? LocalizationTargets[0]: nullptr;
	UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("=== STARTING MAIN LOOP ==="));
	for (ULocalizationTarget* LocTarget : LocalizationTargets)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Processing target: %s"), LocTarget ? *LocTarget->GetName() : TEXT("NULL"));
		// Do something with LocTarget
		if (LocTarget != nullptr)
		{
			UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("Target is valid, checking operations..."));
			UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("bDoImport=%s, bDoExport=%s, bDoDownloadSourceChanges=%s"), 
				bDoImport ? TEXT("true") : TEXT("false"),
				bDoExport ? TEXT("true") : TEXT("false"),
				bDoDownloadSourceChanges ? TEXT("true") : TEXT("false"));
			
			if (bDoImport)
			{
				// List all cultures (even the native one in case some native translations have been modified in Gridly) to download
				TArray<FString> Cultures;
				for (int ItCulture = 0; ItCulture < LocTarget->Settings.SupportedCulturesStatistics.Num(); ItCulture++)
				{
					if (ItCulture != LocTarget->Settings.NativeCultureIndex)
					{
						const FCultureStatistics CultureStats = LocTarget->Settings.SupportedCulturesStatistics[ItCulture];
						Cultures.Add(CultureStats.CultureName);
					}
				}

				// Download cultures from Gridly
				CulturesToDownload.Append(Cultures);
				for (const FString& CultureName : Cultures)
				{
					ILocalizationServiceProvider& Provider = ILocalizationServiceModule::Get().GetProvider();
					TSharedRef<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadTargetFileOp =
						ILocalizationServiceOperation::Create<FDownloadLocalizationTargetFile>();
					DownloadTargetFileOp->SetInTargetGuid(LocTarget->Settings.Guid);
					DownloadTargetFileOp->SetInLocale(CultureName);

					FString Path = FPaths::ProjectSavedDir() / "Temp" / "Game" / LocTarget->Settings.Name / CultureName /
						LocTarget->Settings.Name + ".po";
					FPaths::MakePathRelativeTo(Path, *FPaths::ProjectDir());
					FString NormalizedPath = FPaths::ConvertRelativePathToFull(Path);
					DownloadTargetFileOp->SetInRelativeOutputFilePathAndName(Path);

					auto OperationCompleteDelegate = FLocalizationServiceOperationComplete::CreateUObject(this,
						&UGridlyImportExportCommandlet::OnDownloadComplete, false);

					Provider.Execute(DownloadTargetFileOp, TArray<FLocalizationServiceTranslationIdentifier>(),
						ELocalizationServiceOperationConcurrency::Synchronous, OperationCompleteDelegate);
				}

				// Wait for all downloads
				while (CulturesToDownload.Num())
				{
					FPlatformProcess::Sleep(0.4f);
					FHttpModule::Get().GetHttpManager().Tick(-1.f);
				}

				// Run task to import po files, it will be done on the base folder and import all po files data generated after downloading data from gridly
				if (CulturesToDownload.Num() == 0 && DownloadedFiles.Num() > 0)
				{
					const FString& DlPoFile = DownloadedFiles[0]; // retrieve first po file to deduce the base folder
					const FString TargetName = FPaths::GetBaseFilename(DlPoFile);
					const auto Target = ILocalizationModule::Get().GetLocalizationTargetByName(TargetName, false);

					const FString DirectoryPath = FPaths::GetPath(DlPoFile);
					const FString DownloadBasePath = FPaths::GetPath(DirectoryPath);

					// Create commandlet task to Import texts
					// Note that we could simply "Import all PO files" using a call to PortableObjectPipeline::ImportAll(...), though
					//		using tasks we are able to easily add/remove call to existing localization functionalities
					TArray<LocalizationCommandletExecution::FTask> Tasks;
					const bool ShouldUseProjectFile = !Target->IsMemberOfEngineTargetSet();

					// Normalize Import config path
					FString ImportScriptPath = LocalizationConfigurationScript::GetImportTextConfigPath(Target, TOptional<FString>());
					ImportScriptPath = FConfigCacheIni::NormalizeConfigIniPath(ImportScriptPath);
					LocalizationConfigurationScript::GenerateImportTextConfigFile(Target, TOptional<FString>(), DownloadBasePath).WriteWithSCC(ImportScriptPath);
					Tasks.Add(LocalizationCommandletExecution::FTask(LOCTEXT("ImportTaskName", "Import Translations"), ImportScriptPath, ShouldUseProjectFile));

					// Normalize Report config path
					FString ReportScriptPath = LocalizationConfigurationScript::GetWordCountReportConfigPath(Target);
					ReportScriptPath = FConfigCacheIni::NormalizeConfigIniPath(ReportScriptPath);
					LocalizationConfigurationScript::GenerateWordCountReportConfigFile(Target).WriteWithSCC(ReportScriptPath);
					Tasks.Add(LocalizationCommandletExecution::FTask(LOCTEXT("ReportTaskName", "Generate Reports"), ReportScriptPath, ShouldUseProjectFile));


					// Function will block until all tasks have been run
					BlockingRunLocCommandletTask(Tasks);
				}

				// Cleanup
				CulturesToDownload.Empty();
				DownloadedFiles.Empty();
			}

			if (bDoExport)
			{
				UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("Running gather text task before exporting to Gridly."));
				// Generate gather config file
				FString GatherScriptPath = LocalizationConfigurationScript::GetGatherTextConfigPath(LocTarget);
				GatherScriptPath = FConfigCacheIni::NormalizeConfigIniPath(GatherScriptPath);

				LocalizationConfigurationScript::GenerateGatherTextConfigFile(LocTarget).WriteWithSCC(GatherScriptPath);

				const bool bUseProjectFile = !LocTarget->IsMemberOfEngineTargetSet();

				LocalizationCommandletExecution::FTask GatherTask(
					LOCTEXT("GatherTaskName", "Gather Text"),
					GatherScriptPath,
					bUseProjectFile
				);

				// Run Gather before Export
				BlockingRunLocCommandletTask({ GatherTask });

				FHttpRequestCompleteDelegate ReqDelegate = GridlyProvider->CreateExportNativeCultureDelegate();
				const FText SlowTaskText = LOCTEXT("ExportNativeCultureForTargetToGridlyText", "Exporting native culture for target to Gridly");

				GridlyProvider->ExportForTargetToGridly(LocTarget, ReqDelegate, SlowTaskText);

				// Wait for export requests to complete
				while (GridlyProvider->HasRequestsPending())
				{
					FPlatformProcess::Sleep(0.4f);
					FHttpModule::Get().GetHttpManager().Tick(-1.f);
				}

				const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
				if (GameSettings && GameSettings->bSyncRecords)
				{
					UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("Fetching Gridly CSV to check for stale records to delete..."));
					UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("First check: HasDeleteRequestsPending = %s"),
						GridlyProvider->HasDeleteRequestsPending() ? TEXT("true") : TEXT("false"));

					// Wait for delete requests to finish
					while (GridlyProvider->HasDeleteRequestsPending())
					{
						FPlatformProcess::Sleep(0.4f);
						FHttpModule::Get().GetHttpManager().Tick(-1.f);
					}
					UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("All record deletions completed."));

				}

			}

			UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("=== CHECKING DOWNLOAD SOURCE CHANGES ==="));
			UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("bDoDownloadSourceChanges = %s"), bDoDownloadSourceChanges ? TEXT("true") : TEXT("false"));
			
			if (bDoDownloadSourceChanges)
			{
				UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("=== RUNNING DOWNLOAD SOURCE CHANGES TASK ==="));
				UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("Running Download Source Changes task for target: %s"), *LocTarget->Settings.Name);
				
				// Get the native culture for source strings
				FString NativeCulture;
				if (LocTarget->Settings.SupportedCulturesStatistics.IsValidIndex(LocTarget->Settings.NativeCultureIndex))
				{
					const FCultureStatistics CultureStats = LocTarget->Settings.SupportedCulturesStatistics[LocTarget->Settings.NativeCultureIndex];
					NativeCulture = CultureStats.CultureName;
				}
				else
				{
					UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No native culture found for target: %s"), *LocTarget->Settings.Name);
					continue;
				}

				// Check if we have any supported cultures
				if (LocTarget->Settings.SupportedCulturesStatistics.Num() == 0)
				{
					UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No supported cultures found for target: %s"), *LocTarget->Settings.Name);
					continue;
				}

				// Call the EXACT SAME FUNCTION that the UI uses
				ILocalizationServiceModule& UILocServiceModule = ILocalizationServiceModule::Get();
				ILocalizationServiceProvider& UILocServProvider = UILocServiceModule.GetProvider();
				
				const bool bCanUseUIGridly = UILocServProvider.IsEnabled() && UILocServProvider.IsAvailable() && UILocServProvider.GetName().ToString() == TEXT("Gridly");
				
				if (bCanUseUIGridly)
				{
					FGridlyLocalizationServiceProvider* UIGridlyProvider = static_cast<FGridlyLocalizationServiceProvider*>(&UILocServProvider);
					if (UIGridlyProvider)
					{
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔄 Running Download Source Changes via GridlyProvider..."));

					// Delegate to the provider so the commandlet inherits the UI flow's logic
					// (pagination, settings-driven column prefix, culture conversion, combined-namespace handling).
					UIGridlyProvider->DownloadSourceChangesFromGridlyInternal(LocTarget, NativeCulture);

					// The provider's flow is asynchronous (multi-page HTTP + post-processing).
					// Wait until it signals completion before running our save/gather post-processing.
					while (UIGridlyProvider->IsSourceChangesDownloadInProgress())
					{
						FPlatformProcess::Sleep(0.1f);
						FHttpModule::Get().GetHttpManager().Tick(-1.f);
					}
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("✅ Download Source Changes completed"));
					
					// Save the localization target to persist changes
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("💾 Saving localization target: %s"), *LocTarget->Settings.Name);
					LocTarget->SaveConfig();
					
					// Save all modified string table packages to disk (automated version of UI)
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("💾 Saving modified string table packages to disk..."));
					for (TObjectIterator<UPackage> PackageIt; PackageIt; ++PackageIt)
					{
						UPackage* Package = *PackageIt;
						if (Package && Package->IsDirty())
						{
							FString PackageName = Package->GetName();
							if (PackageName.Contains(TEXT("StringTable")) || PackageName.Contains(TEXT("new_table_56")))
							{
								UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("💾 Saving dirty package: %s"), *PackageName);
								
								// Get the package file path
								FString PackagePath = Package->GetLoadedPath().GetPackageName();
								UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 Package path: %s"), *PackagePath);
								
								// Handle packages with empty paths (newly created packages)
								if (PackagePath.IsEmpty())
								{
									UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🆕 Newly created package detected: %s"), *PackageName);
									
									// For newly created string tables, use the configured save path from plugin settings
									const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
									FString StringTableSavePath = GameSettings->StringTableSavePath;
									if (StringTableSavePath.IsEmpty())
									{
										StringTableSavePath = TEXT("/Game/Localization/StringTables"); // Default fallback
									}
									
									// Extract just the table name from the package name (remove the path prefix)
									FString TableName = PackageName;
									if (TableName.Contains(TEXT("/")))
									{
										TableName = TableName.Mid(TableName.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
									}
									
									FString ConstructedPath = FString::Printf(TEXT("%s/%s"), *StringTableSavePath, *TableName);
									
									// Convert the package path to a relative file path
									FString RelativePath = ConstructedPath.Replace(TEXT("/Game/"), TEXT("Content/"));
									FString FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RelativePath + FPackageName::GetAssetPackageExtension());
									
									UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 Using StringTableSavePath: %s"), *StringTableSavePath);
									UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 Extracted table name: %s"), *TableName);
									UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 Constructed package path: %s"), *ConstructedPath);
									UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 Relative path: %s"), *RelativePath);
									UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 Constructed file path: %s"), *FilePath);
									
									// Save the newly created package
									FSavePackageArgs SaveArgs;
									SaveArgs.TopLevelFlags = RF_NoFlags;
									bool bSaved = UPackage::SavePackage(Package, nullptr, *FilePath, SaveArgs);
									if (bSaved)
									{
										UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("✅ Successfully saved newly created package: %s to %s"), *PackageName, *FilePath);
									}
									else
									{
										UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("❌ Failed to save newly created package: %s to %s"), *PackageName, *FilePath);
									}
									continue;
								}
								
								// Convert package path to actual file path
								FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
								UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔍 File path: %s"), *FilePath);
								
								if (!FilePath.IsEmpty())
								{
									// Save the package to disk
									FSavePackageArgs SaveArgs;
									SaveArgs.TopLevelFlags = RF_NoFlags;
									bool bSaved = UPackage::SavePackage(Package, nullptr, *FilePath, SaveArgs);
									if (bSaved)
									{
										UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("✅ Successfully saved package: %s to %s"), *PackageName, *FilePath);
									}
									else
									{
										UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("❌ Failed to save package: %s to %s"), *PackageName, *FilePath);
									}
								}
								else
								{
									UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("⚠️ No file path for package: %s"), *PackageName);
								}
							}
						}
					}
					
					// Force garbage collection to ensure all string table changes are committed
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🗑️ Forcing garbage collection to commit string table changes..."));
					CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("✅ Garbage collection completed"));
					
					// Refresh asset registry to ensure string table changes are visible
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("🔄 Refreshing asset registry to make string table changes visible..."));
					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					AssetRegistryModule.Get().ScanModifiedAssetFiles(TArray<FString>());
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("✅ Asset registry refreshed"));
					
					// Run "Gather Text" to update manifest files from the updated string tables
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("📝 Running Gather Text to update manifest files..."));
					
					// Generate gather text config file
					FString GatherScriptPath = LocalizationConfigurationScript::GetGatherTextConfigPath(LocTarget);
					LocalizationConfigurationScript::GenerateGatherTextConfigFile(LocTarget).WriteWithSCC(GatherScriptPath);
					
					const bool bUseProjectFile = !LocTarget->IsMemberOfEngineTargetSet();
					LocalizationCommandletExecution::FTask GatherTask(
						LOCTEXT("GatherTaskName", "Gather Text"),
						GatherScriptPath,
						bUseProjectFile
					);
					
					// Execute the gather text task
					BlockingRunLocCommandletTask({ GatherTask });
					UE_LOG(LogGridlyImportExportCommandlet, Display, TEXT("✅ Gather Text completed - manifest files updated"));
					}
					else
					{
						UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("❌ UIGridlyProvider cast failed"));
					}
				}
				else
				{
					UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("❌ Cannot use UI GridlyProvider"));
				}
			}
		}


		if (ExportAllGameTargetPtr && *ExportAllGameTargetPtr == "false") {
			break;
		}
	}
	return 0;
}

void UGridlyImportExportCommandlet::OnDownloadComplete(const FLocalizationServiceOperationRef& Operation, ELocalizationServiceOperationCommandResult::Type Result, bool bIsTargetSet)
{
	// do like in FGridlyLocalizationServiceProvider::OnImportCultureForTargetFromGridly
	TSharedPtr<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadLocalizationTargetOp = StaticCastSharedRef<FDownloadLocalizationTargetFile>(Operation);
	CulturesToDownload.Remove(DownloadLocalizationTargetOp->GetInLocale());

	if (Result != ELocalizationServiceOperationCommandResult::Succeeded)
	{
		const FText ErrorMessage = DownloadLocalizationTargetOp->GetOutErrorText();
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("%s"), *ErrorMessage.ToString());
	}

	const FString TargetName = FPaths::GetBaseFilename(DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

	const auto Target = ILocalizationModule::Get().GetLocalizationTargetByName(TargetName, false);
	const FString AbsoluteFilePathAndName = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

	DownloadedFiles.Add(AbsoluteFilePathAndName);
}

void UGridlyImportExportCommandlet::BlockingRunLocCommandletTask(const TArray<LocalizationCommandletExecution::FTask>& Tasks)
{
	for (const LocalizationCommandletExecution::FTask& LocTask : Tasks)
	{
		TSharedPtr<FLocalizationCommandletProcess> CommandletProcess = FLocalizationCommandletProcess::Execute(LocTask.ScriptPath, LocTask.ShouldUseProjectFile);

		if (CommandletProcess.IsValid())
		{
			UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("=== Starting Task [%s] ==="), *LocTask.Name.ToString());

			FProcHandle CurrentProcessHandle = CommandletProcess->GetHandle();
			int32 ReturnCode = INDEX_NONE;

			// This loop is the same than FCommandletLogPump::Run(), and it's used when SLocalizationCommandletExecutor widget runs localization commandlet tasks
			for (;;)
			{
				// Read from pipe.
				const FString PipeString = FPlatformProcess::ReadPipe(CommandletProcess->GetReadPipe());

				// Process buffer.
				if (!PipeString.IsEmpty())
				{
					UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("%s"), *PipeString);
				}

				// If the process isn't running and there's no data in the pipe, we're done.
				if (!FPlatformProcess::IsProcRunning(CurrentProcessHandle) && PipeString.IsEmpty())
				{
					break;
				}

				// Sleep.
				FPlatformProcess::Sleep(0.0f);
			}

			if (CurrentProcessHandle.IsValid() && FPlatformProcess::GetProcReturnCode(CurrentProcessHandle, &ReturnCode))
			{
				UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("===> Task [%s] returned : %d"), *LocTask.Name.ToString(), ReturnCode);
			}
		}
		else
		{
			UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("Failed to start Task [%s] !"), *LocTask.Name.ToString());
		}
	}
}


#undef LOCTEXT_NAMESPACE
