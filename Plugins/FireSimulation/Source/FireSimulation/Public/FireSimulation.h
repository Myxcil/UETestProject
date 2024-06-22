// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "FireSimulationConfig.h"

class UTextureRenderTargetVolume;

class FIRESIMULATION_API FFireSimulationModule final : public IModuleInterface
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

	void Initialize(const FVector& Size, const FFireSimulationConfig& Config);
	void Dispatch(float TimeStep, const FFireSimulationConfig& Config);

private:
	struct FBufferDesc
	{
		FIntVector Resolution = FIntVector::ZeroValue;
		FIntVector Bounds = FIntVector::ZeroValue;
		FVector3f RcpSize = FVector3f::ZeroVector;
		FIntVector ThreadCount = FIntVector::ZeroValue;

		void Init(FIntVector Res);
	};
	
	void DispatchRenderThread(float TimeStep, const FFireSimulationConfig& Config, FRHICommandListImmediate& CommandList);

	FVector3f LocalSize = FVector3f::ZeroVector;
	FVector2f TScale = FVector2f::ZeroVector;
	FVector3f WorldToGrid = FVector3f::ZeroVector;
	
	FBufferDesc Velocity;
	FBufferDesc Fluid;

	TRefCountPtr<IPooledRenderTarget> PrevVelocity;
	TRefCountPtr<IPooledRenderTarget> PrevFluid;
	TRefCountPtr<IPooledRenderTarget> Obstacles;
};
