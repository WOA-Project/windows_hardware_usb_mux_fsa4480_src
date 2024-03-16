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
#include "fsa4480.h"
#include <Acpiioct.h>
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, fsa4480CreateDevice)
#pragma alloc_text(PAGE, fsa4480DevicePrepareHardware)
#pragma alloc_text(PAGE, OnInternalDeviceControl)
#pragma alloc_text(PAGE, OnRequestCompletionRoutine)
#endif

NTSTATUS UtilitySetGPIO(
	WDFIOTARGET GpioIoTarget,
	UCHAR Value)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_MEMORY_DESCRIPTOR inputDescriptor, outputDescriptor;
	UCHAR Buffer[1];

	Buffer[0] = Value;

	if (GpioIoTarget == NULL)
	{
		status = STATUS_INVALID_HANDLE;
		goto exit;
	}

	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputDescriptor, (PVOID)&Buffer, sizeof(Buffer));
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor, (PVOID)&Buffer, sizeof(Buffer));

	status = WdfIoTargetSendIoctlSynchronously(GpioIoTarget, NULL, IOCTL_GPIO_WRITE_PINS, &inputDescriptor, &outputDescriptor, NULL, NULL);

exit:
	return status;
}

NTSTATUS UtilityOpenIOTarget(
	PDEVICE_CONTEXT DeviceContext,
	LARGE_INTEGER Resource,
	ACCESS_MASK UseMask,
	WDFIOTARGET *IoTraget)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES ObjectAttributes;
	WDF_IO_TARGET_OPEN_PARAMS OpenParams;
	UNICODE_STRING ReadString;
	WCHAR ReadStringBuffer[260];

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

	RtlInitEmptyUnicodeString(&ReadString,
							  ReadStringBuffer,
							  sizeof(ReadStringBuffer));

	status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&ReadString,
											  Resource.LowPart,
											  Resource.HighPart);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "RESOURCE_HUB_CREATE_PATH_FROM_ID failed %!STATUS!\n", status);
		return status;
	}

	WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
	ObjectAttributes.ParentObject = DeviceContext->Device;

	status = WdfIoTargetCreate(DeviceContext->Device, &ObjectAttributes, IoTraget);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfIoTargetCreate failed %!STATUS!\n", status);
		return status;
	}

	WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&OpenParams, &ReadString, UseMask);
	status = WdfIoTargetOpen(*IoTraget, &OpenParams);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfIoTargetOpen failed %!STATUS!\n", status);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
	return status;
}

VOID USBCCChangeNotifyCallback(
	PVOID NotificationContext,
	ULONG NotifyCode)
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
		FSA4480_Switch(device, FSA4480_SET_DP_DISCONNECTED);
	}
	else if (deviceContext->CCOUT == 0)
	{
		FSA4480_Switch(device, FSA4480_SET_USBC_CC1);
	}
	else if (deviceContext->CCOUT == 1)
	{
		FSA4480_Switch(device, FSA4480_SET_USBC_CC2);
	}
}

VOID OnInternalDeviceControl(
	IN WDFQUEUE Queue,
	IN WDFREQUEST Request,
	IN size_t OutputBufferLength,
	IN size_t InputBufferLength,
	IN ULONG IoControlCode)
/*++

Routine Description:

	This routine is the dispatch routine for internal device control requests.

Arguments:

	Queue - Handle to the framework queue object that is associated
			with the I/O request.
	Request - Handle to a framework request object.

	OutputBufferLength - length of the request's output buffer,
						if an output buffer is available.
	InputBufferLength - length of the request's input buffer,
						if an input buffer is available.

	IoControlCode - the driver-defined or system-defined I/O control code
					(IOCTL) that is associated with the request.

Return Value:

   VOID

--*/
{
	NTSTATUS status;
	WDFDEVICE device;
	BOOLEAN forwardWithCompletionRoutine = FALSE;
	BOOLEAN requestSent = TRUE;
	WDF_REQUEST_SEND_OPTIONS options;
	WDFMEMORY outputMemory;
	WDFMEMORY inputMemory;
	WDFIOTARGET Target;

	UNREFERENCED_PARAMETER(OutputBufferLength);

	PAGED_CODE();

	device = WdfIoQueueGetDevice(Queue);
	Target = WdfDeviceGetIoTarget(device);

	//
	// Please note that ACPI provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl. So depending on the ioctl code, we will either
	// use retreive function or escape to WDM to get the UserBuffer.
	//

	switch (IoControlCode)
	{

	case IOCTL_ACPI_EVAL_METHOD:
		//
		// Obtains Eval Method Output buffer for the USB C Connector Manager device
		//
		forwardWithCompletionRoutine = TRUE;
		break;

	default:
		break;
	}

	//
	// Forward the request down. WdfDeviceGetIoTarget returns
	// the default target, which represents the device attached to us below in
	// the stack.
	//
	if (forwardWithCompletionRoutine)
	{
		//
		// Format the request with the input and output memory so the completion routine
		// can access the return data in order to cache it into the context area
		//
		status = WdfRequestRetrieveInputMemory(Request, &inputMemory);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfRequestRetrieveInputMemory failed: 0x%x\n",
				status);

			WdfRequestComplete(Request, status);
			return;
		}

		status = WdfRequestRetrieveOutputMemory(Request, &outputMemory);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfRequestRetrieveOutputMemory failed: 0x%x\n",
				status);

			WdfRequestComplete(Request, status);
			return;
		}

		status = WdfIoTargetFormatRequestForInternalIoctl(
			Target,
			Request,
			IoControlCode,
			inputMemory,
			NULL,
			outputMemory,
			NULL);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfIoTargetFormatRequestForInternalIoctl failed: 0x%x\n",
				status);

			WdfRequestComplete(Request, status);
			return;
		}

		//
		// Set our completion routine with a context area that we will save
		// the output data into
		//
		WdfRequestSetCompletionRoutine(
			Request,
			OnRequestCompletionRoutine,
			&InputBufferLength);

		requestSent = WdfRequestSend(
			Request,
			Target,
			WDF_NO_SEND_OPTIONS);
	}
	else
	{
		//
		// We are not interested in post processing the IRP so
		// fire and forget.
		//
		WDF_REQUEST_SEND_OPTIONS_INIT(
			&options,
			WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

		requestSent = WdfRequestSend(Request, Target, &options);
	}

	if (requestSent == FALSE)
	{
		status = WdfRequestGetStatus(Request);

		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DEVICE,
			"WdfRequestSend failed: 0x%x\n",
			status);

		WdfRequestComplete(Request, status);
	}
}

VOID OnRequestCompletionRoutine(
	IN WDFREQUEST Request,
	IN WDFIOTARGET Target,
	IN PWDF_REQUEST_COMPLETION_PARAMS Params,
	IN WDFCONTEXT Context)
/*++

Routine Description:

	Completion Routine

Arguments:

	Target - Target handle
	Request - Request handle
	Params - request completion params
	Context - Driver supplied context


Return Value:

	VOID

--*/
{
	WDFMEMORY inputMemory = Params->Parameters.Ioctl.Input.Buffer;
	WDFMEMORY outputMemory = Params->Parameters.Ioctl.Output.Buffer;
	NTSTATUS status = Params->IoStatus.Status;

	ULONG inputBufferLength = 0;
	PUCHAR inputBuffer = NULL;

	ULONG outputBufferLength = 0;
	PUCHAR outputBuffer = NULL;

	UNREFERENCED_PARAMETER(Target);

	if (NT_SUCCESS(status) &&
		Params->Type == WdfRequestTypeDeviceControlInternal &&
		Params->Parameters.Ioctl.IoControlCode == IOCTL_ACPI_EVAL_METHOD)
	{
		inputBufferLength = *(PULONG)Context;

		inputBuffer = (PUCHAR)ExAllocatePoolWithTag(
			NonPagedPoolNx,
			inputBufferLength,
			ACPI_INPUT_BUFFER_POOL_TAG);

		if (NULL == inputBuffer)
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"Could not allocate input buffer!");

			status = STATUS_UNSUCCESSFUL;
			goto exit;
		}

		status = WdfMemoryCopyToBuffer(
			inputMemory,
			Params->Parameters.Ioctl.Input.Offset,
			inputBuffer,
			inputBufferLength);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfMemoryCopyToBuffer failed: 0x%x\n",
				status);

			status = STATUS_UNSUCCESSFUL;
			goto free_input_buffer;
		}

		// You can parse the input buffer here
		// You can also edit the input buffer here
		// And any change will be reflected onto the upper layer

		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "FSA4480: ACPIINBUF: LENGTH=%d", inputBufferLength);
		for (ULONG j = 0; j < inputBufferLength; j++)
		{
			UCHAR byte = *(inputBuffer + j);
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, " %02hhX", byte);
		}
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "\n");

		status = WdfMemoryCopyFromBuffer(
			inputMemory,
			0,
			(PVOID)inputBuffer,
			inputBufferLength);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfMemoryCopyFromBuffer failed: 0x%x\n",
				status);

			status = STATUS_UNSUCCESSFUL;
			goto free_input_buffer;
		}

	free_input_buffer:
		ExFreePoolWithTag(inputBuffer, ACPI_INPUT_BUFFER_POOL_TAG);

		if (!NT_SUCCESS(status))
		{
			goto exit;
		}

		outputBufferLength = (ULONG)Params->Parameters.Ioctl.Output.Length;

		outputBuffer = (PUCHAR)ExAllocatePoolWithTag(
			NonPagedPoolNx,
			outputBufferLength,
			ACPI_OUTPUT_BUFFER_POOL_TAG);

		if (NULL == outputBuffer)
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"Could not allocate output buffer!");

			status = STATUS_UNSUCCESSFUL;
			goto exit;
		}

		status = WdfMemoryCopyToBuffer(
			outputMemory,
			Params->Parameters.Ioctl.Output.Offset,
			outputBuffer,
			outputBufferLength);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfMemoryCopyToBuffer failed: 0x%x\n",
				status);

			status = STATUS_UNSUCCESSFUL;
			goto free_output_buffer;
		}

		// You can parse the output buffer here
		// You can also edit the output buffer here
		// And any change will be reflected onto the upper layer

		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "FSA4480: ACPIOUTBUF: LENGTH=%d", outputBufferLength);
		for (ULONG j = 0; j < outputBufferLength; j++)
		{
			UCHAR byte = *(outputBuffer + j);
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, " %02hhX", byte);
		}
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "\n");

		status = WdfMemoryCopyFromBuffer(
			outputMemory,
			0,
			(PVOID)outputBuffer,
			outputBufferLength);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfMemoryCopyFromBuffer failed: 0x%x\n",
				status);

			status = STATUS_UNSUCCESSFUL;
			goto free_output_buffer;
		}

	free_output_buffer:
		ExFreePoolWithTag(outputBuffer, ACPI_OUTPUT_BUFFER_POOL_TAG);
	}

exit:
	WdfRequestComplete(Request, status);
	return;
}

NTSTATUS
RegisterForUSBCCChangeNotification(
	IN WDFDEVICE Device)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT deviceContext;
	WDF_IO_QUEUE_CONFIG queueConfig;

	PAGED_CODE();

	deviceContext = DeviceGetContext(Device);

	if (deviceContext->InitializedAcpiInterface)
	{
		goto exit;
	}

	//
	// Create a parallel dispatch queue to handle requests from ACPI
	//
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
		&queueConfig,
		WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = OnInternalDeviceControl;

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		WDF_NO_HANDLE); // pointer to default queue

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DEVICE,
			"Error creating WDF default queue - 0x%08lX",
			status);

		goto exit;
	}

	deviceContext->InitializedAcpiInterface = TRUE;

exit:
	return status;
}

NTSTATUS
fsa4480CreateDevice(
	_Inout_ PWDFDEVICE_INIT DeviceInit)
/*++

Routine description:

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

	if (NT_SUCCESS(status))
	{
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

_Use_decl_annotations_
	NTSTATUS
	fsa4480DevicePrepareHardware(
		WDFDEVICE Device,
		WDFCMRESLIST ResourcesRaw,
		WDFCMRESLIST ResourcesTranslated)

/*++

Routine description:

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
				switch (k)
				{
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

	devContext->InitializedSpbHardware = TRUE;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Turning on FSA4480");

	status = UtilityOpenIOTarget(
		devContext,
		devContext->EnGpioId,
		GENERIC_READ | GENERIC_WRITE,
		&devContext->EnGpio);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error opening enable gpio - %!STATUS!",
			status);

		goto exit;
	}

	devContext->InitializedEnGpioHardware = TRUE;

	// Enable by setting the pin LOW.
	status = UtilitySetGPIO(devContext->EnGpio, 0);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error setting enable gpio to low - %!STATUS!",
			status);

		goto exit;
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"Initializing FSA4480");

	status = FSA4480_Initialize(Device);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"Error in FSA4480 initialization - %!STATUS!",
			status);

		goto exit;
	}

	devContext->InitializedFSAHardware = TRUE;

exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "Leaving %!FUNC!: Status = 0x%08lX\n", status);
	return status;
}

VOID fsa4480DeviceUnPrepareHardware(
	WDFDEVICE Device)
{
	PDEVICE_CONTEXT devContext = DeviceGetContext(Device);

	if (devContext->InitializedAcpiInterface)
	{
		devContext->InitializedAcpiInterface = FALSE;
	}

	if (devContext->InitializedFSAHardware)
	{
		FSA4480_Uninitialize(Device);
		devContext->InitializedFSAHardware = FALSE;
	}

	if (devContext->InitializedEnGpioHardware)
	{
		UtilitySetGPIO(devContext->EnGpio, 1);
		WdfIoTargetClose(devContext->EnGpio);
		devContext->InitializedEnGpioHardware = FALSE;
	}

	if (devContext->InitializedSpbHardware)
	{
		SpbTargetDeinitialize(Device, &devContext->I2CContext);
		devContext->InitializedSpbHardware = FALSE;
	}
}