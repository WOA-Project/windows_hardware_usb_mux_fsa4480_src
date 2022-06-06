/*++

Module Name:

	device.c - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.

Environment:

	Kernel-mode Driver Framework

--*/

#include "driver.h"
#include <wdmguid.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <gpio.h>
#include <wdf.h>
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, fsa4480CreateDevice)
#pragma alloc_text (PAGE, fsa4480DevicePrepareHardware)
#endif

#define FSA4480_SWITCH_SETTINGS 0x04
#define FSA4480_SWITCH_CONTROL  0x05
#define FSA4480_SWITCH_STATUS1  0x07
#define FSA4480_SLOW_L          0x08
#define FSA4480_SLOW_R          0x09
#define FSA4480_SLOW_MIC        0x0A
#define FSA4480_SLOW_SENSE      0x0B
#define FSA4480_SLOW_GND        0x0C
#define FSA4480_DELAY_L_R       0x0D
#define FSA4480_DELAY_L_MIC     0x0E
#define FSA4480_DELAY_L_SENSE   0x0F
#define FSA4480_DELAY_L_AGND    0x10
#define FSA4480_RESET           0x1E


/*
* Rob Green, a member of the NTDEV list, provides the
* following set of macros that'll keep you from having
* to scratch your head and count zeros ever again.
* Using these defintions, all you'll have to do is write:
*
* interval.QuadPart = RELATIVE(SECONDS(5));
*/

#ifndef ABSOLUTE
#define ABSOLUTE(wait) (wait)
#endif

#ifndef RELATIVE
#define RELATIVE(wait) (-(wait))
#endif

#ifndef NANOSECONDS
#define NANOSECONDS(nanos) \
	(((signed __int64)(nanos)) / 100L)
#endif

#ifndef MICROSECONDS
#define MICROSECONDS(micros) \
	(((signed __int64)(micros)) * NANOSECONDS(1000L))
#endif

#ifndef MILLISECONDS
#define MILLISECONDS(milli) \
	(((signed __int64)(milli)) * MICROSECONDS(1000L))
#endif

#ifndef SECONDS
#define SECONDS(seconds) \
	(((signed __int64)(seconds)) * MILLISECONDS(1000L))
#endif

NTSTATUS
FSA4480UpdateSettings(
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

VOID
FSA4480ValidateDisplayPortSettings(
	WDFDEVICE Device
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;
	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(Device);

	UINT32 SwitchStatus = 0;

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
	}
	else
	{
		if (SwitchStatus != 0x23 && SwitchStatus != 0x1C)
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Invalid AUX Switch Configuration for Display Port! SwitchStatus: %d",
				SwitchStatus);
		}
		else
		{
			TraceEvents(
				TRACE_LEVEL_INFORMATION,
				TRACE_DRIVER,
				"Valid AUX Switch Configuration for Display Port! SwitchStatus: %d",
				SwitchStatus);
		}
	}
}

VOID
USBCCChangeNotifyCallback(
	PVOID   NotificationContext,
	ULONG   NotifyCode
)
{
	PDEVICE_CONTEXT deviceContext;
	WDFDEVICE device = (WDFDEVICE)NotificationContext;

	deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(device);

	// 0 -> CC1
	// 1 -> CC2
	// 2 -> CC Open
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC!: CC OUT Status = %d\n", NotifyCode);

	deviceContext->CCOUT = NotifyCode;

	if (deviceContext->CCOUT == 2)
	{
		FSA4480UpdateSettings(device, 0x18, 0x98);
	}
	else
	{
		FSA4480UpdateSettings(device, 0x00, 0x9F);

		if (deviceContext->CCOUT == 0)
		{
			FSA4480UpdateSettings(device, 0x18, 0xF8);
		}
		else if (deviceContext->CCOUT == 1)
		{
			FSA4480UpdateSettings(device, 0x78, 0xF8);
		}

		FSA4480ValidateDisplayPortSettings(device);
	}
}

NTSTATUS
RegisterForUSBCCChangeNotification(
	IN WDFDEVICE Device
)
{
	PACPI_INTERFACE_STANDARD2 ACPIInterface;
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;

	PAGED_CODE();

	deviceContext = DeviceGetContext(Device);

	if (deviceContext->RegisteredforNotification)
	{
		goto exit;
	}

	ACPIInterface = &(deviceContext->AcpiInterface);

	status = WdfFdoQueryForInterface(
		Device,
		&GUID_ACPI_INTERFACE_STANDARD2,
		(PINTERFACE)ACPIInterface,
		sizeof(ACPI_INTERFACE_STANDARD2),
		1,
		NULL
	);

	if (!NT_SUCCESS(status))
	{
		goto exit;
	}

	status = ACPIInterface->RegisterForDeviceNotifications(
		ACPIInterface->Context,
		USBCCChangeNotifyCallback,
		Device);

	if (!NT_SUCCESS(status))
	{
		goto exit;
	}

	deviceContext->RegisteredforNotification = TRUE;

exit:
	return status;
}

NTSTATUS
fsa4480CreateDevice(
	_Inout_ PWDFDEVICE_INIT DeviceInit
)
/*++

Routine resription:

	Worker routine called to create a device and its software resources.

Arguments:

	DeviceInit - Pointer to an opaque init structure. Memory for this
					structure will be freed by the framework when the WdfDeviceCreate
					succeeds. So don't access the structure after that point.

Return Value:

	NTSTATUS

--*/
{
	WDF_OBJECT_ATTRIBUTES deviceAttributes;
	PDEVICE_CONTEXT deviceContext;
	WDFDEVICE device;
	NTSTATUS status;
	WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;

	PAGED_CODE();

	//
	// Initialize the PnpPowerCallbacks structure.  Callback events for PNP
	// and Power are specified here.
	//

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
	PnpPowerCallbacks.EvtDevicePrepareHardware = fsa4480DevicePrepareHardware;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

	status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

	if (NT_SUCCESS(status)) {
		//
		// Get a pointer to the device context structure that we just associated
		// with the device object. We define this structure in the device.h
		// header file. DeviceGetContext is an inline function generated by
		// using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
		// This function will do the type checking and return the device context.
		// If you pass a wrong object handle it will return NULL and assert if
		// run under framework verifier mode.
		//
		deviceContext = (PDEVICE_CONTEXT)DeviceGetContext(device);

		//
		// Initialize the context.
		//
		deviceContext->Device = device;

		//
		// Register for notifications
		//
		status = RegisterForUSBCCChangeNotification(device);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DRIVER,
				"Error in RegisterForUSBCCChangeNotification - %!STATUS!",
				status);

			goto exit;
		}
	}

exit:
	return status;
}

NTSTATUS
InitializeFSA4480(
	IN WDFDEVICE Device
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;

	deviceContext = DeviceGetContext(Device);

	BYTE Data = 0;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Writing default values");

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SLOW_L,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_SLOW_L write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SLOW_R,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_SLOW_R write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SLOW_MIC,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_SLOW_MIC write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SLOW_SENSE,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_SLOW_SENSE write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_SLOW_GND,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_SLOW_GND write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_DELAY_L_R,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_DELAY_L_R write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_DELAY_L_MIC,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_DELAY_L_MIC write - %!STATUS!",
			status);

		goto exit;
	}

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_DELAY_L_SENSE,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_DELAY_L_SENSE write - %!STATUS!",
			status);

		goto exit;
	}

	Data = 0x09;

	status = SpbWriteDataSynchronously(
		&deviceContext->I2CContext,
		FSA4480_DELAY_L_AGND,
		&Data,
		1);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb FSA4480_DELAY_L_AGND write - %!STATUS!",
			status);

		goto exit;
	}

	Data = 0x98;

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
			"Error in Spb FSA4480_SWITCH_SETTINGS write - %!STATUS!",
			status);

		goto exit;
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Done writing default values");

exit:
	return status;
}

_Use_decl_annotations_
NTSTATUS
fsa4480DevicePrepareHardware(
	WDFDEVICE Device,
	WDFCMRESLIST ResourcesRaw,
	WDFCMRESLIST ResourcesTranslated
)

/*++

Routine resription:

	EvtDevicePrepareHardware event callback performs operations that are
	necessary to make the driver's device operational. The framework calls the
	driver's EvtDevicePrepareHardware callback when the PnP manager sends an
	IRP_MN_START_DEVICE request to the driver stack.

Arguments:

	Device - Supplies a handle to a framework device object.

	ResourcesRaw - Supplies a handle to a collection of framework resource
		objects. This collection identifies the raw (bus-relative) hardware
		resources that have been assigned to the device.

	ResourcesTranslated - Supplies a handle to a collection of framework
		resource objects. This collection identifies the translated
		(system-physical) hardware resources that have been assigned to the
		device. The resources appear from the CPU's point of view. Use this list
		of resources to map I/O space and device-accessible memory into virtual
		address space

Return Value:

	NTSTATUS

--*/

{
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR res, resRaw;
	ULONG resourceCount;
	ULONG i;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "Entering %!FUNC!\n");
	PAGED_CODE();

	PDEVICE_CONTEXT devContext = DeviceGetContext(Device);

	devContext->Device = Device;

	//
	// Get the resouce hub connection ID for our I2C driver
	//
	resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Looking for resources");

	BOOLEAN I2CFound = FALSE;
	UINT32 k = 0;

	for (i = 0; i < resourceCount; i++)
	{
		res = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
		resRaw = WdfCmResourceListGetDescriptor(ResourcesRaw, i);

		switch (res->Type)
		{
		case CmResourceTypeConnection:
		{
			if (res->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				res->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				devContext->I2CContext.I2cResHubId.LowPart =
					res->u.Connection.IdLowPart;
				devContext->I2CContext.I2cResHubId.HighPart =
					res->u.Connection.IdHighPart;

				TraceEvents(
					TRACE_LEVEL_INFORMATION,
					TRACE_DRIVER,
					"Found I2C!");

				I2CFound = TRUE;
			}
			else if (res->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_GPIO &&
				res->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_GPIO_IO)
			{
				switch (k) {
				case 0:
					TraceEvents(
						TRACE_LEVEL_INFORMATION,
						TRACE_DRIVER,
						"Found CCOUT GPIO!");

					devContext->CCOutGpioId.LowPart = res->u.Connection.IdLowPart;
					devContext->CCOutGpioId.HighPart = res->u.Connection.IdHighPart;
					break;
				case 1:
					TraceEvents(
						TRACE_LEVEL_INFORMATION,
						TRACE_DRIVER,
						"Found EN GPIO!");

					devContext->EnGpioId.LowPart = res->u.Connection.IdLowPart;
					devContext->EnGpioId.HighPart = res->u.Connection.IdHighPart;
					break;
				default:
					break;
				}

				k++;
			}
			break;
		}
		}
	}

	if (!(I2CFound && k == 2))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error finding CmResourceTypeConnection resources - %!STATUS!",
			status);

		goto exit;
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"All resources have been found");

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Initializing I2C Bus");

	//
	// Initialize Spb so the driver can issue reads/writes
	//
	status = SpbTargetInitialize(Device, &devContext->I2CContext);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in Spb initialization - %!STATUS!",
			status);

		goto exit;
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Initializing FSA4480");

	status = InitializeFSA4480(Device);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in FSA4480 initialization - %!STATUS!",
			status);

		goto exit;
	}

exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "Leaving %!FUNC!: Status = 0x%08lX\n", status);
	return status;
}