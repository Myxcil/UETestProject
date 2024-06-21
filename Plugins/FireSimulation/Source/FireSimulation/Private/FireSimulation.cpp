// Copyright Epic Games, Inc. All Rights Reserved.

#include "FireSimulation.h"

#include "FireShaderKernels.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Math/UnitConversion.h"

#define LOCTEXT_NAMESPACE "FFireSimulationModule"

DECLARE_STATS_GROUP(TEXT("FireSimulation"), STATGROUP_FireSimulation, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("FireSimulation Execute"), STAT_FireSimulation_Execute, STATGROUP_FireSimulation);

static const FIntVector3 THREAD_COUNT = { 8, 8, 8 };
constexpr int32 READ = 0;
constexpr int32 WRITE = 1;

void FFireSimulationModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("FireSimulation"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/FireSimulation"), PluginShaderDir);
}

void FFireSimulationModule::ShutdownModule()
{
}

static constexpr int32 SnapValues[] = { 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 128 };
static constexpr int32 NumSnapValues = sizeof(SnapValues) / sizeof(int32);

static int32 SetResolution(const float Value, const int32 MaxRes = 0)
{
	int32 IntValue = FMath::RoundToInt32(Value);
	if (IntValue <= SnapValues[0])
	{
		IntValue = SnapValues[0];
	}
	else if (IntValue >= SnapValues[NumSnapValues-1])
	{
		IntValue = SnapValues[NumSnapValues-1];
	}
	else
	{
		for(int I=1; I < NumSnapValues; ++I)
		{
			const int32 DP = IntValue - SnapValues[I-1];
			if (int32 DN = SnapValues[I] - IntValue; DP >= 0 && DN >= 0)
			{
				IntValue = DP < DN ? SnapValues[I-1] : SnapValues[I];
				break;
			}
		}
	}
	if (MaxRes > 0)
	{
		IntValue = FMath::Min(MaxRes,IntValue);
	}
	return IntValue;
}

static void GetResolution(FVector Size, float GridSize, int MaxRes, FIntVector3& OutResolution)
{
	Size /= GridSize;
	if (Size.X >= Size.Y && Size.X >= Size.Z)
	{
		OutResolution.X = SetResolution(Size.X, MaxRes);
		OutResolution.Y = SetResolution(Size.Y * OutResolution.X / Size.X);
		OutResolution.Z = SetResolution(Size.Z * OutResolution.X / Size.X);
	}
	else if (Size.Y >= Size.X && Size.Y >= Size.Z)
	{
		OutResolution.Y = SetResolution(Size.Y, MaxRes);
		OutResolution.Y = SetResolution(Size.X * OutResolution.Y / Size.Y);
		OutResolution.Z = SetResolution(Size.Z * OutResolution.Y / Size.Y);
	}
	else
	{
		OutResolution.Z = SetResolution(Size.Z, MaxRes);
		OutResolution.X = SetResolution(Size.X * OutResolution.Z / Size.Z);
		OutResolution.Y = SetResolution(Size.Y * OutResolution.Z / Size.Z);
	}
}

void FFireSimulationModule::Initialize(UWorld* World, FVector Size, const FFireSimulationConfig& Config)
{
	FIntVector Resolution;
	GetResolution(Size, Config.CellSize, Config.MaxResolution, Resolution);
	const FIntVector TransportResolution = Resolution * Config.FluidResolutionScale;
	
	Obstacles.Initialize(Resolution, false, TEXT("Obstacles"));
	Vorticity.Initialize(Resolution, true, TEXT("Vorticity"));
	Divergence.Initialize(Resolution, false, TEXT("Divergence"));
	for(int32 I=0; I < 2; ++I)
	{
		Velocity[I].Initialize(Resolution, true, TEXT("Velocity"));
		Pressure[I].Initialize(Resolution, false, TEXT("Pressure"));

		FluidData[I].Initialize(TransportResolution, true, TEXT("Fluid"));
		Phi[I].Initialize(TransportResolution, true, TEXT("Phi"));
	}

	DefaultThreadCount = FComputeShaderUtils::GetGroupCount(Resolution, THREAD_COUNT);
	TransportThreadCount = FComputeShaderUtils::GetGroupCount(TransportResolution, THREAD_COUNT);
	
	LocalSize = FVector3f(Size.X, Size.Y, Size.Z);
	TScale.X = Config.FluidResolutionScale;
	TScale.Y = 1.0f / Config.FluidResolutionScale;

	WorldToGrid = { Resolution.X / LocalSize.X, Resolution.Y / LocalSize.Y, Resolution.Z / LocalSize.Z };
}

void FFireSimulationModule::Deinitialize()
{
	bBuffersInitialized = false;
}

void FFireSimulationModule::Dispatch(float TimeStep, const FFireSimulationConfig& Config)
{
	if (!GWorld)
		return;
	
	if (IsInRenderingThread())
	{
		DispatchRenderThread(TimeStep, Config, GetImmediateCommandList_ForRenderCommand());
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
			[this, TimeStep, Config](FRHICommandListImmediate& CommandList)
		{
			DispatchRenderThread(TimeStep, Config, CommandList);
		});
	}
}

void FFireSimulationModule::FShaderBuffer::Initialize(const FIntVector Res, const bool bIsFloat4, const TCHAR* Name)
{
	Format = bIsFloat4 ? PF_FloatRGB : PF_R16F;
	Resolution = Res;
	Bounds = FIntVector(Resolution.X-1, Resolution.Y-1, Resolution.Z-1);
	RcpSize = FVector3f(1.0f/Resolution.X, 1.0f/Resolution.Y, 1.0f/Resolution.Z);
	DebugName = Name;
}

void FFireSimulationModule::DispatchRenderThread(float TimeStep, const FFireSimulationConfig& Config, FRHICommandListImmediate& CommandList)
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
		else if (bRestartSimulation)
		{
			bRestartSimulation = false;
			ClearAllBuffer(GraphBuilder);
		}
		else
		{
			AdvectFluid(GraphBuilder, TimeStep, Config);
			AdvectVelocity(GraphBuilder, TimeStep, Config);

			ApplyBuoyancy(GraphBuilder, TimeStep, Config);
			HandleExtinguish(GraphBuilder, TimeStep, Config);
			CalculateVorticity(GraphBuilder, Config);
			UpdateConfinement(GraphBuilder, TimeStep, Config);

			CalculateDivergence(GraphBuilder, Config);
			SolvePressure(GraphBuilder, Config);
			DoProjection(GraphBuilder, Config);
		}
	}

	GraphBuilder.Execute();
}

FRDGTextureRef FFireSimulationModule::CreateTexture(FRDGBuilder& GraphBuilder, FShaderBuffer& Buffer)
{
	if (Buffer.RenderTarget == nullptr)
	{
		const FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateVolumeDesc(
			Buffer.Resolution.X, Buffer.Resolution.Y, Buffer.Resolution.Z,
			Buffer.Format,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_UAV | TexCreate_ShaderResource,
			false));
		
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Buffer.RenderTarget, Buffer.DebugName);
	}
	return GraphBuilder.RegisterExternalTexture(Buffer.RenderTarget, Buffer.DebugName);
	/*
	TRefCountPtr<IPooledRenderTarget> RenderTarget = CreateRenderTarget(Buffer.Volume->GetResource()->GetTexture3DRHI(), Buffer.DebugName);
	return GraphBuilder.RegisterExternalTexture(RenderTarget, ERDGTextureFlags::None);
	*/
}

FRDGTextureUAVRef FFireSimulationModule::CreateUAV(FRDGBuilder& GraphBuilder, FShaderBuffer& Buffer)
{
	return GraphBuilder.CreateUAV(CreateTexture(GraphBuilder, Buffer));
}

void FFireSimulationModule::SwapBuffer(FShaderBuffer Buffer[2])
{
	Swap(Buffer[0], Buffer[1]);
}

void FFireSimulationModule::AdvectFluid(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config)
{
	FFireShaderPrepareFluidDataAdvectionCS::FParameters* PrepareParams = GraphBuilder.AllocParameters<FFireShaderPrepareFluidDataAdvectionCS::FParameters>();
	PrepareParams->TScale = TScale;
	PrepareParams->Forward = TimeStep;
	PrepareParams->WorldToGrid = WorldToGrid;
	PrepareParams->RcpVelocitySize = Velocity[READ].RcpSize;
	PrepareParams->RcpFluidSize = FluidData[READ].RcpSize;
	PrepareParams->_LinearClamp = TStaticSamplerState<>::GetRHI();
	
	PrepareParams->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
	PrepareParams->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
	PrepareParams->phiIn = CreateTexture(GraphBuilder, FluidData[READ]);
	PrepareParams->outputFloat4 = CreateUAV(GraphBuilder, Phi[1]);

	const auto GroupCount = TransportThreadCount;
	TShaderMapRef<FFireShaderPrepareFluidDataAdvectionCS> PrepareFluidDataAdvectCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRDGPassRef PassPrepareForward = GraphBuilder.AddPass(RDG_EVENT_NAME("Prepare Fluid Advection Fwd"), PrepareParams, ERDGPassFlags::AsyncCompute,
		[&PrepareParams, PrepareFluidDataAdvectCS, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, PrepareFluidDataAdvectCS, *PrepareParams, GroupCount);
					});

	PrepareParams->Forward = -TimeStep;
	PrepareParams->phiIn =  CreateTexture(GraphBuilder, Phi[1]);
	PrepareParams->outputFloat4 = CreateUAV(GraphBuilder, Phi[0]);

	FRDGPassRef PassPrepareBack = GraphBuilder.AddPass(
		RDG_EVENT_NAME("Prepare Fluid Advection Back"),
		PrepareParams,
		ERDGPassFlags::AsyncCompute,
		[&PrepareParams, PrepareFluidDataAdvectCS, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, PrepareFluidDataAdvectCS, *PrepareParams, GroupCount);
		});

	GraphBuilder.AddPassDependency(PassPrepareForward, PassPrepareBack);


	FFireShaderAdvectFluidDataCS::FParameters* AdvectParams = GraphBuilder.AllocParameters<FFireShaderAdvectFluidDataCS::FParameters>();
	AdvectParams->TScale = TScale;
	AdvectParams->Forward = TimeStep;
	AdvectParams->FluidDissipation = Config.FluidDissipation;
	AdvectParams->FluidDecay = Config.FluidDecay * TimeStep;
	AdvectParams->WorldToGrid = WorldToGrid;
	AdvectParams->RcpVelocitySize = Velocity[READ].RcpSize;
	AdvectParams->RcpFluidSize = FluidData[READ].RcpSize;
	AdvectParams->FluidBounds = FluidData[READ].Bounds;
	AdvectParams->_LinearClamp = TStaticSamplerState<>::GetRHI();
	AdvectParams->obstaclesIn = PrepareParams->obstaclesIn;
	AdvectParams->velocityIn =  PrepareParams->velocityIn;
	AdvectParams->fluidDataIn = CreateTexture(GraphBuilder, FluidData[READ]);
	AdvectParams->phi0 = CreateTexture(GraphBuilder, Phi[0]);
	AdvectParams->phi1 = CreateTexture(GraphBuilder, Phi[1]);

	AdvectParams->outputFloat4 = CreateUAV(GraphBuilder, FluidData[WRITE]);

	TShaderMapRef<FFireShaderAdvectFluidDataCS> FluidDataAdvectCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRDGPassRef PassAdvect = GraphBuilder.AddPass(
		RDG_EVENT_NAME("Fluid Advection"),
		PrepareParams,
		ERDGPassFlags::AsyncCompute,
		[&AdvectParams, FluidDataAdvectCS, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, FluidDataAdvectCS, *AdvectParams, GroupCount);
		});

	GraphBuilder.AddPassDependency(PassPrepareBack, PassAdvect);

	SwapBuffer(FluidData);
}

void FFireSimulationModule::AdvectVelocity(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config)
{
	FFireShaderAdvectVelocityCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderAdvectVelocityCS::FParameters>();
	Params->Forward = TimeStep;
	Params->Dissipation = Config.Dissipation;
	Params->WorldToGrid = WorldToGrid;
	Params->RcpVelocitySize = Velocity[READ].RcpSize;
	Params->_LinearClamp = TStaticSamplerState<>::GetRHI();
	Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
	Params->obstaclesIn = CreateTexture( GraphBuilder, Obstacles);
	Params->outputFloat4 = CreateUAV( GraphBuilder, Velocity[WRITE]);

	TShaderMapRef<FFireShaderAdvectVelocityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = DefaultThreadCount;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Velocity Advection"),
		Params,
		ERDGPassFlags::AsyncCompute,
		[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
		});

	SwapBuffer(Velocity);
}

void FFireSimulationModule::ApplyBuoyancy(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config)
{
	FFireShaderBuoyancyCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderBuoyancyCS::FParameters>();
	Params->Buoyancy = Config.Buoyancy * TimeStep;
	Params->Weight = Config.DensityWeight;
	Params->AmbientTemperature = Config.AmbientTemperature;
	Params->Up = FVector3f(FVector::UpVector);
	Params->fluidDataIn = CreateTexture(GraphBuilder, FluidData[READ]);
	Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
	Params->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
	Params->outputFloat4 = CreateUAV(GraphBuilder, Velocity[WRITE]);
	
	TShaderMapRef<FFireShaderBuoyancyCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = DefaultThreadCount;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Buoyancy Calculation"),
		Params,
		ERDGPassFlags::AsyncCompute,
		[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
		});

	SwapBuffer(Velocity);
}

void FFireSimulationModule::HandleExtinguish(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config)
{
	FFireShaderExtinguishCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderExtinguishCS::FParameters>();
	Params->Amount = Config.ReactionAmount;
	Params->Extinguishment = FVector3f(Config.VaporCooling, Config.VaporExtinguish, Config.ReactionExtinguish);
	Params->TempDistribution = Config.TemperatureDistribution * TimeStep;
	Params->fluidDataIn = CreateTexture(GraphBuilder, FluidData[READ]);
	Params->outputFloat4 = CreateUAV(GraphBuilder, FluidData[WRITE]);

	TShaderMapRef<FFireShaderExtinguishCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = TransportThreadCount;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Extinguishment"),
		Params,
		ERDGPassFlags::AsyncCompute,
		[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
		});

	SwapBuffer(FluidData);
}

void FFireSimulationModule::CalculateVorticity(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config)
{
	FFireShaderVorticityCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderVorticityCS::FParameters>();
	Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
	Params->outputFloat4 = CreateUAV( GraphBuilder, Vorticity);

	TShaderMapRef<FFireShaderVorticityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = DefaultThreadCount;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Vorticity"),
		Params,
		ERDGPassFlags::AsyncCompute,
		[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
		});
}

void FFireSimulationModule::UpdateConfinement(FRDGBuilder& GraphBuilder, float TimeStep, const FFireSimulationConfig& Config)
{
	FFireShaderConfinementCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderConfinementCS::FParameters>();
	Params->Strength = Config.VorticityStrength * TimeStep;
	Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
	Params->vorticityIn = CreateTexture( GraphBuilder, Vorticity);
	Params->outputFloat4 = CreateUAV( GraphBuilder, Velocity[WRITE]);

	TShaderMapRef<FFireShaderConfinementCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = DefaultThreadCount;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Vorticity"),
		Params,
		ERDGPassFlags::AsyncCompute,
		[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
		});

	SwapBuffer(Velocity);
}

void FFireSimulationModule::CalculateDivergence(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config)
{
	FFireShaderDivergenceCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderDivergenceCS::FParameters>();
	Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
	Params->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
	Params->outputFloat = CreateUAV( GraphBuilder, Divergence);

	TShaderMapRef<FFireShaderDivergenceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = DefaultThreadCount;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Divergence"),
		Params,
		ERDGPassFlags::AsyncCompute,
		[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
		});
}

void FFireSimulationModule::SolvePressure(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config)
{
	if (Config.NumPressureIterations == 0)
		return;

	FFireShaderPressureCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderPressureCS::FParameters>();
	Params->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
	Params->divergenceIn = CreateTexture( GraphBuilder,Divergence);

	TShaderMapRef<FFireShaderPressureCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const auto GroupCount = DefaultThreadCount;

	for(int32 I=0; I < Config.NumPressureIterations; ++I)
	{
		Params->pressureIn = CreateTexture(GraphBuilder, Pressure[READ]);
		Params->outputFloat = CreateUAV(GraphBuilder, Pressure[WRITE]);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Pressure"),
			Params,
			ERDGPassFlags::AsyncCompute,
			[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
			});

		SwapBuffer(Pressure);
	}
}

void FFireSimulationModule::DoProjection(FRDGBuilder& GraphBuilder, const FFireSimulationConfig& Config)
{
}

void FFireSimulationModule::ClearAllBuffer(FRDGBuilder& GraphBuilder)
{
	ClearBuffer(GraphBuilder, DefaultThreadCount, Obstacles);
	ClearBuffer(GraphBuilder, DefaultThreadCount, Vorticity);
	ClearBuffer(GraphBuilder, DefaultThreadCount, Divergence);
	for(int32 I=0; I < 2; ++I)
	{
		ClearBuffer(GraphBuilder, DefaultThreadCount, Velocity[I]);
		ClearBuffer(GraphBuilder, DefaultThreadCount, Pressure[I]);

		ClearBuffer(GraphBuilder, TransportThreadCount, FluidData[I]);
		ClearBuffer(GraphBuilder, TransportThreadCount, Phi[I]);
	}
}

void FFireSimulationModule::ClearBuffer(FRDGBuilder& GraphBuilder, FIntVector GroupCount, FShaderBuffer& Buffer)
{
	if (Buffer.Format == PF_R16F)
	{
		FFireShaderClearFloatCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloatCS::FParameters>();
		Params->outputFloat = CreateUAV(GraphBuilder, Buffer);

		TShaderMapRef<FFireShaderClearFloatCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ClearFloatBuffer"),
			Params,
			ERDGPassFlags::AsyncCompute,
			[&Params, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Params, GroupCount);
			});
	}
	else
	{
		FFireShaderClearFloat4CS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloat4CS::FParameters>();
		Params->outputFloat4 = CreateUAV(GraphBuilder, Buffer);

		TShaderMapRef<FFireShaderClearFloat4CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ClearFloat4Buffer"),
			Params,
			ERDGPassFlags::AsyncCompute,
			[&Params, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
						{
							FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Params, GroupCount);
						});
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFireSimulationModule, FireSimulation)