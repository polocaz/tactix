#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ITraceProvider.generated.h"

UINTERFACE(Blueprintable)
class TACTIX_API UTraceProvider : public UInterface
{
    GENERATED_BODY()
};

class TACTIX_API ITraceProvider
{
    GENERATED_BODY()

public:
    virtual FVector GetTraceOrigin() const = 0;
    virtual FVector GetTraceDirection() const = 0;
};