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

	void Initialize(UWorld* World, FVector Size, const FFireSimulationConfig& Config);
	void Deinitialize();
	void Dispatch(float TimeStep, const FFireSimulationConfig& Config);

private:
	struct FShaderBuffer
	{
		EPixelFormat Format = PF_FloatRGBA;
		FIntVector Resolution = FIntVector::ZeroValue;
		FIntVector Bounds = FIntVector::ZeroValue;
		FVector3f RcpSize = FVector3f::ZeroVector;
		const TCHAR* DebugName = nullptr;

		void Initialize(const FIntVector Res, const bool bIsFloat4, const TCHAR* Name);
	};
	
	void DispatchRenderThread(float TimeStep, const FFireSimulationConfig& Config, FRHICommandListImmediate& CommandList);
	static FRDGTextureRef CreateTexture(FRDGBuilder& GraphBuilder, const FShaderBuffer& Buffer);
	static FRDGTextureUAVRef CreateUAV(FRDGBuilder& GraphBuilder, const FShaderBuffer& Buffer);
	static void SwapBuffer(FShaderBuffer Buffer[2]);

	void AdvectFluid(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config);
	void AdvectVelocity(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config);

	void ApplyBuoyancy(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);
	void HandleExtinguish(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);

	void CalculateVorticity(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);

	void UpdateConfinement(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);

	void CalculateDivergence(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);
	void SolvePressure(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);
	void DoProjection(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config);

	void ClearAllBuffer(FRDGBuilder& GraphBuilder);
	static void ClearBuffer(FRDGBuilder& GraphBuilder, FIntVector3 Resolution, const FShaderBuffer& Buffer);

	FVector3f LocalSize = FVector3f::ZeroVector;
	FVector2f TScale = FVector2f::ZeroVector;
	FVector3f WorldToGrid = FVector3f::ZeroVector;
	
	bool bBuffersInitialized = false;
	FShaderBuffer Obstacles;
	FShaderBuffer Vorticity;
	FShaderBuffer Divergence;
	FShaderBuffer Velocity[2];
	FShaderBuffer Pressure[2];
	FShaderBuffer FluidData[2];
	FShaderBuffer Phi[2];
	
	FIntVector DefaultThreadCount = FIntVector::ZeroValue;
	FIntVector TransportThreadCount = FIntVector::ZeroValue;

	bool bRestartSimulation = true;
};
