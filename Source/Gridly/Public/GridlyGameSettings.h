// Copyright 2020 LocalizeDirect AB

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GridlyGameSettings.generated.h"

UENUM(BlueprintType)
enum class EGridlyColumnDataType : uint8
{
    String,
    Number
};

USTRUCT(BlueprintType)
struct GRIDLY_API FGridlyColumnInfo
{
    GENERATED_USTRUCT_BODY()

public:
    UPROPERTY(EditAnywhere, Category = ColumnInfo)
    FString Name;

    UPROPERTY(EditAnywhere, Category = ColumnInfo)
    EGridlyColumnDataType DataType;
};

/** A self-contained Gridly connection (API keys + view IDs) that can be assigned per localization target. */
USTRUCT(BlueprintType)
struct GRIDLY_API FGridlyConnection
{
    GENERATED_USTRUCT_BODY()

    /** The API key used when importing for this target */
    UPROPERTY(Category = "Import", BlueprintReadWrite, EditAnywhere)
    FString ImportApiKey;

    /** The view IDs to fetch from Gridly for this target. Record IDs will be combined. Duplicate keys will be ignored */
    UPROPERTY(Category = "Import", BlueprintReadWrite, EditAnywhere)
    TArray<FString> ImportFromViewIds;

    /** The max amount of records to import on each request */
    UPROPERTY(Category = "Import", BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "1", ClampMax = "1000"))
    int ImportMaxRecordsPerRequest = 1000;

    /** The API key used when exporting for this target. Make sure you have write access */
    UPROPERTY(Category = "Export", BlueprintReadWrite, EditAnywhere)
    FString ExportApiKey;

    /** The view ID to export the source strings to for this target */
    UPROPERTY(Category = "Export", BlueprintReadWrite, EditAnywhere)
    FString ExportViewId;

    /** The max amount of records to export on each request */
    UPROPERTY(Category = "Export", BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "1", ClampMax = "1000"))
    int ExportMaxRecordsPerRequest = 1000;
};

class ULocalizationTarget;

UCLASS(BlueprintType, Config = Game, DefaultConfig,
    AutoExpandCategories = ("Gridly|Connection Mode", "Gridly|Import Settings", "Gridly|Export Settings", "Gridly|Options"))
    class GRIDLY_API UGridlyGameSettings final : public UObject
{
    GENERATED_BODY()

public:
    /**
     * When enabled, the per-target map below is used to look up the API key / view ID for each
     * localization target. When disabled, the single global Import/Export fields below are used
     * for every target (original behaviour).
     */
    UPROPERTY(Category = "Gridly|Connection Mode", BlueprintReadOnly, EditAnywhere, Config)
    bool bUsePerTargetConnections = false;

    /**
     * Per-target Gridly connections, keyed by the localization target's name (the same name
     * shown in the Localization Dashboard). Targets not listed here fall back to the global
     * Import/Export fields below.
     */
    UPROPERTY(Category = "Gridly|Connection Mode", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "bUsePerTargetConnections", GetOptions = "GetAvailableTargetNames"))
    TMap<FString, FGridlyConnection> TargetConnections;

    /** Auto-discovered list of localization target names — shown as a reference when editing TargetConnections. */
    UPROPERTY(Category = "Gridly|Connection Mode", VisibleAnywhere, Transient,
        meta = (EditCondition = "bUsePerTargetConnections"))
    TArray<FString> AvailableTargetNames;

    /** The API key can be retrieved from your Gridly dashboard */
    UPROPERTY(Category = "Gridly|Import Settings", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "!bUsePerTargetConnections"))
    FString ImportApiKey;

    /** The view IDs to fetch from Gridly. Record IDs will be combined. Duplicate keys will be ignored */
    UPROPERTY(Category = "Gridly|Import Settings", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "!bUsePerTargetConnections"))
    TArray<FString> ImportFromViewIds;

    /** The max amount of records to import on each request. This should normally be set to the API limit */
    UPROPERTY(Category = "Gridly|Import Settings|Advanced", BlueprintReadOnly, EditAnywhere, Config, meta = (ClampMin = "1", ClampMax = "1000"))
    int ImportMaxRecordsPerRequest = 1000;

    /** The API key can be retrieved from your Gridly dashboard. Make sure you have write access */
    UPROPERTY(Category = "Gridly|Export Settings", BlueprintReadOnly, EditAnywhere, Transient,
        meta = (EditCondition = "!bUsePerTargetConnections"))
    FString ExportApiKey;

    /** The view ID to export the source strings to */
    UPROPERTY(Category = "Gridly|Export Settings", BlueprintReadOnly, EditAnywhere, Transient,
        meta = (EditCondition = "!bUsePerTargetConnections"))
    FString ExportViewId;

    /** The max amount of records to export on each request. This should normally be set to the API limit */
    UPROPERTY(Category = "Gridly|Export Settings|Advanced", BlueprintReadOnly, EditAnywhere, Config, meta = (ClampMin = "1", ClampMax = "1000"))
    int ExportMaxRecordsPerRequest = 1000;

    /** Use combined comma-separated "{namespace},{key}" as record ID. WARNING! This should not be changed after a project has already been exported */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    bool bUseCombinedNamespaceId = false;

    /** Exports namespace to a separate column even if using bUseCombinedNamespaceId  */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "bUseCombinedNamespaceId"))
    bool bAlsoExportNamespaceColumn = false;

    /** Set to "path" to use Gridly's path tag functionality for namespaces. This can also be mapped to any other column of the string type  */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "!bUseCombinedNamespaceId || bAlsoExportNamespaceColumn"))
    FString NamespaceColumnId = "path";

    /** Column ID prefix for source language columns on Gridly */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    FString SourceLanguageColumnIdPrefix = "src_";

    /** Column ID prefix for target language columns on Gridly */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    FString TargetLanguageColumnIdPrefix = "tg_";

    /** By default during import and export, Gridly will try to automatically convert to and from Unreal's culture format. This behaviour can be overriden with custom mapping */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    bool bUseCustomCultureMapping = true;

    /** This will remap locale settings from Unreal to Gridly. Unreal uses "en-US", while Gridly generally uses "enUS". However, this mapping can be modified to suit the project's needs. */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "bUseCustomCultureMapping"))
    TMap<FString, FString> CustomCultureMapping;

    /** When set, will export context (SourceLocation) */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    bool bExportContext = false;

    /** Column name of context (SourceLocation) on Gridly */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "bExportContext"))
    FString ContextColumnId = "src_context";

    /** When set, metadata will also be exported to Gridly */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    bool bExportMetadata = false;

    /** When set, the recoprds that has deleted in UE, will be deleted in Gridly */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    bool bSyncRecords = true;

    /** Path where new string tables will be saved when downloading source changes from Gridly */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config, meta = (ContentDir))
    FString StringTableSavePath = "/Game/Localization/StringTables";

    /**
     * When enabled, records that exist in UE string tables but no longer exist in Gridly will be
     * removed during Download Source Changes. Deletion runs only when the full paginated download
     * completed successfully; a failed or partial download skips the deletion pass entirely.
     */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config)
    bool bDeleteMissingRecordsOnDownload = false;

    /**
     * Absolute directory path (outside the /Content tree) where affected UStringTable .uasset files
     * are copied before any entries are removed. A timestamped subfolder is created per run. Leave
     * empty to skip backups (not recommended).
     */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "bDeleteMissingRecordsOnDownload"))
    FString StringTableBackupPath;

    /**
     * When enabled, only individual records inside existing string tables are removed. A
     * UStringTable asset itself is never deleted, even if Gridly no longer has any records for it.
     * This is the safe default; disable only if you want empty tables removed as assets too.
     */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config,
        meta = (EditCondition = "bDeleteMissingRecordsOnDownload"))
    bool bOnlyDeleteEntriesWhenStringTableExists = true;

    /** This will remap metadata to specific Gridly columns during the export */
    UPROPERTY(Category = "Gridly|Options", BlueprintReadOnly, EditAnywhere, Config, meta = (EditCondition = "bExportMetadata"))
    TMap<FString, FGridlyColumnInfo> MetadataMapping;

public:
    UGridlyGameSettings(const FObjectInitializer& ObjectInitializer);

    // Helper functions for JSON serialization
    static FString SerializeArrayToJson(const TArray<FString>& Array);
    static bool DeserializeJsonToArray(const FString& JsonString, TArray<FString>& OutArray);

    /**
     * Returns the connection that should be used for the given target. When per-target
     * connections are disabled (or no entry matches the target name) this falls back to
     * a connection built from the global Import/Export fields.
     */
    FGridlyConnection ResolveConnectionForTarget(const FString& TargetName) const;
    FGridlyConnection ResolveConnectionForTarget(const ULocalizationTarget* Target) const;

    /** Refreshes AvailableTargetNames from the project's localization target set. */
    void RefreshAvailableTargetNames();

    /** Used by the TargetConnections key dropdown (GetOptions metadata). */
    UFUNCTION()
    TArray<FString> GetAvailableTargetNames() const { return AvailableTargetNames; }

public:
    static bool OnSettingsSaved();

private:
    static FString GetGridlyConfigPath();
    static void EnsureConfigFileExists(const FString& ConfigPath);
};
