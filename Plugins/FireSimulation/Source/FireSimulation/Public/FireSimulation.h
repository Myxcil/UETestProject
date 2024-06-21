// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "FireSimulationConfig.h"

class UTextureRenderTargetVolume;

class FIRESIMULATION_API FFireSimulationModule : public IModuleInterface
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

	void Initialize(UWorld* World, FVector3f Size, const FFireSimulationConfig& Config);
	void Deinitialize();
	
	void Dispatch(float TimeStep);
	

protected:
	void DispatchRenderThread(float TimeStep, FRHICommandListImmediate& CommandList);

private:
	void AdvectFluid(float TimeStep);
	void AdvectVelocity(float TimeStep);

	void ApplyBuoyancy();
	void HandleExtinguish();

	void CalculateVorticity();

	void UpdateConfinement();

	void CalculateDivergence();
	void SolvePressure();
	void DoProjection();

	void ClearAllBuffer(FRDGBuilder& GraphBuilder);
	void ClearBuffer(FRDGBuilder& GraphBuilder, FIntVector3 Resolution, UTextureRenderTargetVolume* Target);

private:
	FIntVector3 VelocityResolution = FIntVector3::ZeroValue;
	FIntVector3 VelocityBounds = FIntVector3::ZeroValue;
	FVector3f RcpVelocitySize = FVector3f::ZeroVector;

	FIntVector3 FluidResolution = FIntVector3::ZeroValue;
	FIntVector3 FluidBounds = FIntVector3::ZeroValue;
	FVector3f RcpFluidSize = FVector3f::ZeroVector;

	FVector3f LocalSize = FVector3f::ZeroVector;
	FVector2f TScale = FVector2f::ZeroVector;
	FVector3f WorldToGrid = FVector3f::ZeroVector;

	int32 NumPressureIterations = 0;

	bool bBuffersInitialized = false;
	UTextureRenderTargetVolume* RTObstacles;
	

	bool bRestartSimulation = true;
};
