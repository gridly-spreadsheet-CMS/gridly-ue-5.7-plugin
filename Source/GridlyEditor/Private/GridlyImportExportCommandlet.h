// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#include "LocalizationCommandletExecution.h"
#include "ILocalizationServiceProvider.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"
#include "GridlyImportExportCommandlet.generated.h"

// Forward declarations
class ULocalizationTarget;

/**
 *	GridlyImportExportCommandlet: Commandlet to Export Native Texts to Gridy and Import translations from Gridly.
 */
UCLASS()
class UGridlyImportExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGridlyImportExportCommandlet(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{}

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	TArray<FString> CulturesToDownload;
	TArray<FString> DownloadedFiles;

	void OnDownloadComplete(const FLocalizationServiceOperationRef& Operation, ELocalizationServiceOperationCommandResult::Type Result, bool bIsTargetSet);
	void BlockingRunLocCommandletTask(const TArray<LocalizationCommandletExecution::FTask>& LocTasks);
};