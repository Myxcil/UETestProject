// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "IntVectorTypes.h"
#include "ShaderParameterStruct.h"

class FFireShaderBaseCS : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FFireShaderClearFloatCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderClearFloatCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderClearFloatCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, outputFloat)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderClearFloat4CS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderClearFloat4CS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderClearFloat4CS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderPrepareFluidDataAdvectionCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderPrepareFluidDataAdvectionCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderPrepareFluidDataAdvectionCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, TScale)
		SHADER_PARAMETER(float, Forward)
		SHADER_PARAMETER(FVector3f, WorldToGrid)
		SHADER_PARAMETER(FVector3f, RcpVelocitySize)
		SHADER_PARAMETER(FVector3f, RcpFluidSize)
		SHADER_PARAMETER_SAMPLER(SamplerState, _LinearClamp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, phiIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderAdvectFluidDataCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderAdvectFluidDataCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderAdvectFluidDataCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, TScale)
		SHADER_PARAMETER(float, Forward)
		SHADER_PARAMETER(FVector4f, FluidDissipation)
		SHADER_PARAMETER(FVector4f, FluidDecay)
		SHADER_PARAMETER(FVector3f, WorldToGrid)
		SHADER_PARAMETER(FVector3f, RcpVelocitySize)
		SHADER_PARAMETER(FVector3f, RcpFluidSize)
		SHADER_PARAMETER(FIntVector3, FluidBounds)
		SHADER_PARAMETER_SAMPLER(SamplerState, _LinearClamp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, fluidDataIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, phi0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, phi1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderAdvectVelocityCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderAdvectVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderAdvectVelocityCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Forward)
		SHADER_PARAMETER(FVector3f, Dissipation)
		SHADER_PARAMETER(FVector3f, WorldToGrid)
		SHADER_PARAMETER(FVector3f, RcpVelocitySize)
		SHADER_PARAMETER_SAMPLER(SamplerState, _LinearClamp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderBuoyancyCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderBuoyancyCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderBuoyancyCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Buoyancy)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, AmbientTemperature)
		SHADER_PARAMETER(FVector3f, Up)
		SHADER_PARAMETER(FVector3f, RcpVelocitySize)
		SHADER_PARAMETER_SAMPLER(SamplerState, _LinearClamp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, fluidDataIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderExtinguishCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderExtinguishCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderExtinguishCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, TScale)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(FVector3f, Extinguishment)
		SHADER_PARAMETER(FVector3f, TempDistribution)
		SHADER_PARAMETER(FIntVector3, FluidBounds)
		SHADER_PARAMETER(FVector3f, RcpVelocitySize)
		SHADER_PARAMETER_SAMPLER(SamplerState, _LinearClamp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, fluidDataIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderVorticityCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderVorticityCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderVorticityCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector3, VelocityBounds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderConfinementCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderConfinementCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderConfinementCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(FIntVector3, VelocityBounds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, vorticityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderDivergenceCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderDivergenceCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector3, VelocityBounds)
		SHADER_PARAMETER(FVector3f, RcpVelocitySize)
		SHADER_PARAMETER_SAMPLER(SamplerState, _LinearClamp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float>, outputFloat)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderPressureCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderPressureCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderPressureCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector3, VelocityBounds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, pressureIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, divergenceIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float>, outputFloat)
	END_SHADER_PARAMETER_STRUCT()
};

class FFireShaderProjectionCS : public FFireShaderBaseCS
{
public:
	DECLARE_GLOBAL_SHADER(FFireShaderProjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FFireShaderProjectionCS, FFireShaderBaseCS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector3, VelocityBounds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, obstaclesIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, pressureIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, velocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float4>, outputFloat4)
	END_SHADER_PARAMETER_STRUCT()
};
