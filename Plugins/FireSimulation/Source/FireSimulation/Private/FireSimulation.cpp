// Copyright Epic Games, Inc. All Rights Reserved.

#include "FireSimulation.h"

#include "FireShaderKernels.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetRenderingLibrary.h"

#define LOCTEXT_NAMESPACE "FFireSimulationModule"

DECLARE_STATS_GROUP(TEXT("FireSimulation"), STATGROUP_FireSimulation, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("FireSimulation Execute"), STAT_FireSimulation_Execute, STATGROUP_FireSimulation);

const FIntVector3 THREAD_COUNT = { 8, 8, 8 };

void FFireSimulationModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("FireSimulation"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/FireSimulation"), PluginShaderDir);
}

void FFireSimulationModule::ShutdownModule()
{
}

void FFireSimulationModule::Initialize(UWorld* World, FVector3f Size, const FFireSimulationConfig& Config)
{
	VelocityResolution = Config.Resolution;
	VelocityBounds =  { VelocityResolution.X-1, VelocityResolution.Y-1, VelocityResolution.Z-1 };
	RcpVelocitySize = { 1.0f / VelocityResolution.X, 1.0f / VelocityResolution.Y, 1.0f / VelocityResolution.Z };

	FluidResolution = VelocityBounds * Config.FluidResolutionScale;
	FluidBounds = { FluidResolution.X-1, FluidResolution.Y-1, FluidResolution.Z-1 };
	RcpFluidSize = { 1.0f / FluidResolution.X, 1.0f / FluidResolution.Y, 1.0f / FluidResolution.Z };
		
	LocalSize = Size;

	TScale.X = Config.FluidResolutionScale;
	TScale.Y = 1.0f / Config.FluidResolutionScale;

	WorldToGrid = { VelocityResolution.X / LocalSize.X, VelocityResolution.Y / LocalSize.Y, VelocityResolution.Z / LocalSize.Z };

	NumPressureIterations = Config.NumPressureIterations;
	
	RTObstacles = UKismetRenderingLibrary::CreateRenderTargetVolume(World, VelocityResolution.X, VelocityResolution.Y, VelocityResolution.Z, RTF_R32f, FLinearColor::Black, false, true);
}

void FFireSimulationModule::Deinitialize()
{
	RTObstacles = nullptr;
}

void FFireSimulationModule::Dispatch(float TimeStep)
{
	if (IsInRenderingThread())
	{
		DispatchRenderThread(TimeStep, GetImmediateCommandList_ForRenderCommand());
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
			[this, TimeStep](FRHICommandListImmediate& CommandList)
		{
			DispatchRenderThread(TimeStep, CommandList);
		});
	}
}

void FFireSimulationModule::DispatchRenderThread(float TimeStep, FRHICommandListImmediate& CommandList)
{
	FRDGBuilder GraphBuilder(CommandList);
	{
		SCOPE_CYCLE_COUNTER(STAT_FireSimulation_Execute);
		DECLARE_GPU_STAT(FireSimulation)
		RDG_EVENT_SCOPE(GraphBuilder, "FireSimulation");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FireSimulation);

		if (!bBuffersInitialized)
		{
			bBuffersInitialized = true;
		}
	
		if (bRestartSimulation)
		{
			bRestartSimulation = false;
			ClearAllBuffer(GraphBuilder);
		}
	
		AdvectFluid(TimeStep);
		AdvectVelocity(TimeStep);

		ApplyBuoyancy();
		HandleExtinguish();
		CalculateVorticity();
		UpdateConfinement();

		CalculateDivergence();

		for(int32 I=0; I < NumPressureIterations; ++I)
		{
			SolvePressure();
		}

		DoProjection();
	}

	GraphBuilder.Execute();
}

void FFireSimulationModule::AdvectFluid(float TimeStep)
{
	
}

void FFireSimulationModule::AdvectVelocity(float TimeStep)
{
}

void FFireSimulationModule::ApplyBuoyancy()
{
}

void FFireSimulationModule::HandleExtinguish()
{
}

void FFireSimulationModule::CalculateVorticity()
{
}

void FFireSimulationModule::UpdateConfinement()
{
}

void FFireSimulationModule::CalculateDivergence()
{
}

void FFireSimulationModule::SolvePressure()
{
}

void FFireSimulationModule::DoProjection()
{
}

void FFireSimulationModule::ClearAllBuffer(FRDGBuilder& GraphBuilder)
{
	ClearBuffer(GraphBuilder, VelocityResolution, RTObstacles);
}

void FFireSimulationModule::ClearBuffer(FRDGBuilder& GraphBuilder, FIntVector3 Resolution, UTextureRenderTargetVolume* Target)
{
	if (!Target)
		return;

	FRDGTextureDesc Desc(FRDGTextureDesc::Create3D(VelocityResolution, Target->GetFormat(), EClearBinding::ENoneBound, ETextureCreateFlags::RenderTargetable|ETextureCreateFlags::UAV));
	FRDGTextureRef TargetTexture = GraphBuilder.CreateTexture(Desc, *Target->GetName());
	const FRDGTextureUAVRef TargetRef = GraphBuilder.CreateUAV(TargetTexture);
	
	auto GroupCount = FComputeShaderUtils::GetGroupCount(Resolution, THREAD_COUNT);

	if (Target->GetFormat() == PF_R32_FLOAT)
	{
		FFireShaderClearFloatCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloatCS::FParameters>();
		Params->outputFloat = TargetRef;

		TShaderMapRef<FFireShaderClearFloatCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		
		GraphBuilder.AddPass(RDG_EVENT_NAME("ClearFloatBuffer"), Params, ERDGPassFlags::AsyncCompute,
			[&Params, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
						{
							FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Params, GroupCount);
						});
	}
	else
	{
		FFireShaderClearFloat4CS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloat4CS::FParameters>();
		Params->outputFloat4 = TargetRef;

		TShaderMapRef<FFireShaderClearFloat4CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		
		GraphBuilder.AddPass(RDG_EVENT_NAME("ClearFloat4Buffer"), Params, ERDGPassFlags::AsyncCompute,
			[&Params, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
						{
							FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Params, GroupCount);
						});
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFireSimulationModule, FireSimulation)