// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FireSimulationConfig.h"
#include "Components/SceneComponent.h"
#include "FireSimulatorVolume.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FIRESIMULATION_API UFireSimulatorVolume : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UFireSimulatorVolume();

protected:
	UPROPERTY(EditAnywhere)
	FFireSimulationConfig Config;
	UPROPERTY(EditAnywhere)
	FVector VolumeSize = { 1000.0f, 1000.0, 1000.0 };
	
	// Called when the game starts
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
