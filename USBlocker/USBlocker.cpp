
#include "stdafx.h"
#include <usb.h>
#include <usbioctl.h>
#include "USBlocker.h"

//using namespace std;

/*
	USBlocker - Main file
	This file contains a very simple implementation of a WDM driver. Note that it does not support all
	WDM functionality, or any functionality sufficient for practical use. The only thing this driver does
	perfectly, is loading and unloading.

	To install the driver, go to Control Panel -> Add Hardware Wizard, then select "Add a new hardware device".
	Select "manually select from list", choose device category, press "Have Disk" and enter the path to your
	INF file.
	Note that not all device types (specified as Class in INF file) can be installed that way.

	To start/stop this driver, use Windows Device Manager (enable/disable device command).

	If you want to speed up your driver development, it is recommended to see the BazisLib library, that
	contains convenient classes for standard device types, as well as a more powerful version of the driver
	wizard. To get information about BazisLib, see its website:
		http://bazislib.sysprogs.org/
*/


VOID USBlockerUnload(IN PDRIVER_OBJECT DriverObject);
VOID dumpBuffer(IN ULONG bufSize,IN PVOID pBuffer, IN PMDL pMdl);
NTSTATUS USBlockerCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
//NTSTATUS USBlockerCreate(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS USBlockerDispatchAny(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS USBlockerAddDevice(IN PDRIVER_OBJECT  DriverObject, IN PDEVICE_OBJECT  PhysicalDeviceObject);
NTSTATUS USBlockerPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS IOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
//NTSTATUS wmiControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
//NTSTATUS dispatchIOCTL(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS dispatchPower(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS getDeviceDescriptor(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS USBCall(IN PDEVICE_OBJECT DeviceObject, IN PURB urb, IN PIRP Irp);
NTSTATUS inspectReturnedURB(IN PDEVICE_OBJECT fdio, IN PIRP Irp, IN KEVENT Context);
VOID CompleteRequest(IN PIRP Irp, IN NTSTATUS status, IN ULONG_PTR info);

// {5e0e7886-b727-4a14-b692-8ffc70f7b7ea}
static const GUID GUID_USBlockerInterface = {0x5E0E7886, 0xb727, 0x4a14, {0xb6, 0x92, 0x8f, 0xfc, 0x70, 0xf7, 0xb7, 0xea } };
UNICODE_STRING srvkey;
//DRIVER_INITIALIZE DriverEntry;
//DRIVER_ADD_DEVICE USBlockerAddDevice;

#ifdef __cplusplus
extern "C" NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath);
#endif


NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath)
{
	unsigned int i;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	DEBUG_ENTER_FUNCTION("DriverObject=0x%p; RegistryPath=\"%wZ\"", DriverObject, RegistryPath);
	PAGED_CODE();

#if DBG
	DbgBreakPoint();
#endif
				
	status = STATUS_SUCCESS;
	srvkey.Buffer = (PWSTR)ExAllocatePool(NonPagedPool,RegistryPath->MaximumLength);
	if(srvkey.Buffer==NULL)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;

		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	srvkey.MaximumLength = RegistryPath->MaximumLength;
	RtlCopyUnicodeString((PUNICODE_STRING)srvkey.Buffer,(PUNICODE_STRING)RegistryPath->Buffer);
	for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = USBlockerDispatchAny;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = USBlockerCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = USBlockerCreateClose;
	DriverObject->MajorFunction[IRP_MJ_PNP] = USBlockerPnP;
	DriverObject->MajorFunction[IRP_MJ_POWER] = dispatchPower;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL]= IOControl;

	DriverObject->DriverUnload = USBlockerUnload;
	DriverObject->DriverExtension->AddDevice = USBlockerAddDevice;

	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


//set information and status of IoStatus member of IRP request in order to complete it
VOID CompleteRequest(IN PIRP Irp, IN NTSTATUS Status, IN ULONG_PTR Information)
{
	DEBUG_ENTER_FUNCTION("Irp=0x%p; Status=0x%x; Information=%u", Irp, Status, Information);

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = Information;
	IoCompleteRequest(Irp,IO_NO_INCREMENT);

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}


VOID USBlockerUnload(IN PDRIVER_OBJECT DriverObject)
{

	PAGED_CODE();
	DEBUG_ENTER_FUNCTION("DriverObject=0x%p", DriverObject);

	//free registryPath
	if(srvkey.Buffer)
	{
		ExFreePool(srvkey.Buffer);
		srvkey.Buffer = NULL;
	}

	IoDeleteDevice(DriverObject->DeviceObject);

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}


NTSTATUS USBlockerCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	PUSBlocker_DEVICE_EXTENSION dex = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

	dex = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	stack = IoGetCurrentIrpStackLocation(Irp);
	status = IoAcquireRemoveLock(&dex->RemoveLock,Irp);
	if(!NT_SUCCESS(status))
	{
		CompleteRequest(Irp,status,0);

		// We completed the IRP, so it belongs to I/O manager, not to us.
		// It might actually be freed, so we must not touch it anymore.
		// Let's return.
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	switch(stack->MajorFunction)
	{

	case IRP_MJ_CREATE:
		DbgPrint("%S: entering in IRP_MJ_CREATE.\n",DRV_NAME);
		// We are not registering the completion routine, nor using our
		// stack location for any other purpose. So, we can just skip it
		// and provide it to the lower driver instead of copying it
		// (although skipping or copying... it actually does not matter).
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(dex->lowerDeviceObject,Irp);
		break;
	case IRP_MJ_CLOSE:
		DbgPrint("%S: entering in IRP_MJ_CLOSE.\n",DRV_NAME);
		// We are not registering the completion routine, nor using our
		// stack location for any other purpose. So, we can just skip it
		// and provide it to the lower driver instead of copying it
		// (although skipping or copying... it actually does not matter).
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(dex->lowerDeviceObject,Irp);
		break;
	case IRP_MJ_CLEANUP:
		DbgPrint("%S: entering in IRP_MJ_CLEANUP.\n", DRV_NAME);
		// We are not registering the completion routine, nor using our
		// stack location for any other purpose. So, we can just skip it
		// and provide it to the lower driver instead of copying it
		// (although skipping or copying... it actually does not matter).
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(dex->lowerDeviceObject, Irp);
		break;
	default:
		break;
	}

	IoReleaseRemoveLock(&dex->RemoveLock, Irp);

	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


NTSTATUS USBlockerDispatchAny(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PUSBlocker_DEVICE_EXTENSION deviceExtension = NULL;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

	deviceExtension = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	status = IoAcquireRemoveLock(&deviceExtension->RemoveLock,Irp);
	if(!NT_SUCCESS(status))
	{
		CompleteRequest(Irp,status,0);

		// We completed the IRP, so it belongs to I/O manager, not to us.
		// It might actually be freed, so we must not touch it anymore.
		// Let's return.
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	status = IoCallDriver(deviceExtension->lowerDeviceObject, Irp);
	IoReleaseRemoveLock(&deviceExtension->RemoveLock,Irp);
	
	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


NTSTATUS USBlockerAddDevice(IN PDRIVER_OBJECT  DriverObject, IN PDEVICE_OBJECT  PhysicalDeviceObject)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	 DEBUG_ENTER_FUNCTION("DriverObject=0x%p; PhysicalDeviceObject=0x%p", DriverObject, PhysicalDeviceObject);
	 PAGED_CODE();

	 status = STATUS_SUCCESS;
	//determine whether driver is running in safe mode or not
	if(*InitSafeBootMode > 0)
	{
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	PDEVICE_OBJECT fido = NULL;
	PUSBlocker_DEVICE_EXTENSION pdx = NULL;
	PDEVICE_OBJECT highestDO = IoGetAttachedDeviceReference(PhysicalDeviceObject);

	status = IoCreateDevice(DriverObject,
						    sizeof(USBlocker_DEVICE_EXTENSION),
							NULL, //no name provided
							highestDO->DeviceType,
							(highestDO->Characteristics & FILE_REMOVABLE_MEDIA), //remove FILE_DEVICE_SECURE_OPEN change to 0
							(highestDO->Flags & DO_EXCLUSIVE) != 0,
							&fido); // change device type to FILE_DEVICE_UNKNOWN

	ObDereferenceObject(highestDO);
	if (!NT_SUCCESS(status))
	{
		//DBGUSBlock: Cannot Create Device Object. Status C0000035 ~ STATUS_OBJECT_NAME_COLLISION

		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	pdx = (PUSBlocker_DEVICE_EXTENSION)fido->DeviceExtension;
	RtlZeroMemory(pdx,sizeof(PUSBlocker_DEVICE_EXTENSION));
		
	DbgPrint("%S: Symbolic Link Name is %T.\n",DRV_NAME,&pdx->DeviceInterface);

	pdx->DeviceObject = fido;
	pdx->PhysicalDeviceObject = PhysicalDeviceObject;

	IoInitializeRemoveLock(&pdx->RemoveLock,0,0,0);
	status = IoAttachDeviceToDeviceStackSafe(fido, PhysicalDeviceObject, &pdx->lowerDeviceObject);
	if(!NT_SUCCESS(status))
	{
		IoDeleteDevice(fido);

		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}
		
	status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_USBlockerInterface, NULL, &pdx->DeviceInterface);
	if (!NT_SUCCESS(status)) {
		// Failed to register the device interface
		IoDetachDevice(pdx->lowerDeviceObject);
		IoDeleteDevice(fido);
		
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	highestDO = pdx->lowerDeviceObject;
	fido->Flags |= highestDO->Flags & (DO_DIRECT_IO | DO_POWER_PAGABLE | DO_POWER_INRUSH | DO_BUFFERED_IO);
	fido->DeviceType = highestDO->DeviceType;
	fido->Characteristics = highestDO->Characteristics |= FILE_DEVICE_SECURE_OPEN;

	fido->Flags &= ~DO_DEVICE_INITIALIZING;

	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


NTSTATUS USBlockerIrpCompletion(
					  IN PDEVICE_OBJECT DeviceObject,
					  IN PIRP Irp,
					  IN PVOID Context
					  )
{
	PKEVENT Event = (PKEVENT) Context;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p; Context=0x%p", DeviceObject, Irp, Context);

	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	KeSetEvent(Event, IO_NO_INCREMENT, FALSE);

	DEBUG_EXIT_FUNCTION("0x%x", STATUS_MORE_PROCESSING_REQUIRED);
	return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS USBlockerForwardIrpSynchronous(
							  IN PDEVICE_OBJECT DeviceObject,
							  IN PIRP Irp
							  )
{
	KEVENT event;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PUSBlocker_DEVICE_EXTENSION deviceExtension = NULL;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	deviceExtension = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, USBlockerIrpCompletion, &event, TRUE, TRUE, TRUE);
	status = IoCallDriver(deviceExtension->lowerDeviceObject, Irp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = Irp->IoStatus.Status;
	}

	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


NTSTATUS dispatchPower(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PUSBlocker_DEVICE_EXTENSION pdx = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

	status = IoAcquireRemoveLock(&pdx->RemoveLock, Irp);
	if (!NT_SUCCESS(status)) {
		CompleteRequest(Irp, status, 0);
		
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);
	status = PoCallDriver(pdx->lowerDeviceObject, Irp);
	IoReleaseRemoveLock(&pdx->RemoveLock, Irp);

	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


NTSTATUS getDeviceDescriptor(IN PDEVICE_OBJECT DeviceObject,IN PIRP Irp)
{
	NTSTATUS status;

	//URB
	PURB urb;
	USHORT urbSize = sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST);

	PUSBlocker_DEVICE_EXTENSION deviceExtension = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

	ULONG			   response;
	UNICODE_STRING	   title, text;
	ULONG_PTR		   param[3];

	//device descriptor
	PUSB_DEVICE_DESCRIPTOR pDescriptor;
	ULONG sizedd = sizeof(_USB_DEVICE_DESCRIPTOR);

	urb = NULL;

	urb = (PURB)ExAllocatePool(NonPagedPool,urbSize);

	if(urb)
	{	
		pDescriptor = (PUSB_DEVICE_DESCRIPTOR)ExAllocatePool(NonPagedPool,sizedd);

		if(pDescriptor)
		{
			UsbBuildGetDescriptorRequest(
				urb,                                                 // Points to the URB to be formatted
				urbSize,
				USB_DEVICE_DESCRIPTOR_TYPE,
				0,                                                    // Not used for device descriptors
				0,                                                    // Not used for device descriptors
				pDescriptor,                                          // Points to a USB_DEVICE_DESCRIPTOR structure
				NULL,
				sizedd,												// Size of a _USB_DEVICE_DESCRIPTOR
				NULL
				);
			
			//sending urb to the appropriate lower-level device driver
			status = USBCall(DeviceObject,urb,Irp);
			
			//if usbcall status failed or even urb failed at usb host controller
			if(!NT_SUCCESS(status) || !USBD_STATUS(urb->UrbHeader.Status))
			{
				KdPrint(("%S: STATUS is %x and URB status is %x\n",DRV_NAME,status,urb->UrbHeader.Status));
				status = STATUS_UNSUCCESSFUL;
			}
			
			//report all information from device descriptor
			KdPrint(("%S: USB_DEVICE_DESCRIPTOR...\n",DRV_NAME));
			KdPrint(("%S: Length of the descriptor data structure= %u (bytes)\n",DRV_NAME,pDescriptor->bLength));
			KdPrint(("%S: Descriptor type= %x\n",DRV_NAME,pDescriptor->bDescriptorType));
			KdPrint(("%S: Version of USB specification= %x\n",DRV_NAME,pDescriptor->bcdUSB));
			KdPrint(("%S: idProduct= %x\n",DRV_NAME,pDescriptor->idProduct));
			KdPrint(("%S: idVendor= %x\n",DRV_NAME,pDescriptor->idVendor));
			KdPrint(("%S: bMaxPacketSize0= %x\n",DRV_NAME,pDescriptor->bMaxPacketSize0));
			KdPrint(("%S: bDeviceSubClass= %x\n",DRV_NAME,pDescriptor->bDeviceSubClass));
			KdPrint(("%S: bDeviceClass= %x\n",DRV_NAME,pDescriptor->bDeviceClass));
			KdPrint(("%S: iProduct= %x\n",DRV_NAME,pDescriptor->iProduct));
			KdPrint(("%S: iSerialNumber= %x\n",DRV_NAME,pDescriptor->iSerialNumber));
			KdPrint(("%S: bNumConfigurations= %x\n",DRV_NAME,pDescriptor->bNumConfigurations));

			//moved to return
			
			if(pDescriptor->bDeviceClass == 0x08)
			{
				KdPrint(("%S: USB Mass Storage detected! - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
				RtlInitUnicodeString(&title, L"PayamPardaz USBlocker!");
				RtlInitUnicodeString(&text, L"USB Mass Storage device detected! Driver prevents using any kind of storage for security reasons.");
				param[0]= (ULONG_PTR) &text;
				param[1]= (ULONG_PTR) &title;
				param[2]= 0x40;
				ExRaiseHardError(STATUS_SERVICE_NOTIFICATION, 3, 3, param, 1, &response);
				Irp->IoStatus.Status = STATUS_ACCESS_DENIED;
				IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp);
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
				status = STATUS_ACCESS_DENIED;
			}
		}
		else
		{
			KdPrint(("%S: Insufficient nonpaged pool memory for device descriptor!\n",DRV_NAME));
			ExFreePool(pDescriptor);
			status = STATUS_INSUFFICIENT_RESOURCES;
		}

	}
	else
	{
		KdPrint(("%S: Insufficient nonpaged pool memory for URB!\n",DRV_NAME));
		ExFreePool(urb);
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	
	ExFreePool(pDescriptor);
	ExFreePool(urb);
	return status;
}


NTSTATUS USBCall(IN PDEVICE_OBJECT DeviceObject, IN PURB urb,IN PIRP Irp)
{
	KEVENT Event;
	PIRP irp;
	PIO_STACK_LOCATION nxtStack;
	PUSBlocker_DEVICE_EXTENSION dxe = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	NTSTATUS status;
	IO_STATUS_BLOCK  IoStatusBlock;
	ULONG IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
	
	status = IoAcquireRemoveLock(&dxe->RemoveLock,Irp);
	if (!NT_SUCCESS(status)) {
		CompleteRequest(Irp, status, 0);
	
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	KeInitializeEvent(&Event,NotificationEvent,FALSE);

	//Build internal IOCTL IRP
	irp = IoBuildDeviceIoControlRequest(
		IoControlCode, 
		dxe->lowerDeviceObject,   //pass IRP to lower-level device driver
		NULL, 0,    // Input buffer    
		NULL, 0,    // Output buffer    
		FALSE, &Event, &IoStatusBlock);   
	
	if(!irp)
	{
		KdPrint(("%S: Insufficient nonpaged pool memory for building DEVICE IO control!\n",DRV_NAME));
		status = STATUS_INSUFFICIENT_RESOURCES;
	}

	nxtStack = IoGetNextIrpStackLocation(irp); //get lower-level driver stack location
	ASSERT(nxtStack); //debug if false

	//set urb to the lower-level irp
	nxtStack->Parameters.Others.Argument1=urb;
	//set irp major function to internal_device_control
	nxtStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	
	//call lower-level driver and wait until device driver return
	status = IoCallDriver(dxe->lowerDeviceObject,irp);

	if(status == STATUS_PENDING)
	{
		KdPrint(("%S: KeWaitForSingleObject waiting for lower-level device driver to complete requests!\n",DRV_NAME));
		KeWaitForSingleObject(&Event,Executive,KernelMode,FALSE,NULL);
		if(status!=STATUS_SUCCESS) KdPrint(("%S: KeWaitForSingleObject failed!\n",DRV_NAME));
		status = IoStatusBlock.Status;
	}

	IoReleaseRemoveLock(&dxe->RemoveLock, Irp);

	return status;
	
}

NTSTATUS IOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	NTSTATUS status;
	PIO_STACK_LOCATION stack;
	wchar_t *req=NULL;
	PURB urb;
	KEVENT startDevice;
	PUSBlocker_DEVICE_EXTENSION dex = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

	stack = IoGetCurrentIrpStackLocation(Irp);
	DEBUG_MSG("Major=%u, Minor=%u, IoControlCode=0x%x", stack->MajorFunction, stack->MinorFunction, stack->Parameters.DeviceIoControl.IoControlCode);
	
	ULONG ioControlCode = stack->Parameters.DeviceIoControl.IoControlCode;
	
	status = IoAcquireRemoveLock(&dex->RemoveLock, Irp);
	if (!NT_SUCCESS(status)) {
		CompleteRequest(Irp, status, 0);
	
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	switch(ioControlCode)
	{
	case IOCTL_INTERNAL_USB_SUBMIT_URB: req = L"IOCTL_INTERNAL_USB_SUBMIT_URB"; break;
	case IOCTL_INTERNAL_USB_CYCLE_PORT: req = L"IOCTL_INTERNAL_USB_CYCLE_PORT"; break;
	case IOCTL_INTERNAL_USB_GET_BUS_INFO: req = L"IOCTL_INTERNAL_USB_GET_BUS_INFO"; break;
	case IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME: req = L"IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME"; break;
	//case IOCTL_INTERNAL_USB_GET_DEVICE_CONFIG_INFO: req = L"IOCTL_INTERNAL_USB_GET_DEVICE_CONFIG_INFO"; break;
	case IOCTL_INTERNAL_USB_GET_HUB_NAME: req = L"IOCTL_INTERNAL_USB_GET_HUB_NAME"; break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS: req = L"IOCTL_INTERNAL_USB_GET_PORT_STATUS"; break;
	case IOCTL_INTERNAL_USB_RESET_PORT: req = L"IOCTL_INTERNAL_USB_RESET_PORT"; break;
	case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION: req = L"IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION"; break;
	default: req=NULL; break; //default switch case 
	}

	if (req==L"IOCTL_INTERNAL_USB_SUBMIT_URB")
	{
		urb = (PURB)stack->Parameters.Others.Argument1;
		if(( urb->UrbControlDescriptorRequest.DescriptorType==USB_STRING_DESCRIPTOR_TYPE || urb->UrbControlDescriptorRequest.DescriptorType==USB_CONFIGURATION_DESCRIPTOR_TYPE || urb->UrbControlDescriptorRequest.DescriptorType==USB_DEVICE_DESCRIPTOR_TYPE) && (urb->UrbHeader.Function==URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE || urb->UrbHeader.Function==URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT))
		{
			_URB_CONTROL_DESCRIPTOR_REQUEST *pControlDescriptorRequest = (struct _URB_CONTROL_DESCRIPTOR_REQUEST *) urb;
			
			if (pControlDescriptorRequest->Hdr.Length < sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST))
			{
				KdPrint(("%S: IRP_MJ_INTERNAL_DEVICE_CONTROL - IOCTL_INTERNAL_USB_SUBMIT_URB - GetDescriptorFromDevice\n",DRV_NAME));
				KdPrint(("%S: GetDescriptorFromDevice - incorrect size of urb header = %d and should be at least %d\n",DRV_NAME,pControlDescriptorRequest->Hdr.Length,sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST)));
			}

			dumpBuffer(pControlDescriptorRequest->TransferBufferLength,pControlDescriptorRequest->TransferBuffer,pControlDescriptorRequest->TransferBufferMDL);
			//check for whether completion routine get completed or not
			KeInitializeEvent(&startDevice,NotificationEvent,FALSE);

			IoCopyCurrentIrpStackLocationToNext(Irp);

			IoSetCompletionRoutine(Irp,(PIO_COMPLETION_ROUTINE)inspectReturnedURB,PVOID(&startDevice),TRUE,TRUE,TRUE);
			status = IoCallDriver(dex->lowerDeviceObject,Irp);
			

			if(status==STATUS_PENDING)
			{
				KdPrint(("%S: waiting for lower-level driver to complete request.\n",DRV_NAME));
				//wait if status returned from lower-driver is pending	
				//wait more to event get completed
				KeWaitForSingleObject(&startDevice,Executive,KernelMode,FALSE,NULL);
				status=Irp->IoStatus.Status;
			}

			if(!NT_SUCCESS(status))
			{	
				KdPrint(("%S: Lower driver cannot process this IRP.\n",DRV_NAME));
				return status;
			}

			
		}		
	}//end if IOCTL_INTERNAL_USB_SUBMIT_URB
	else
	{
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(dex->lowerDeviceObject,Irp);
	}

	IoReleaseRemoveLock(&dex->RemoveLock, Irp);
	
	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;

}


NTSTATUS inspectReturnedURB(IN PDEVICE_OBJECT fdio, IN PIRP Irp, IN KEVENT Context)
{
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG ioControlCode = stack->Parameters.DeviceIoControl.IoControlCode;
	PURB urb;
	NTSTATUS status;

	if(Irp->PendingReturned) 
		IoMarkIrpPending(Irp);
	
	status = Irp->IoStatus.Status; //set by lower-level driver
	if (!NT_SUCCESS(status))
		// Well, the URB actually failed without we helping it, so
		// there should be no point in examining it since it might not be
		// properly initialized
		return STATUS_CONTINUE_COMPLETION;

	if(ioControlCode==IOCTL_INTERNAL_USB_SUBMIT_URB)
	{
		urb = (PURB)stack->Parameters.Others.Argument1;
		_URB_CONTROL_DESCRIPTOR_REQUEST *pControlDescriptorRequest = (_URB_CONTROL_DESCRIPTOR_REQUEST *) urb;
		PVOID transferBuf = pControlDescriptorRequest->TransferBuffer;
		PMDL mdlBuf = pControlDescriptorRequest->TransferBufferMDL;
		UCHAR descType = pControlDescriptorRequest->DescriptorType;

		switch(descType)
		{
		case USB_DEVICE_DESCRIPTOR_TYPE:
			PUSB_DEVICE_DESCRIPTOR pDescriptor;
			if(transferBuf)
			{
				if(mdlBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
				KdPrint(("%S: Descriptor buffer content is sending to USB hub controller = \n",DRV_NAME));
				//cast transferred buffer to USB_DEVICE_DESCRIPTOR type
				pDescriptor = (PUSB_DEVICE_DESCRIPTOR)transferBuf;
				
				//report all information from device descriptor
				KdPrint(("%S: USB_DEVICE_DESCRIPTOR...\n",DRV_NAME));
				KdPrint(("%S: Length of the descriptor data structure= %u (bytes)\n",DRV_NAME,pDescriptor->bLength));
				KdPrint(("%S: Descriptor type= %x\n",DRV_NAME,pDescriptor->bDescriptorType));
				KdPrint(("%S: Version of USB specification= %x\n",DRV_NAME,pDescriptor->bcdUSB));
				KdPrint(("%S: idProduct= %x\n",DRV_NAME,pDescriptor->idProduct));
				KdPrint(("%S: idVendor= %x\n",DRV_NAME,pDescriptor->idVendor));
				KdPrint(("%S: bMaxPacketSize0= %x\n",DRV_NAME,pDescriptor->bMaxPacketSize0));
				KdPrint(("%S: bDeviceSubClass= %x\n",DRV_NAME,pDescriptor->bDeviceSubClass));
				KdPrint(("%S: bDeviceClass= %x\n",DRV_NAME,pDescriptor->bDeviceClass));
				KdPrint(("%S: iProduct= %x\n",DRV_NAME,pDescriptor->iProduct));
				KdPrint(("%S: iSerialNumber= %x\n",DRV_NAME,pDescriptor->iSerialNumber));
				KdPrint(("%S: bNumConfigurations= %x\n",DRV_NAME,pDescriptor->bNumConfigurations));
				
				UCHAR classType = pDescriptor->bDeviceClass;

				switch(classType)
				{
				case massStorage:
					KdPrint(("%S: USB Mass Storage detected! - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
					status = STATUS_ACCESS_DENIED;
					break;

				default:
					KdPrint(("%S: not Mass Storage device - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
					break;
				}
			}
			else if(mdlBuf)
			{
				if(transferBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
				PVOID buf = MmGetSystemAddressForMdl(mdlBuf);
				if(!buf) 
				{
					KdPrint(("%S: Cannot allocate buffer for Mdl!\n",DRV_NAME));
					IoFreeMdl(mdlBuf);
				}
				pDescriptor = (PUSB_DEVICE_DESCRIPTOR)buf;

				//report all information from device descriptor
				KdPrint(("%S: USB_DEVICE_DESCRIPTOR...\n",DRV_NAME));
				KdPrint(("%S: Length of the descriptor data structure= %u (bytes)\n",DRV_NAME,pDescriptor->bLength));
				KdPrint(("%S: Descriptor type= %x\n",DRV_NAME,pDescriptor->bDescriptorType));
				KdPrint(("%S: Version of USB specification= %x\n",DRV_NAME,pDescriptor->bcdUSB));
				KdPrint(("%S: idProduct= %x\n",DRV_NAME,pDescriptor->idProduct));
				KdPrint(("%S: idVendor= %x\n",DRV_NAME,pDescriptor->idVendor));
				KdPrint(("%S: bMaxPacketSize0= %x\n",DRV_NAME,pDescriptor->bMaxPacketSize0));
				KdPrint(("%S: bDeviceSubClass= %x\n",DRV_NAME,pDescriptor->bDeviceSubClass));
				KdPrint(("%S: bDeviceClass= %x\n",DRV_NAME,pDescriptor->bDeviceClass));
				KdPrint(("%S: iProduct= %x\n",DRV_NAME,pDescriptor->iProduct));
				KdPrint(("%S: iSerialNumber= %x\n",DRV_NAME,pDescriptor->iSerialNumber));
				KdPrint(("%S: bNumConfigurations= %x\n",DRV_NAME,pDescriptor->bNumConfigurations));

				UCHAR classType = pDescriptor->bDeviceClass;

				switch(classType)
				{
				case massStorage:
					KdPrint(("%S: USB Mass Storage detected! - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
					status = STATUS_ACCESS_DENIED;
					break;

				default:
					KdPrint(("%S: not Mass Storage device - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
					break;
				}
			}//endif mdl buffer
			else {KdPrint(("%S: Cannot retrive any data from descriptor buffer!\n",DRV_NAME));}
		


		case USB_CONFIGURATION_DESCRIPTOR_TYPE:
			
			PUSB_CONFIGURATION_DESCRIPTOR configDesc;
			PUSB_INTERFACE_DESCRIPTOR interfacedesc;
			ULONG numberOfInterfaces;

			if(transferBuf)
			{
				if(mdlBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
				KdPrint(("%S: Configuration Descriptor buffer content is sending to USB hub controller = \n",DRV_NAME));
				//cast transferred buffer to USB_CONFIGURATION_DESCRIPTOR type
				configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)transferBuf;
				//interfacedesc = USBD_ParseConfigurationDescriptorEx(configDesc,configDesc,-1,-1,-1,-1,-1);

				//find number of interfaces available for this configuration
				numberOfInterfaces = configDesc->bNumInterfaces;

				for(int i=0;i<numberOfInterfaces;i++)
				{
					interfacedesc = USBD_ParseConfigurationDescriptorEx(configDesc,configDesc,i,-1,-1,-1,-1);
					if (interfacedesc)
					{
						UCHAR classType = interfacedesc->bInterfaceClass;

						switch(classType)
						{
						case massStorage:
							KdPrint(("%S: USB Mass Storage detected! - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
							status = STATUS_ACCESS_DENIED;
							break;

						default:
							KdPrint(("%S: not Mass Storage device - USB-IF class code = %x\n",DRV_NAME,pDescriptor->bDeviceClass));
							break;
						}

					}
				}
			}//end transfer buffer

			if(mdlBuf)
			{
				if(transferBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
				KdPrint(("%S: Configuration Descriptor buffer content is sending to USB hub controller = \n",DRV_NAME));
				//cast transferred buffer to USB_CONFIGURATION_DESCRIPTOR type
				PVOID buf = MmGetSystemAddressForMdl(mdlBuf);
				if(!buf) 
				{
					KdPrint(("%S: Cannot allocate buffer for Mdl!\n",DRV_NAME));
					IoFreeMdl(mdlBuf);
				}

				configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)buf;
				//interfacedesc = USBD_ParseConfigurationDescriptorEx(configDesc,configDesc,-1,-1,-1,-1,-1);

				//find number of interfaces available for this configuration
				numberOfInterfaces = configDesc->bNumInterfaces;

				for(int i=0;i<numberOfInterfaces;i++)
				{
					interfacedesc = USBD_ParseConfigurationDescriptorEx(configDesc,configDesc,i,-1,-1,-1,-1);
					if (interfacedesc)
					{
						UCHAR classType = interfacedesc->bInterfaceClass;

						switch(classType)
						{
						case massStorage:
							KdPrint(("%S: USB Mass Storage detected! - USB-IF class code = %x\n",DRV_NAME,interfacedesc->bInterfaceClass));
							status = STATUS_ACCESS_DENIED;
							break;

						default:
							KdPrint(("%S: not Mass Storage device - USB-IF class code = %x\n",DRV_NAME,interfacedesc->bInterfaceClass));
							break;
						}

					}
				}
			}//end Mdl buffer

		case USB_STRING_DESCRIPTOR_TYPE:
			PUSB_STRING_DESCRIPTOR strDesc;
			//PUNICODE_STRING stringDesc;
			//stringDesc->Buffer=NULL;
			//stringDesc->Length=0;
			//stringDesc->MaximumLength=0;

			if(transferBuf)
			{
				if(mdlBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
				KdPrint(("%S: String Descriptor buffer content is sending to USB hub controller = \n",DRV_NAME));
				//cast transferred buffer to USB_DEVICE_DESCRIPTOR type
				strDesc = (PUSB_STRING_DESCRIPTOR)transferBuf;
				ULONG size = (strDesc->bLength-2)/2;
				PWSTR str = (PWSTR)ExAllocatePool(PagedPool,strDesc->bLength);
				if (!str) return STATUS_INSUFFICIENT_RESOURCES;
				memcpy(str,strDesc->bString,size*2 + sizeof(WCHAR));
				//RtlCopyUnicodeString(str,strDesc->Buffer);
				str[size]=0;
				
				//stringDesc->Length = (USHORT)size*2;
				//stringDesc->MaximumLength = (USHORT)((size*2) + sizeof(WCHAR));
				//stringDesc->Buffer = str;

				//report all information from device descriptor
				KdPrint(("%S: USB_STRING_DESCRIPTOR...\n",DRV_NAME));
				KdPrint(("%S: Length of the string descriptor structure= %u (bytes)\n",DRV_NAME,size));
				KdPrint(("%S: String descriptor = %s\n",DRV_NAME,&str));

				break;

			}//end if tbuffer

			if(mdlBuf)
			{
				if(transferBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
				KdPrint(("%S: String Descriptor buffer content is sending to USB hub controller = \n",DRV_NAME));
				//cast transferred buffer to USB_CONFIGURATION_DESCRIPTOR type
				PVOID buf = MmGetSystemAddressForMdl(mdlBuf);
				if(!buf) 
				{
					KdPrint(("%S: Cannot allocate buffer for Mdl!\n",DRV_NAME));
					IoFreeMdl(mdlBuf);
				}

				strDesc = (PUSB_STRING_DESCRIPTOR)buf;
				
				ULONG size = (strDesc->bLength-2)/2;
				PWSTR str = (PWSTR)ExAllocatePool(PagedPool,strDesc->bLength);
				if (str) {
					memcpy(str, (PWSTR)strDesc->bString, size * 2 + sizeof(WCHAR));
					//RtlCopyUnicodeString(str,strDesc->Buffer);
					str[size] = 0;

					//stringDesc->Length = (USHORT)size*2;
					//stringDesc->MaximumLength = (USHORT)((size*2) + sizeof(WCHAR));
					//stringDesc->Buffer = str;

					//report all information from device descriptor
					KdPrint(("%S: USB_STRING_DESCRIPTOR...\n", DRV_NAME));
					KdPrint(("%S: Length of the string descriptor structure= %u (bytes)\n", DRV_NAME, size));
					KdPrint(("%S: String descriptor = %s\n", DRV_NAME, &str));
				} else status = STATUS_INSUFFICIENT_RESOURCES;
				
				break;

			}//end Mdl buffer
		}
	}

	if (!NT_SUCCESS(status))
		Irp->IoStatus.Status = status;

	return STATUS_CONTINUE_COMPLETION;
}


NTSTATUS USBlockerPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	PUSBlocker_DEVICE_EXTENSION pdx = (PUSBlocker_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PAGED_CODE();
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

	status = IoAcquireRemoveLock(&pdx->RemoveLock,Irp);
	if (!NT_SUCCESS(status)) {
		CompleteRequest(Irp, status, 0);
	
		DEBUG_EXIT_FUNCTION("0x%x", status);
		return status;
	}

	switch (irpSp->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		status = USBlockerForwardIrpSynchronous(DeviceObject, Irp);
		if (NT_SUCCESS(status))
			status = IoSetDeviceInterfaceState(&pdx->DeviceInterface, TRUE);

		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp,IO_NO_INCREMENT); //should be called from dispatcher routine not from completion routine
		IoReleaseRemoveLock(&pdx->RemoveLock,Irp);
		break;
	case IRP_MN_REMOVE_DEVICE:
		IoSetDeviceInterfaceState(&pdx->DeviceInterface,FALSE);
		IoReleaseRemoveLockAndWait(&pdx->RemoveLock, Irp);
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(pdx->lowerDeviceObject,Irp);
		IoDetachDevice(pdx->lowerDeviceObject);
		IoDeleteDevice(pdx->DeviceObject);
		break;
	default:
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(pdx->lowerDeviceObject,Irp);
		IoReleaseRemoveLock(&pdx->RemoveLock,Irp);
		break;
	}

	DEBUG_EXIT_FUNCTION("0x%x", status);
	return status;
}


void dumpBuffer(IN ULONG bufSize,IN PVOID pBuffer, IN PMDL pMdl)
{
	PVOID transferBuf = pBuffer;
	PMDL mdlBuf = pMdl;

	if(transferBuf)
	{
		if(mdlBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
		KdPrint(("%S: Descriptor buffer content is sending to USB hub controller = \n",DRV_NAME));
		PUCHAR buf = (PUCHAR)transferBuf;
		for(int i=0;i<sizeof(buf);i++) KdPrint(("%S: %02X",DRV_NAME,buf[i]));
		KdPrint(("\n"));
	}
	else if(mdlBuf)
	{
		if(transferBuf) KdPrint(("%S: Unsupported descriptor request since both mdl and tbuffer is initialized.\n",DRV_NAME));
		PUCHAR buf = (PUCHAR)MmGetSystemAddressForMdl(mdlBuf);
		if(!buf) 
		{
			KdPrint(("%S: Cannot allocate buffer for Mdl!\n",DRV_NAME));
			IoFreeMdl(mdlBuf);
		}
		for(int i=0;i<sizeof(buf);i++) KdPrint(("%S: %02X",DRV_NAME,buf[i]));
		KdPrint(("\n"));
	}
	else {KdPrint(("%S: Cannot reterive any data from descriptor buffer!\n",DRV_NAME));}
}
