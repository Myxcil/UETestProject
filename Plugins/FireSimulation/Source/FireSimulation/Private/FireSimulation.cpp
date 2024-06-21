// Copyright Epic Games, Inc. All Rights Reserved.

#include "FireSimulation.h"

#include "FireShaderKernels.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
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

FRDGTextureDesc CreateTextureDesc(FIntVector Res, bool bIsFloat4)
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

		if (!PrevVelocity.IsValid())
		{
			FRDGTextureDesc VelocityDesc(CreateTextureDesc(Velocity.Resolution, true));
			FRDGTextureRef VelocityTexture = GraphBuilder.CreateTexture(VelocityDesc, TEXT("PrevVelocity"));
		
			FFireShaderClearFloat4CS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloat4CS::FParameters>();
			Params->outputFloat4 = GraphBuilder.CreateUAV(VelocityTexture);

			const auto GroupCount = Velocity.ThreadCount;
			TShaderMapRef<FFireShaderClearFloat4CS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Clear Velocity"),
				Params,
				ERDGPassFlags::AsyncCompute,
				[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
				});

			GraphBuilder.QueueTextureExtraction(VelocityTexture, &PrevVelocity);
		}

		if (!PrevFluid.IsValid())
		{
			FRDGTextureDesc FluidDesc(CreateTextureDesc(Fluid.Resolution, true));
			FRDGTextureRef FluidTexture = GraphBuilder.CreateTexture(FluidDesc, TEXT("PrevFluid"));
		
			FFireShaderClearFloat4CS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloat4CS::FParameters>();
			Params->outputFloat4 = GraphBuilder.CreateUAV(FluidTexture);

			const auto GroupCount = Fluid.ThreadCount;
			TShaderMapRef<FFireShaderClearFloat4CS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Clear FluidData"),
				Params,
				ERDGPassFlags::AsyncCompute,
				[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
				});

			GraphBuilder.QueueTextureExtraction(FluidTexture, &PrevFluid);
		}

		if (!PrevPressure.IsValid())
		{
			FRDGTextureDesc PressureDesc(CreateTextureDesc(Velocity.Resolution, false));
			FRDGTextureRef PressureTexture = GraphBuilder.CreateTexture(PressureDesc, TEXT("PrevPressure"));
        		
			FFireShaderClearFloatCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloatCS::FParameters>();
			Params->outputFloat = GraphBuilder.CreateUAV(PressureTexture);
        
			const auto GroupCount = Velocity.ThreadCount;
			TShaderMapRef<FFireShaderClearFloatCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Clear Pressure"),
				Params,
				ERDGPassFlags::AsyncCompute,
				[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
				});
        
			GraphBuilder.QueueTextureExtraction(PressureTexture, &PrevPressure);
		}

		if (!Obstacles.IsValid())
		{
			FRDGTextureDesc ObstaclesDesc(CreateTextureDesc(Velocity.Resolution, false));
			FRDGTextureRef ObstaclesTexture = GraphBuilder.CreateTexture(ObstaclesDesc, TEXT("Obstacles"));
		
			FFireShaderClearFloatCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderClearFloatCS::FParameters>();
			Params->outputFloat = GraphBuilder.CreateUAV(ObstaclesTexture);

			const auto GroupCount = Velocity.ThreadCount;
			TShaderMapRef<FFireShaderClearFloatCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Clear Obstacles"),
				Params,
				ERDGPassFlags::AsyncCompute,
				[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
				});

			GraphBuilder.QueueTextureExtraction(ObstaclesTexture, &Obstacles);
		}

		if (PrevVelocity.IsValid() && PrevFluid.IsValid() && PrevPressure.IsValid() && Obstacles.IsValid())
		{
			FRDGTextureRef PrevVelocityTexture = GraphBuilder.RegisterExternalTexture(PrevVelocity);
			FRDGTextureRef PrevFluidDataTexture = GraphBuilder.RegisterExternalTexture(PrevFluid);
			FRDGTextureRef ObstaclesTexture = GraphBuilder.RegisterExternalTexture(Obstacles);

			FRDGTextureRef Phi0 = GraphBuilder.CreateTexture(CreateTextureDesc(Fluid.Resolution, true), TEXT("Phi0"));
			FRDGTextureRef Phi1 = GraphBuilder.CreateTexture(CreateTextureDesc(Fluid.Resolution, true), TEXT("Phi1"));

			FRDGTextureRef TmpFluid4[2] = { nullptr, nullptr };
			FRDGTextureRef TmpVelocity4[3] = { nullptr, nullptr, nullptr };
			
			// Advect Fluid
			{
				// Prepare Advection
				FFireShaderPrepareFluidDataAdvectionCS::FParameters* PrepareParams = GraphBuilder.AllocParameters<FFireShaderPrepareFluidDataAdvectionCS::FParameters>();
				PrepareParams->TScale = TScale;
				PrepareParams->Forward = TimeStep;
				PrepareParams->WorldToGrid = WorldToGrid;
				PrepareParams->RcpVelocitySize = Velocity.RcpSize;
				PrepareParams->RcpFluidSize = Fluid.RcpSize;
				PrepareParams->_LinearClamp = TStaticSamplerState<>::GetRHI();
			
				PrepareParams->obstaclesIn = ObstaclesTexture;
				PrepareParams->velocityIn = PrevVelocityTexture;
				PrepareParams->phiIn = PrevFluidDataTexture;
				PrepareParams->outputFloat4 = GraphBuilder.CreateUAV(Phi1);

				const auto GroupCount = Fluid.ThreadCount;
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
				PrepareParams->phiIn =  Phi1;
				PrepareParams->outputFloat4 = GraphBuilder.CreateUAV(Phi0);

				FRDGPassRef PassPrepareBack = GraphBuilder.AddPass(
					RDG_EVENT_NAME("Prepare Fluid Advection Back"),
					PrepareParams,
					ERDGPassFlags::AsyncCompute,
					[&PrepareParams, PrepareFluidDataAdvectCS, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, PrepareFluidDataAdvectCS, *PrepareParams, GroupCount);
					});

				GraphBuilder.AddPassDependency(PassPrepareForward, PassPrepareBack);

				// Advect fluid now
				TmpFluid4[0] = GraphBuilder.CreateTexture(CreateTextureDesc(Fluid.Resolution, true), TEXT("TmpFluid4_0"));
			
				FFireShaderAdvectFluidDataCS::FParameters* AdvectParams = GraphBuilder.AllocParameters<FFireShaderAdvectFluidDataCS::FParameters>();
				AdvectParams->TScale = TScale;
				AdvectParams->Forward = TimeStep;
				AdvectParams->FluidDissipation = Config.FluidDissipation;
				AdvectParams->FluidDecay = Config.FluidDecay * TimeStep;
				AdvectParams->WorldToGrid = WorldToGrid;
				AdvectParams->RcpVelocitySize = Velocity.RcpSize;
				AdvectParams->RcpFluidSize = Fluid.RcpSize;
				AdvectParams->FluidBounds = Fluid.Bounds;
				AdvectParams->_LinearClamp = TStaticSamplerState<>::GetRHI();
				AdvectParams->obstaclesIn = ObstaclesTexture;
				AdvectParams->velocityIn =  PrevVelocityTexture;
				AdvectParams->fluidDataIn = PrevFluidDataTexture;
				AdvectParams->phi0 = Phi0;
				AdvectParams->phi1 = Phi1;

				AdvectParams->outputFloat4 = GraphBuilder.CreateUAV(TmpFluid4[0]);

				TShaderMapRef<FFireShaderAdvectFluidDataCS> FluidDataAdvectCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FRDGPassRef PassAdvect = GraphBuilder.AddPass(
					RDG_EVENT_NAME("Fluid Advection"),
					AdvectParams,
					ERDGPassFlags::AsyncCompute,
					[&AdvectParams, FluidDataAdvectCS, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, FluidDataAdvectCS, *AdvectParams, GroupCount);
					});

				GraphBuilder.AddPassDependency(PassPrepareBack, PassAdvect);
			}

			// Advect Velocity
			// TmpFluid4[0] = current fluid state  
			{
				TmpVelocity4[0] =  GraphBuilder.CreateTexture(CreateTextureDesc(Velocity.Resolution, true), TEXT("TmpVelocity4_0"));

				FFireShaderAdvectVelocityCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderAdvectVelocityCS::FParameters>();
				Params->Forward = TimeStep;
				Params->Dissipation = Config.Dissipation;
				Params->WorldToGrid = WorldToGrid;
				Params->RcpVelocitySize = Velocity.RcpSize;
				Params->_LinearClamp = TStaticSamplerState<>::GetRHI();
				Params->velocityIn = PrevVelocityTexture;
				Params->obstaclesIn = ObstaclesTexture;
				Params->outputFloat4 = GraphBuilder.CreateUAV( TmpVelocity4[0]);
	
				TShaderMapRef<FFireShaderAdvectVelocityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Velocity.ThreadCount;
				
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
			// TmpFluid4[0] = current fluid state
			// TmpVelocity4[0] = current velocity state
			{
				TmpVelocity4[1] =  GraphBuilder.CreateTexture(CreateTextureDesc(Velocity.Resolution, true), TEXT("TmpVelocity4_1"));
				
				FFireShaderBuoyancyCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderBuoyancyCS::FParameters>();
				Params->Buoyancy = Config.Buoyancy * TimeStep;
				Params->Weight = Config.DensityWeight;
				Params->AmbientTemperature = Config.AmbientTemperature;
				Params->Up = FVector3f(FVector::UpVector);
				Params->_LinearClamp = Params->_LinearClamp = TStaticSamplerState<>::GetRHI();
				Params->fluidDataIn = TmpFluid4[0];
				Params->velocityIn = TmpVelocity4[0];
				Params->obstaclesIn = ObstaclesTexture;
				Params->outputFloat4 = GraphBuilder.CreateUAV(TmpVelocity4[1]);
				
				TShaderMapRef<FFireShaderBuoyancyCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Velocity.ThreadCount;
				
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
			// TmpFluid4[0] = current fluid state
			// TmpVelocity4[1] = current velocity state
			{
				TmpFluid4[1] = GraphBuilder.CreateTexture(CreateTextureDesc(Fluid.Resolution, true), TEXT("TmpFluid4_1"));
				
				FFireShaderExtinguishCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderExtinguishCS::FParameters>();
				Params->Amount = Config.ReactionAmount;
				Params->Extinguishment = FVector3f(Config.VaporCooling, Config.VaporExtinguish, Config.ReactionExtinguish);
				Params->TempDistribution = Config.TemperatureDistribution * TimeStep;
				Params->_LinearClamp = Params->_LinearClamp = TStaticSamplerState<>::GetRHI();
				Params->fluidDataIn = TmpFluid4[0];
				Params->obstaclesIn = ObstaclesTexture;
				Params->outputFloat4 = GraphBuilder.CreateUAV(TmpFluid4[1]);
	
				TShaderMapRef<FFireShaderExtinguishCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Fluid.ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Extinguishment"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});

				GraphBuilder.QueueTextureExtraction(TmpFluid4[1], &PrevFluid);
			}
	
			// CalculateVorticity
			// TmpFluid4[1] = current fluid state
			// TmpVelocity4[1] = current velocity state
			{
				FFireShaderVorticityCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderVorticityCS::FParameters>();
				Params->velocityIn = TmpVelocity4[1];
				Params->outputFloat4 = GraphBuilder.CreateUAV(TmpVelocity4[0]);
	
				TShaderMapRef<FFireShaderVorticityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Velocity.ThreadCount;
				
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
			// TmpVelocity4[1] = current velocity state
			// TmpVelocity4[0] = vorticity result
			{
				TmpVelocity4[2] = GraphBuilder.CreateTexture(CreateTextureDesc(Velocity.Resolution, true), TEXT("TmpVelocity4_2"));
				
				FFireShaderConfinementCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderConfinementCS::FParameters>();
				Params->Strength = Config.VorticityStrength * TimeStep;
				Params->velocityIn = TmpVelocity4[1];
				Params->vorticityIn = TmpVelocity4[0];
				Params->outputFloat4 = GraphBuilder.CreateUAV(TmpVelocity4[2]);
	
				TShaderMapRef<FFireShaderConfinementCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Velocity.ThreadCount;
				
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
			// TmpVelocity4[0] = vorticity result
			// TmpVelocity4[2] = current velocity state
			FRDGTextureRef Divergence = GraphBuilder.CreateTexture(CreateTextureDesc(Velocity.Resolution, false), TEXT("TmpVelocity1_0"));; 
			{
				FFireShaderDivergenceCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderDivergenceCS::FParameters>();
				Params->_LinearClamp = Params->_LinearClamp = TStaticSamplerState<>::GetRHI();
				Params->velocityIn = TmpVelocity4[2];
				Params->obstaclesIn = ObstaclesTexture;
				Params->outputFloat = GraphBuilder.CreateUAV(Divergence);
	
				TShaderMapRef<FFireShaderDivergenceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Velocity.ThreadCount;
				
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
			// TmpVelocity4[2] = current velocity state
			// TmpVelocity1[0] = divergence result
			FRDGTextureRef PressureResult = nullptr;
			{
				if (Config.NumPressureIterations > 0)
				{
					FRDGTextureRef Pressure[2] =
					{
						GraphBuilder.RegisterExternalTexture(PrevPressure),
						GraphBuilder.CreateTexture(CreateTextureDesc(Velocity.Resolution, false), TEXT("Pressure1"))
					};
					
					TShaderMapRef<FFireShaderPressureCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					const auto GroupCount = Velocity.ThreadCount;
	
					for(int32 I=0; I < Config.NumPressureIterations; ++I)
					{
						FFireShaderPressureCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderPressureCS::FParameters>();
						Params->obstaclesIn = ObstaclesTexture;
						Params->divergenceIn = Divergence;
						Params->pressureIn = Pressure[0];
						Params->outputFloat = GraphBuilder.CreateUAV(Pressure[1]);
					
						GraphBuilder.AddPass(
							RDG_EVENT_NAME("Pressure"),
							Params,
							ERDGPassFlags::AsyncCompute,
							[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
							{
								FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
							});

						Swap(Pressure[0], Pressure[1]);
					}

					PressureResult = Pressure[0];
					GraphBuilder.QueueTextureExtraction(PressureResult, &PrevPressure);
				}
			}
	
			// DoProjection
			// TmpVelocity4[2] = current velocity state
			{
				FFireShaderProjectionCS::FParameters* Params = GraphBuilder.AllocParameters<FFireShaderProjectionCS::FParameters>();
				Params->obstaclesIn = ObstaclesTexture;
				Params->pressureIn = PressureResult;
				Params->velocityIn = TmpVelocity4[2];
				Params->outputFloat4 = GraphBuilder.CreateUAV(TmpVelocity4[0]);
				
				TShaderMapRef<FFireShaderProjectionCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				const auto GroupCount = Velocity.ThreadCount;
				
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Projection"),
					Params,
					ERDGPassFlags::AsyncCompute,
					[&Params, Shader, GroupCount](FRHIComputeCommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, Shader, *Params, GroupCount);
					});

				GraphBuilder.QueueTextureExtraction(TmpVelocity4[0], &PrevVelocity);
			}
		}
	}
	GraphBuilder.Execute();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFireSimulationModule, FireSimulation)