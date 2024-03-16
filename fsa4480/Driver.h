/*++

Module Name:

	driver.h

Abstract:

	This file contains the driver definitions.

Environment:

	Kernel-mode Driver Framework

--*/

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

#include "device.h"
#include "trace.h"

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD fsa4480EvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP fsa4480EvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE fsa4480DevicePrepareHardware;
EVT_WDF_DRIVER_UNLOAD fsa4480EvtDriverUnload;

VOID fsa4480DeviceUnPrepareHardware(
	WDFDEVICE Device);