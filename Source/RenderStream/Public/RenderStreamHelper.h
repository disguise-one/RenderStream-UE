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

    //                  coord systems translations
    //    d3                                        UE
    // +x = right                |             | +x = forward
    // +y = up                   | Translation | +y = right
    // +z = forward              |             | +z = up
    //
    // +x = anti-clockwise pitch |             | +x = anti-clockwise pitch
    // +y = clockwise yaw        | Rotation    | +y = anti-clockwise roll
    // +z = clockwise roll       |             | +z = clockwise yaw

    FVector Convertd3VectorToUE(float x, float y, float z)
    {
        return FVector(z, x, y);
    }

    FVector Convertd3VectorToUE(const FVector3f& Trans)
    {
        return Convertd3VectorToUE(Trans.X, Trans.Y, Trans.Z);
    }

    FQuat Convertd3QuaternionToUE(float rx, float ry, float rz, float rw)
    {
        return FQuat(rx, -rz, ry, rw);
    }

    FQuat Convertd3QuaternionToUE(const FQuat4f& Rot)
    {
        return Convertd3QuaternionToUE(Rot.X, Rot.Y, Rot.Z, Rot.W);
    }
}
