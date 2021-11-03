#include "RemoteControlRenderStreamProtocol.h"

const FName FRemoteControlRenderStreamProtocol::ProtocolName = TEXT("RenderStream");

FRemoteControlRenderStreamProtocolEntity::~FRemoteControlRenderStreamProtocolEntity()
{
	
}

FName FRemoteControlRenderStreamProtocolEntity::GetRangePropertyName() const
{
	return NAME_UInt32Property;
}

uint8 FRemoteControlRenderStreamProtocolEntity::GetRangePropertySize() const
{
	return sizeof(uint32);
}

const FString& FRemoteControlRenderStreamProtocolEntity::GetRangePropertyMaxValue() const
{
	static const FString Limit = FString::FromInt(TNumericLimits<uint32>::Max());
	return Limit;
}

FRemoteControlRenderStreamProtocol::FRemoteControlRenderStreamProtocol()
	: FRemoteControlProtocol(ProtocolName)
{}

void FRemoteControlRenderStreamProtocol::Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
		return;

	auto* RSProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlRenderStreamProtocolEntity>();
	auto* ExistingProtocolBindings = ProtocolsBindings.FindByPredicate([RSProtocolEntity](const auto& InProtocolEntity)
	{
		if (const FRemoteControlProtocolEntityPtr& ProtocolEntity = InProtocolEntity.Pin())
		{
			const auto* ComparedRSProtocolEntity = ProtocolEntity->CastChecked<FRemoteControlRenderStreamProtocolEntity>();
			if (ComparedRSProtocolEntity->GetPropertyId() == RSProtocolEntity->GetPropertyId())
				return true;
		}

		return false;
	});

	if (ExistingProtocolBindings == nullptr)
		ProtocolsBindings.Emplace(InRemoteControlProtocolEntityPtr);
}

void FRemoteControlRenderStreamProtocol::Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
		return;

	const auto* RSProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlRenderStreamProtocolEntity>();
	ProtocolsBindings.RemoveAllSwap(CreateProtocolComparator(RSProtocolEntity->GetPropertyId()));
}

void FRemoteControlRenderStreamProtocol::UnbindAll()
{
	ProtocolsBindings.Empty();
}

UScriptStruct* FRemoteControlRenderStreamProtocol::GetProtocolScriptStruct() const 
{
	return FRemoteControlRenderStreamProtocolEntity::StaticStruct();
}

void FRemoteControlRenderStreamProtocol::OnEndFrame()
{
}
