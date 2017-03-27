//#define DEV_NAME L"\\Device\\USBFilterTest"
//#define GLOB_NAME L"\\DosDevices\\USBlocker"
//#define DRV_NAME "DBGUSBlock"
//extern const string DRV_NAME;// = L"DBGUSBlock"
#define DRV_TAG 'USBLock'
//extern const string DRV_TAG;


//usb device class type
#define massStorage 0x08
#define Audio 0x01
#define Image 0x06
#define Printer 0x07
#define Video 0x0e
#define Hub 0x09
#define smartCard 0x0b

const WCHAR DEV_NAME[] = L"\\Device\\MassBlocker";
const WCHAR GLOB_NAME[] = L"\\DosDevices\\USBlocker";
const WCHAR DRV_NAME[] = L"DBGUSBlock";

extern "C" NTSTATUS NTAPI ExRaiseHardError(IN NTSTATUS ErrorStatus, IN ULONG NumberOfParameters, IN ULONG UnicodeStringParameterMask,
								IN PULONG_PTR Parameters,IN ULONG ValidResponseOptions, OUT PULONG Response);

extern "C" PULONG InitSafeBootMode; //determine is in safeMode

typedef struct _DEVICE_EXTENSION
{
	PDEVICE_OBJECT DeviceObject; // Filter DO
	PDEVICE_OBJECT lowerDeviceObject; //FDO
	PDEVICE_OBJECT PhysicalDeviceObject; //PDO
	UNICODE_STRING DeviceInterface; //we don't register interface because this is not used to be a PnP device driver
	//UNICODE_STRING LinkName; //symbolic name
	IO_REMOVE_LOCK RemoveLock; //remove lock protection ; ensure device object
	//won't delete during IRP processing unless otherwise removeLock has been released
} USBlocker_DEVICE_EXTENSION, *PUSBlocker_DEVICE_EXTENSION;