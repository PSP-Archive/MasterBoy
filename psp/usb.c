#include "pspcommon.h"

//helper function to make things easier
int oslLoadStartModule(char *path)
{
    u32 loadResult;
    u32 startResult;
    int status;

    loadResult = sceKernelLoadModule(path, 0, NULL);
    if (loadResult & 0x80000000)
	return -1;
    else
	startResult =
	    sceKernelStartModule(loadResult, 0, NULL, &status, NULL);

    if (loadResult != startResult)
	return -2;

    return 0;
}

int oslInitUsbStorage()		{
	int retVal;
    //Lance les drivers USB
    oslLoadStartModule("flash0:/kd/semawm.prx");
    oslLoadStartModule("flash0:/kd/usbstor.prx");
    oslLoadStartModule("flash0:/kd/usbstormgr.prx");
    oslLoadStartModule("flash0:/kd/usbstorms.prx");
    oslLoadStartModule("flash0:/kd/usbstorboot.prx");

    //Paramètre les drivers USB
    retVal = sceUsbStart("USBBusDriver", 0, 0);
    if (retVal != 0)
		return -1;
    retVal = sceUsbStart("USBStor_Driver", 0, 0);
    if (retVal != 0)
		return -2;
    retVal = sceUsbstorBootSetCapacity(0x800000);
    if (retVal != 0)
		return -3;
	return 0;
}

void oslStartUsbStorage()		{
	sceUsbActivate(0x1c8);
}

void oslStopUsbStorage()		{
	sceUsbDeactivate(0x1c8);
}

int oslDeinitUsbStorage()			{
	unsigned long state;
	int retVal;
	
	state = oslGetUsbState();
    //cleanup drivers
    if (state & PSP_USB_ACTIVATED) {
		retVal = sceUsbDeactivate(0x1c8);
		if (retVal != 0)
			return -1;
    }
    retVal = sceUsbStop(PSP_USBSTOR_DRIVERNAME, 0, 0);
    if (retVal != 0)
		return -2;
    retVal = sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
    if (retVal != 0)
		return -3;
	return 0;
}


