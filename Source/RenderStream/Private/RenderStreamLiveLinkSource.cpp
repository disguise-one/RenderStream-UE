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

    FLiveLinkSubjectKey SubjectKey{ SourceGuid, SubjectName };
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
        const double ScaleFactor = FUnitConversion::Convert(1.f, EUnit::Meters, FRenderStreamModule::distanceUnit());

        FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkAnimationFrameData::StaticStruct());
        FLiveLinkAnimationFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkAnimationFrameData>();

        const FQuat RootRotation = d3ToUEHelpers::Convertd3QuaternionToUE(Pose.rootOrientation);
        const FVector RootTranslation = d3ToUEHelpers::Convertd3VectorToUE(Pose.rootPosition);
        const FTransform RootTransform(RootRotation, RootTranslation, FVector(ScaleFactor));

        FrameData.Transforms.SetNum(Pose.joints.Num());
        for (int32 i = 0; i < Pose.joints.Num(); ++i)
        {
            const RenderStreamLink::SkeletonJointPose& Joint = Pose.joints[i];

            int32 idx = Layout.joints.IndexOfByPredicate([&Joint](const auto& LayoutJoint) { return LayoutJoint.id == Joint.id; });
            bool IsRoot = idx == RootIdx;
            
            FQuat Rot = d3ToUEHelpers::Convertd3QuaternionToUE(Joint.transform.rx, Joint.transform.ry, Joint.transform.rz, Joint.transform.rw);
            FVector Trans = d3ToUEHelpers::Convertd3VectorToUE(Joint.transform.x, Joint.transform.y, Joint.transform.z);

            FTransform Transform(Rot, Trans);
            if (IsRoot)
                Transform = RootTransform * Transform;
            FrameData.Transforms[idx] = Transform;
        }

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
