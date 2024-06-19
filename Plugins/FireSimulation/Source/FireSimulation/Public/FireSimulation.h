// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FFireSimulationModule : public IModuleInterface
{
public:
	static FFireSimulationModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FFireSimulationModule>("FireSimulation");
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("FireSimulation");
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
