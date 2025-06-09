#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IWeapon.generated.h"

class UWeaponData;
class UHitTraceComponent;

UINTERFACE(Blueprintable)
class TACTIX_API UWeapon : public UInterface
{
    GENERATED_BODY()
};

class TACTIX_API IWeapon
{
    GENERATED_BODY()

public:
    virtual void StartAttack() = 0;
    virtual void StopAttack() = 0;
    virtual void Reload() = 0;
    virtual bool CanAttack() const = 0;
    virtual bool CanReload() const = 0;

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnAmmoChanged, int32);
    virtual FOnAmmoChanged& OnAmmoChanged() = 0;

};