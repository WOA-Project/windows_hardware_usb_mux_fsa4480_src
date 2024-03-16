#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usbctypes.h>

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

#define FSA4480_SWITCH_SETTINGS 0x04
#define FSA4480_SWITCH_CONTROL 0x05
#define FSA4480_SWITCH_STATUS1 0x07
#define FSA4480_SLOW_L 0x08
#define FSA4480_SLOW_R 0x09
#define FSA4480_SLOW_MIC 0x0A
#define FSA4480_SLOW_SENSE 0x0B
#define FSA4480_SLOW_GND 0x0C
#define FSA4480_DELAY_L_R 0x0D
#define FSA4480_DELAY_L_MIC 0x0E
#define FSA4480_DELAY_L_SENSE 0x0F
#define FSA4480_DELAY_L_AGND 0x10
#define FSA4480_RESET 0x1E

typedef struct _FSA4480_DEFAULT_REGISTER_SETTING
{
	BYTE Address;
	BYTE Value;
} FSA4480_DEFAULT_REGISTER_SETTING, *PFSA4480_DEFAULT_REGISTER_SETTING;

static FSA4480_DEFAULT_REGISTER_SETTING gDefaultRegisterSettings[] =
	{
		{FSA4480_SLOW_L, 0x00},
		{FSA4480_SLOW_R, 0x00},
		{FSA4480_SLOW_MIC, 0x00},
		{FSA4480_SLOW_SENSE, 0x00},
		{FSA4480_SLOW_GND, 0x00},
		{FSA4480_DELAY_L_R, 0x00},
		{FSA4480_DELAY_L_MIC, 0x00},
		{FSA4480_DELAY_L_SENSE, 0x00},
		{FSA4480_DELAY_L_AGND, 0x09},
		{FSA4480_SWITCH_SETTINGS, 0x98},
};

typedef enum _FSA4480_SWITCH_MODE
{
	FSA4480_SWAP_MIC_GND,
	FSA4480_SET_USBC_CC1,
	FSA4480_SET_USBC_CC2,
	FSA4480_SET_DP_DISCONNECTED
} FSA4480_SWITCH_MODE;

NTSTATUS
FSA4480_Switch(
	WDFDEVICE Device,
	FSA4480_SWITCH_MODE SwitchMode);

NTSTATUS
FSA4480_Initialize(
	WDFDEVICE Device);

NTSTATUS
FSA4480_Uninitialize(
	WDFDEVICE Device);

NTSTATUS
FSA4480_OnUSBCModeChanged(
	WDFDEVICE Device,
	USBC_PARTNER USBCPartner);