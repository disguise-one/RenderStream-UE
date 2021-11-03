#pragma once

#include "RemoteControlProtocol.h"

#include "RemoteControlProtocolBinding.h"

#include "RemoteControlRenderStreamProtocol.generated.h"

class FRemoteControlRenderStreamProtocol;

/**
 * RenderStream protocol entity for remote control binding
 */
USTRUCT()
struct FRemoteControlRenderStreamProtocolEntity : public FRemoteControlProtocolEntity
{
	GENERATED_BODY()

	friend class FRemoteControlRenderStreamProtocol;

	/** Destructor */
	virtual ~FRemoteControlRenderStreamProtocolEntity();

public:
	//~ Begin FRemoteControlProtocolEntity interface
	virtual FName GetRangePropertyName() const override;
	virtual uint8 GetRangePropertySize() const override;
	virtual const FString& GetRangePropertyMaxValue() const override;
	//~ End FRemoteControlProtocolEntity interface
private:
};

/**
 * RenderStream protocol implementation for Remote Control
 */
class FRemoteControlRenderStreamProtocol : public FRemoteControlProtocol
{
public:
	FRemoteControlRenderStreamProtocol();

	//~ Begin IRemoteControlProtocol interface
	virtual void Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void UnbindAll() override;
	virtual UScriptStruct* GetProtocolScriptStruct() const override;
	virtual void OnEndFrame() override;
	//~ End IRemoteControlProtocol interface

	static const FName ProtocolName;
private:
	TArray<FRemoteControlProtocolEntityWeakPtr> ProtocolsBindings;
};
