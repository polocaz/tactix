// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "TactixPlayerController.generated.h"

class UInputMappingContext;

/**
 * 
 */
UCLASS()
class TACTIX_API ATactixPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

	ATactixPlayerController(const FObjectInitializer& ObjectInitializer);
	
	//----------------------------------------------------------------------//
	// IGenericTeamAgentInterface
	//----------------------------------------------------------------------//
public:
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;

	virtual FGenericTeamId GetGenericTeamId() const override { return TeamId; }

private:
	FGenericTeamId TeamId;

protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Input", meta = (AllowPrivateAccess = "true"))
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;
};
