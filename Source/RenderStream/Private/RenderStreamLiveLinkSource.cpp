// Fill out your copyright notice in the Description page of Project Settings.


#include "RenderStreamLiveLinkSource.h"

#include "RenderStream.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "RenderStreamHelper.h"

void FRenderStreamLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
    Client = InClient;
    SourceGuid = InSourceGuid;
}

bool FRenderStreamLiveLinkSource::IsSourceStillValid() const
{
    return Client != NULL;
}

bool FRenderStreamLiveLinkSource::RequestSourceShutdown()
{
    return true;
}

void FRenderStreamLiveLinkSource::Update()
{
    
}

void FRenderStreamLiveLinkSource::PushFrameAnimData(const FName& SubjectName, const RenderStreamLink::FSkeletalLayout& Layout, const RenderStreamLink::FSkeletalPose& Pose)
{
    check(Client);

    int32 RootIdx = INDEX_NONE;

    const FLiveLinkSubjectKey SubjectKey{ SourceGuid, SubjectName };
    { // Static data
        FLiveLinkStaticDataStruct StaticDataStruct = FLiveLinkStaticDataStruct(FLiveLinkSkeletonStaticData::StaticStruct());
        FLiveLinkSkeletonStaticData& StaticSkeleton = *StaticDataStruct.Cast<FLiveLinkSkeletonStaticData>();

        StaticSkeleton.BoneNames.SetNum(Layout.joints.Num());
        StaticSkeleton.BoneParents.SetNum(Layout.joints.Num());
        for (int32 i = 0; i < Layout.joints.Num(); ++i)
        {
            const RenderStreamLink::SkeletonJointDesc& Joint = Layout.joints[i];
            const FString& Name = Layout.jointNames[i];
            
            StaticSkeleton.BoneNames[i] = FName(Name);
            const int32 idx = Layout.joints.IndexOfByPredicate([&Joint](const auto& OtherJoint) { return OtherJoint.id == Joint.parentId; });
            StaticSkeleton.BoneParents[i] = idx; // Root bone is indicated by negative index, which IndexOfByPredicate will return if not found

            if (idx == INDEX_NONE)
                RootIdx = i;
        }

        Client->PushSubjectStaticData_AnyThread(SubjectKey, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticDataStruct));
    }

    { // Frame Data
        static const FMatrix YUpMats[] = {
            // Y up matrix for root pose transform and root bone transform
            FMatrix(FVector(0.0f, 0.0f, 1.0f)
                , FVector(1.0f, 0.0f, 0.0f)
                , FVector(0.0f, 1.0f, 0.0f)
                , FVector(0.0f, 0.0f, 0.0f)
            ),
            // Y up matrix for the relative transforms of the rest of the bones 
            FMatrix(FVector(-1.0f, 0.0f, 0.0f)
                , FVector(0.0f, 0.0f, 1.0f)
                , FVector(0.0f, -1.0f, 0.0f)
                , FVector(0.0f, 0.0f, 0.0f)
            )
        };

        auto ToUnrealTransform = [](const FVector& S, const FQuat& R, const FVector& T, const FMatrix& YUpMatrix){
            const FMatrix YUpMatrixInv(YUpMatrix.Inverse());

            FMatrix D3Mat = R.ToMatrix();
            D3Mat.SetOrigin(T);
            D3Mat.ScaleTranslation(S);

            return FTransform(YUpMatrix * D3Mat * YUpMatrixInv);
        };

        FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkAnimationFrameData::StaticStruct());
        FLiveLinkAnimationFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkAnimationFrameData>();
        FrameData.Transforms.SetNum(Pose.joints.Num());
        // calculate all transforms, for root joint and all others
        for (int32 i = 0; i < Pose.joints.Num(); ++i)
        {
            const RenderStreamLink::SkeletonJointPose& Joint = Pose.joints[i];

            const int32 idx = Layout.joints.IndexOfByPredicate([&Joint](const auto& LayoutJoint) { return LayoutJoint.id == Joint.id; });
            const int YUpMatIdx = idx == RootIdx ? 0 : 1;
            FrameData.Transforms[idx] = ToUnrealTransform(FVector(1.)
                , FQuat(Joint.transform.rx, Joint.transform.ry, Joint.transform.rz, Joint.transform.rw)
                , FVector(Joint.transform.x, Joint.transform.y, Joint.transform.z)
                , YUpMats[YUpMatIdx]
            );
        }
        // get correct scale and parent transform for root joint
        const FVector RootScale(FUnitConversion::Convert(1.f, EUnit::Meters, FRenderStreamModule::distanceUnit()));
        const FTransform PoseRootTransform = ToUnrealTransform(RootScale, FQuat(Pose.rootOrientation), FVector(Pose.rootPosition), YUpMats[0]);
        FTransform& RootBoneTransform = FrameData.Transforms[RootIdx];
        // combined rotations, including 90 degree yaw
        FTransform FinalTransform = FTransform(PoseRootTransform.GetRotation() * RootBoneTransform.GetRotation());
        FinalTransform.ConcatenateRotation(FQuat::MakeFromRotator(FRotator(0, 90, 0)));
        // combined translation, unaffected by rotations
        FinalTransform.SetTranslation(PoseRootTransform.GetTranslation() + RootBoneTransform.GetTranslation());
        // update root joint transform
        RootBoneTransform = FinalTransform;

        Client->PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(FrameDataStruct));
    }
}

FText FRenderStreamLiveLinkSource::GetSourceType() const
{
    return FText::FromString("RenderStream LiveLink");
}

FText FRenderStreamLiveLinkSource::GetSourceMachineName() const
{
    return FText();
}

FText FRenderStreamLiveLinkSource::GetSourceStatus() const
{
    return FText();
}

void FRenderStreamLiveLinkSource::InitializeSettings(ULiveLinkSourceSettings* Settings)
{
    
}

void FRenderStreamLiveLinkSource::OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent)
{
    
}
