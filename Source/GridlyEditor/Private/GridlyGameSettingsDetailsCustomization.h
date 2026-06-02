// Copyright (c) 2021 LocalizeDirect AB

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

class FGridlyGameSettingsDetailsCustomization final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnRefreshTargetNamesClicked();

	TWeakObjectPtr<class UGridlyGameSettings> SettingsBeingCustomized;
	IDetailLayoutBuilder* CachedDetailBuilder = nullptr;
};
