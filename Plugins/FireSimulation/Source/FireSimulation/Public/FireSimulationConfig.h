#pragma once

#include "FireSimulationConfig.generated.h"

USTRUCT()
struct FIRESIMULATION_API FFireSimulationConfig
{
	GENERATED_BODY()
	
	float CellSize = 10.0f;
	int32 MaxResolution = 128;
	int32 FluidResolutionScale = 2;
	int32 NumPressureIterations = 8;

	// Fluid advection
	FVector4f FluidDissipation = FVector4f(0.001f, 0.0f, 0.03f, 0.03f);
	FVector4f FluidDecay = FVector4f(0.0f, 0.2f, 0.0f, 0.0f);

	// Velocity Advection & Buoyancy
	FVector3f Dissipation = FVector3f( 0.02f, 0.02f, 0.02f);
	float Buoyancy = 1.0f;
	float DensityWeight = 0.1f;
	float AmbientTemperature = 20.0f;

	// Extinguishment
	float ReactionAmount = 0.2f;
	float VaporCooling = 50.0f;
	float VaporExtinguish = 0.1f;
	float ReactionExtinguish = 0.15f;
	FVector3f TemperatureDistribution = FVector3f(0,0,0);

	// Turbulence
	float VorticityStrength = 12.0f;
};
