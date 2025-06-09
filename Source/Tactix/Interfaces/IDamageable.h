#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Structures/DamageInfo.h"
#include "IDamageable.generated.h"

UINTERFACE(Blueprintable)
class TACTIX_API UDamageable : public UInterface
{
    GENERATED_BODY()
};

class TACTIX_API IDamageable
{
    GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void GetCurrentHealth(double& CurrentHealth);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void GetMaxHealth(double& MaxHealth);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void PerformHeal(double HealAmount, double& NewHealth);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void ReceiveDamage(FDamageInfo DamageInfo, bool& WasDamaged);

};