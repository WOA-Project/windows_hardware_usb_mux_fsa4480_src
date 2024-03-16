#include "fsa4480.h"
#include "Driver.h"
#include <usbctypes.h>
#include "fsa4480.tmh"

NTSTATUS
FSA4480_UpdateSettings(
	WDFDEVICE Device,
	BYTE SwitchControl,
	BYTE SwitchEnable
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;
	LARGE_INTEGER delay = { 0 };

	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	BYTE Data = 0x80;

	if (!deviceContext->InitializedSpbHardware)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;

		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Spb Hardware is not yet initialized, aborting - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SWITCH_SETTINGS,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb initialization - %!STATUS!",
			status);

		goto exit;
	}

	if (!deviceContext->InitializedSpbHardware)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;

		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Spb Hardware is not yet initialized, aborting - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SWITCH_CONTROL,
		&SwitchControl,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb initialization - %!STATUS!",
			status);

		goto exit;
	}

	delay.QuadPart = RELATIVE(MICROSECONDS(55));
	status = KeDelayExecutionThread(KernelMode, TRUE, &delay);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"KeDelayExecutionThread failed with Status = 0x%08lX\n",
			status
		);

		goto exit;
	}

	if (!deviceContext->InitializedSpbHardware)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;

		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Spb Hardware is not yet initialized, aborting - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SWITCH_SETTINGS,
		&SwitchEnable,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb initialization - %!STATUS!",
			status);

		goto exit;
	}

exit:
	return status;
}

NTSTATUS
FSA4480_SetDefaultRegisterSettings(
	WDFDEVICE Device
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;
	UINT32 i = 0;

	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	for (i = 0; i < ARRAYSIZE(gDefaultRegisterSettings); i++)
	{
		if (!deviceContext->InitializedSpbHardware)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;

			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Spb Hardware is not yet initialized, aborting - %!STATUS!",
				status);

			goto exit;
		}

		status = SpbWriteDataSynchronously(
			&deviceContext->I2CContext,
			gDefaultRegisterSettings[i].Address,
			&gDefaultRegisterSettings[i].Value,
			1);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Error writing default register: %d - %!STATUS!",
				gDefaultRegisterSettings[i].Address,
				status);

			goto exit;
		}
	}

exit:
	return status;
}

NTSTATUS
FSA4480_SetupChipGPIOs(
	WDFDEVICE Device,
	USBC_PARTNER USBCPartner
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;

	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	if (USBCPartner == UsbCPartnerAudioAccessory)
	{
		status = FSA4480_UpdateSettings(Device, 0x00, 0x9F);
	}
	else if (USBCPartner == UsbCPartnerInvalid)
	{
		status = FSA4480_UpdateSettings(Device, 0x18, 0x98);
	}

	return status;
}

NTSTATUS
FSA4480_OnUSBCModeChanged(
	WDFDEVICE Device,
	USBC_PARTNER USBCPartner
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;

	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	if ((USBCPartner == UsbCPartnerInvalid ||
		USBCPartner == UsbCPartnerAudioAccessory) &&
		USBCPartner != deviceContext->USBCPartner)
	{
		deviceContext->USBCPartner = USBCPartner;
		status = FSA4480_SetupChipGPIOs(Device, USBCPartner);
	}

	return status;
}

NTSTATUS
FSA4480_ValidateDisplayPortSettings(
	WDFDEVICE Device
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;
	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	UINT32 SwitchStatus = 0;

	if (!deviceContext->InitializedSpbHardware)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;

		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Spb Hardware is not yet initialized, aborting - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbReadDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SWITCH_STATUS1,
		&SwitchStatus,
		1
	);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb initialization - %!STATUS!",
			status);

		goto exit;
	}
	else
	{
		if (SwitchStatus != 0x23 && SwitchStatus != 0x1C)
		{
			status = STATUS_INVALID_CONNECTION;

			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Invalid AUX Switch Configuration for Display Port! SwitchStatus: %d",
				SwitchStatus);

			goto exit;
		}
		else
		{
			status = STATUS_SUCCESS;

			TraceEvents(
				TRACE_LEVEL_INFORMATION,
				TRACE_DRIVER,
				"Valid AUX Switch Configuration for Display Port! SwitchStatus: %d",
				SwitchStatus);

			goto exit;
		}
	}

exit:
	return status;
}

NTSTATUS
FSA4480_Switch(
	WDFDEVICE Device,
	FSA4480_SWITCH_MODE SwitchMode
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;
	BYTE SwitchControl = 0x00;
	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	switch (SwitchMode)
	{
	// TODO: Hook into Audio Jack EU GPIO to swap the button behavior on MBHC headsets
	case FSA4480_SWAP_MIC_GND:
	{
		if (!deviceContext->InitializedSpbHardware)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;

			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Spb Hardware is not yet initialized, aborting - %!STATUS!",
				status);

			goto exit;
		}

		status = SpbReadDataSynchronously(
			&deviceContext->I2CContext,
			FSA4480_SWITCH_CONTROL,
			&SwitchControl,
			1
		);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Error in Spb initialization - %!STATUS!",
				status);

			goto exit;
		}

		if ((SwitchControl & 0x07) == 0x07)
		{
			SwitchControl = 0x00;
		}
		else
		{
			SwitchControl = 0x07;
		}

		status = FSA4480_UpdateSettings(Device, SwitchControl, 0x9F);
		break;
	}
	case FSA4480_SET_USBC_CC1:
	{
		status = FSA4480_UpdateSettings(Device, 0x18, 0xF8);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Error in FSA4480_UpdateSettings - %!STATUS!",
				status);

			goto exit;
		}

		status = FSA4480_ValidateDisplayPortSettings(Device);
		break;
	}
	case FSA4480_SET_USBC_CC2:
	{
		status = FSA4480_UpdateSettings(Device, 0x78, 0xF8);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Error in FSA4480_UpdateSettings - %!STATUS!",
				status);

			goto exit;
		}

		status = FSA4480_ValidateDisplayPortSettings(Device);
		break;
	}
	case FSA4480_SET_DP_DISCONNECTED:
	{
		status = FSA4480_UpdateSettings(Device, 0x18, 0x98);
		break;
	}
	}

exit:
	return status;
}

NTSTATUS
FSA4480_Initialize(
	WDFDEVICE Device
)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = FSA4480_SetDefaultRegisterSettings(Device);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error writing default registers - %!STATUS!",
			status);

		goto exit;
	}

	// TODO: Get real current status
	status = FSA4480_SetupChipGPIOs(Device, UsbCPartnerInvalid);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error setting up chip gpios - %!STATUS!",
			status);

		goto exit;
	}

exit:
	return status;
}

NTSTATUS
FSA4480_Uninitialize(
	WDFDEVICE Device
)
{
	NTSTATUS status;

	// TODO: Do not reset switch settings for usb digital hs
	status = FSA4480_SetupChipGPIOs(Device, UsbCPartnerInvalid);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error setting up chip gpios - %!STATUS!",
			status);

		goto exit;
	}

exit:
	return status;
}