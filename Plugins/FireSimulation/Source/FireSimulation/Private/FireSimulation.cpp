// Copyright Epic Games, Inc. All Rights Reserved.

#include "FireSimulation.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FFireSimulationModule"

void FFireSimulationModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("FireSimulation"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/FireSimulation"), PluginShaderDir);}

void FFireSimulationModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFireSimulationModule, FireSimulation)