/*++

Module Name:

	device.h

Abstract:

	This file contains the device definitions.

Environment:

	Kernel-mode Driver Framework

--*/

#pragma once

#include "spb.h"
#include <usbctypes.h>

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _DEVICE_CONTEXT
{
	//
	// Device handle
	//
	WDFDEVICE Device;

	BOOLEAN InitializedSpbHardware;
	BOOLEAN InitializedEnGpioHardware;
	BOOLEAN InitializedAcpiInterface;
	BOOLEAN InitializedFSAHardware;

	//
	// Spb (I2C) related members used for the lifetime of the device
	//
	SPB_CONTEXT I2CContext;

	LARGE_INTEGER CCOutGpioId;
	WDFIOTARGET CCOutGpio;

	LARGE_INTEGER EnGpioId;
	WDFIOTARGET EnGpio;

	ULONG CCOUT;
	USBC_PARTNER USBCPartner;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// Function to initialize the device and its callbacks
//
NTSTATUS
fsa4480CreateDevice(
	_Inout_ PWDFDEVICE_INIT DeviceInit);

#define ACPI_OUTPUT_BUFFER_POOL_TAG 'bOcA'
#define ACPI_INPUT_BUFFER_POOL_TAG 'bIcA'

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OnInternalDeviceControl;

EVT_WDF_REQUEST_COMPLETION_ROUTINE OnRequestCompletionRoutine;