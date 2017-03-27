#ifndef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#endif						

#ifdef ALLOC_PRAGMA
#pragma alloc_text( INIT, DriverEntry ) 
#pragma alloc_text( PAGE, USBlockerAddDevice) 
#pragma alloc_text( PAGE, USBlockerDispatchAny) 
#pragma alloc_text( PAGE, IOControl) 
#pragma alloc_text( PAGE, USBlockerPnP)
#pragma alloc_text( PAGE, USBlockerUnload)
#pragma alloc_text( PAGE, CompleteRequest)
#endif //end ALLOC_PRAGMA


#ifdef __cplusplus
extern "C" 
{

#endif

//#include "VisualDDKHelpers.h"
#include <ntddk.h>
#include <wdm.h>
#include <usb100.h>
#include <usbdrivr.h>
#include <specstrings.h>
#include <ntddstor.h>
#include <mountdev.h>
#include <ntddvol.h>
#include <usbioctl.h>
#include <usbdlib.h>
#include <stdio.h>


#ifdef __cplusplus
}
#endif
