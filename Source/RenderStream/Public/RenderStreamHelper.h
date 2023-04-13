#pragma once
#include "Core.h"
#include <cmath>

namespace d3ToUEHelpers
{
    FTransform Convertd3TransformToUE(FMatrix& d3Mat, const FMatrix& YUpMatrix)
    {
        CONTEXT();

        //Change of basis vectors
        const FMatrix YUpMatrixInv(YUpMatrix.Inverse());

        FTransform v(YUpMatrix * d3Mat * YUpMatrixInv);
        
        //Scale - with updated axes
        FVector scale = d3Mat.GetScaleVector();

        const FVector inverseScale = FVector(1.0f, 1.0f, 1.0f) / scale;
        v.SetScale3D(inverseScale);

        scale = FVector(scale.Y, scale.X, scale.Z);
        v.SetScale3D(scale);

        v.ScaleTranslation(FUnitConversion::Convert(1.f, EUnit::Meters, FRenderStreamModule::distanceUnit()));

        return v;
    }
}
