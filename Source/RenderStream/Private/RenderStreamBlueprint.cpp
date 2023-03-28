//// Fill out your copyright notice in the Description page of Project Settings.
//
//
//#include "RenderStreamBlueprint.h"
//#include "RenderStream.h"
//#include "Animation/AnimBlueprint.h"
//
//FLiveLinkSubjectName URenderStreamBlueprint::GetRenderStreamLiveLinkSubject(const UAnimBlueprint* AnimBlueprint)
//{
//    if (const FRenderStreamModule* Module = FRenderStreamModule::Get(); Module)
//    {
//        const FName* SubjectName = Module->GetSubjectName(FSoftObjectPath(AnimBlueprint->TargetSkeleton));
//        if (SubjectName)
//            return FLiveLinkSubjectName(*SubjectName);
//    }
//    return FLiveLinkSubjectName();
//}
