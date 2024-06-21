// Copyright Epic Games, Inc. All Rights Reserved.

#include "FireSimulation.h"

#include "FireShaderKernels.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FFireSimulationModule"

DECLARE_STATS_GROUP(TEXT("FireSimulation"), STATGROUP_FireSimulation, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("FireSimulation Execute"), STAT_FireSimulation_Execute, STATGROUP_FireSimulation);

static const FIntVector3 THREAD_COUNT = { 8, 8, 8 };

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

void FFireSimulationModule::FBufferDesc::Init(const FIntVector Res)
{
	Resolution = Res;
	Bounds = FIntVector(Res.X-1, Res.Y-1, Res.Z-1);
	RcpSize = FVector3f(1.0f/Res.X, 1.0f/Res.Y, 1.0f/Res.Z);
	ThreadCount = FComputeShaderUtils::GetGroupCount(Res, THREAD_COUNT);
}

void FFireSimulationModule::Initialize(const FVector& Size, const FFireSimulationConfig& Config)
{
	FIntVector Resolution;
	GetResolution(Size, Config.CellSize, Config.MaxResolution, Resolution);
	Velocity.Init(Resolution);

	const FIntVector FluidResolution = Resolution * Config.FluidResolutionScale;
	Fluid.Init(FluidResolution);
	
	LocalSize = FVector3f(Size.X, Size.Y, Size.Z);
	TScale.X = Config.FluidResolutionScale;
	TScale.Y = 1.0f / Config.FluidResolutionScale;

	WorldToGrid = { Resolution.X / LocalSize.X, Resolution.Y / LocalSize.Y, Resolution.Z / LocalSize.Z };
}

void FFireSimulationModule::Dispatch(float TimeStep, const FFireSimulationConfig& Config)
{
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

FRDGTextureDesc CreateTexture(FIntVector Res, bool bIsFloat4)
{
	return FRDGTextureDesc::Create3D(Res, bIsFloat4 ? PF_FloatRGBA : PF_R16F , EClearBinding::ENoneBound, ETextureCreateFlags::RenderTargetable|ETextureCreateFlags::ShaderResource|ETextureCreateFlags::UAV);
}

void FFireSimulationModule::DispatchRenderThread(float TimeStep, const FFireSimulationConfig& Config, FRHICommandListImmediate& CommandList)
{
	FRDGBuilder GraphBuilder(CommandList);
	{
		SCOPE_CYCLE_COUNTER(STAT_FireSimulation_Execute);
		DECLARE_GPU_STAT(FireSimulation)
		RDG_EVENT_SCOPE(GraphBuilder, "FireSimulation");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FireSimulation);

		if (!Velocity.RenderTarget.IsValid())
		{
			FRDGTextureDesc VelocityDesc(CreateTexture(Velocity.Resolution, true));
			FRDGTextureRef VelocityTexture = GraphBuilder.CreateTexture(VelocityDesc, TEXT("Velocity"));
		
			FFireShaderClearFloat4CS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloat4CS::FParameters>();
			Params->outputFloat4 = GraphBuilder.CreateUAV(VelocityTexture);

			const auto GroupCount = Velocity.ThreadCount;
			TShaderMapRef<FFireShaderClearFloat4CS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRDGPassRef PassPrepareForward = GraphBuilder.AddPass(
				RDG_EVENT_NAME("Clear Velocity"),
				Params,
				ERDGPassFlags::AsyncCompute,
				[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
				});

			GraphBuilder.QueueTextureExtraction(VelocityTexture, &Velocity.RenderTarget);
		}

		if (!Fluid.RenderTarget.IsValid())
		{
			FRDGTextureDesc FluidDesc(CreateTexture(Fluid.Resolution, true));
			FRDGTextureRef FluidTexture = GraphBuilder.CreateTexture(FluidDesc, TEXT("FluidData"));
		
			FFireShaderClearFloat4CS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloat4CS::FParameters>();
			Params->outputFloat4 = GraphBuilder.CreateUAV(FluidTexture);

			const auto GroupCount = Fluid.ThreadCount;
			TShaderMapRef<FFireShaderClearFloat4CS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRDGPassRef PassPrepareForward = GraphBuilder.AddPass(
				RDG_EVENT_NAME("Clear FluidData"),
				Params,
				ERDGPassFlags::AsyncCompute,
				[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
				});

			GraphBuilder.QueueTextureExtraction(FluidTexture, &Fluid.RenderTarget);
		}

		if (Velocity.RenderTarget.IsValid() && Fluid.RenderTarget.IsValid())
		{
			FRDGTextureRef VelocityIn = GraphBuilder.RegisterExternalTexture(Velocity.RenderTarget);
			FRDGTextureRef FluidDataIn = GraphBuilder.RegisterExternalTexture(Fluid.RenderTarget);

			/*
			// Advect Fluid
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
	
				const auto GroupCount = FluidThreadCount;
				TShaderMapRef<FFireShaderPrepareFluidDataAdvectionCS> PrepareFluidDataAdvectCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FRDGPassRef PassPrepareForward = GraphBuilder.AddPass(
					RDG_EVENT_NAME("Prepare Fluid Advection Fwd"),
					PrepareParams,
					ERDGPassFlags::AsyncCompute,
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
			}
			
			// Advect Velocity
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
				const auto GroupCount = ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Velocity Advection"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
			
			// ApplyBuoyancy
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
				const auto GroupCount = ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Buoyancy Calculation"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
	
			// HandleExtinguish
			{
				FFireShaderExtinguishCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderExtinguishCS::FParameters>();
				Params->Amount = Config.ReactionAmount;
				Params->Extinguishment = FVector3f(Config.VaporCooling, Config.VaporExtinguish, Config.ReactionExtinguish);
				Params->TempDistribution = Config.TemperatureDistribution * TimeStep;
				Params->fluidDataIn = CreateTexture(GraphBuilder, FluidData[READ]);
				Params->outputFloat4 = CreateUAV(GraphBuilder, FluidData[WRITE]);
	
				TShaderMapRef<FFireShaderExtinguishCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = FluidThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Extinguishment"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
	
			//CalculateVorticity
			{
				FFireShaderVorticityCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderVorticityCS::FParameters>();
				Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
				Params->outputFloat4 = CreateUAV( GraphBuilder, Vorticity);
	
				TShaderMapRef<FFireShaderVorticityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Vorticity"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
	
			// Update Confinement
			{
				FFireShaderConfinementCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderConfinementCS::FParameters>();
				Params->Strength = Config.VorticityStrength * TimeStep;
				Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
				Params->vorticityIn = CreateTexture( GraphBuilder, Vorticity);
				Params->outputFloat4 = CreateUAV( GraphBuilder, Velocity[WRITE]);
	
				TShaderMapRef<FFireShaderConfinementCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Vorticity"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
	
			// Calculate Divergence
			{
				FFireShaderDivergenceCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderDivergenceCS::FParameters>();
				Params->velocityIn = CreateTexture(GraphBuilder, Velocity[READ]);
				Params->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
				Params->outputFloat = CreateUAV( GraphBuilder, Divergence);
	
				TShaderMapRef<FFireShaderDivergenceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Divergence"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
	
			// Solve Pressure
			{
				if (Config.NumPressureIterations > 0)
				{
					FFireShaderPressureCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderPressureCS::FParameters>();
					Params->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
					Params->divergenceIn = CreateTexture( GraphBuilder,Divergence);
	
					TShaderMapRef<FFireShaderPressureCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					const auto GroupCount = ThreadCount;
	
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
					}
				}
			}
	
			// DoProjection
			{
				FFireShaderProjectionCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderProjectionCS::FParameters>();
				Params->obstaclesIn = CreateTexture(GraphBuilder, Obstacles);
				Params->pressureIn = CreateTexture( GraphBuilder, Pressure[READ]);
				Params->velocityIn = CreateTexture( GraphBuilder, Velocity[READ]);
				Params->outputFloat4 = CreateUAV( GraphBuilder, Velocity[WRITE]);
				
				TShaderMapRef<FFireShaderPressureCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Projection"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});
			}
			*/
		}
	}
	GraphBuilder.Execute();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFireSimulationModule, FireSimulation)