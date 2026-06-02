// Copyright (c) 2021 LocalizeDirect AB

#include "GridlyGameSettingsDetailsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "GridlyGameSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GridlyGameSettingsDetails"

TSharedRef<IDetailCustomization> FGridlyGameSettingsDetailsCustomization::MakeInstance()
{
	return MakeShared<FGridlyGameSettingsDetailsCustomization>();
}

void FGridlyGameSettingsDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	CachedDetailBuilder = &DetailBuilder;

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	for (const TWeakObjectPtr<UObject>& Object : ObjectsBeingCustomized)
	{
		if (UGridlyGameSettings* Settings = Cast<UGridlyGameSettings>(Object.Get()))
		{
			SettingsBeingCustomized = Settings;
			Settings->RefreshAvailableTargetNames();
			break;
		}
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Connection Mode"),
		FText::GetEmpty(),
		ECategoryPriority::Important);

	Category.AddCustomRow(LOCTEXT("RefreshTargetsRowFilter", "Refresh Localization Target Names"))
		.WholeRowContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.OnClicked(this, &FGridlyGameSettingsDetailsCustomization::OnRefreshTargetNamesClicked)
			.ToolTipText(LOCTEXT("RefreshTargetsTooltip",
				"Re-scans the project's localization targets and updates the Available Target Names list."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RefreshTargetsButton", "Refresh Localization Target Names"))
			]
		];
}

FReply FGridlyGameSettingsDetailsCustomization::OnRefreshTargetNamesClicked()
{
	if (UGridlyGameSettings* Settings = SettingsBeingCustomized.Get())
	{
		Settings->RefreshAvailableTargetNames();
		if (CachedDetailBuilder)
		{
			CachedDetailBuilder->ForceRefreshDetails();
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
