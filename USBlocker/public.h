
#ifndef __PUBLIC_H__
#define __PUBLIC_H__

#define IOCTL_USBLOCKER_ENABLE    \
	CTL_CODE(FILE_DEVICE_UNKNOWN,        \
	0x800,                      \
	METHOD_OUT_DIRECT,           \
	FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_USBLOCKER_DISABLE   \ 
CTL_CODE(FILE_DEVICE_UNKNOWN,        \
		 0x801,                      \
		 METHOD_OUT_DIRECT,          \
		 FILE_READ_DATA | FILE_WRITE_DATA) 


#endif
