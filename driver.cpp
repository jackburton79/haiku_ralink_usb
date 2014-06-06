#include <Drivers.h>
#include <KernelExport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include <lock.h>
//#include <util/AutoLock.h>

#include "driver.h"
#include "ralink_usb.h"
#include "kernel_cpp.h"


status_t	ralink_open(const char *name, uint32 flags, void **cookie);
status_t	ralink_close(void *cookie);
status_t	ralink_free(void *cookie);
status_t	ralink_control(void *cookie, uint32 op, void *args, size_t length);
status_t	ralink_read(void *cookie, off_t position, void *buffer, size_t *numBytes);
status_t	ralink_write(void *cookie, off_t position, const void *buffer, size_t *numBytes);
status_t	usb_ralink_device_added(usb_device device, void **cookie);
status_t	usb_ralink_device_removed(void *cookie);

int32 api_version = B_CUR_DRIVER_API_VERSION;


usb_module_info* gUSBModule;
char* gDeviceNames[MAX_DEVICES + 1];
RalinkUSB* gDevicesList[MAX_DEVICES];
int32 gOpenMask = 0;
//mutex gDriverLock;
static usb_support_descriptor sDescriptor;
static const char* sDeviceBaseName = "net/usb_ralink/";
	
device_hooks gDeviceHooks = {
	ralink_open,
	ralink_close,
	ralink_free,
	ralink_control,
	ralink_read,
	ralink_write
};


static usb_notify_hooks sNotifyHooks = {
	&usb_ralink_device_added,
	&usb_ralink_device_removed
};
		
	
RalinkUSB*
lookup_and_create_device(usb_device device)
{
	const usb_device_descriptor *deviceDescriptor
		= gUSBModule->get_device_descriptor(device);

	if (deviceDescriptor == NULL) {
		TRACE_ALWAYS("Error of getting USB device descriptor.\n");
		return NULL;
	}

	TRACE("trying %#06x:%#06x.\n",
			deviceDescriptor->vendor_id, deviceDescriptor->product_id);

	return new RalinkUSB(device);
}


status_t
usb_ralink_device_added(usb_device device, void **cookie)
{
	*cookie = NULL;

	//MutexLocker lock(gDriverLock); // released on exit

	// check if this is a replug of an existing device first
	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (gDevicesList[i] == NULL)
			continue;

		if (gDevicesList[i]->CompareAndReattach(device) != B_OK)
			continue;

		TRACE("The device is plugged back. Use entry at %ld.\n", i);
		*cookie = gDevicesList[i];
		return B_OK;
	}

	// no such device yet, create a new one
	RalinkUSB* ralinkDevice = lookup_and_create_device(device);
	if (ralinkDevice == NULL)
		return ENODEV;

	status_t status = ralinkDevice->InitCheck();
	if (status < B_OK) {
		delete ralinkDevice;
		return status;
	}

	status = ralinkDevice->SetupDevice(false);
	if (status < B_OK) {
		delete ralinkDevice;
		return status;
	}

	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (gDevicesList[i] != NULL)
			continue;

		gDevicesList[i] = ralinkDevice;
		gDeviceNames[i] = (char*)malloc(B_PATH_NAME_LENGTH);
		snprintf(gDeviceNames[i], B_PATH_NAME_LENGTH,
			"%s%" B_PRId32, sDeviceBaseName, i);
		*cookie = ralinkDevice;

		TRACE("New device is added at %ld.\n", i);
		return B_OK;
	}

	// no space for the device
	TRACE_ALWAYS("Error: no more device entries available.\n");

	delete ralinkDevice;
	return B_ERROR;
}


status_t
usb_ralink_device_removed(void *cookie)
{
	//MutexLocker lock(gDriverLock); // released on exit

	RalinkUSB* device = (RalinkUSB*)cookie;
	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (gDevicesList[i] == device) {
			if (device->IsOpen()) {
				// the device will be deleted upon being freed
				device->Removed();
			} else {
				gDevicesList[i] = NULL;
				delete device;
				TRACE("Device at %ld deleted.\n", i);
			}
			break;
		}
	}

	return B_OK;
}


status_t
init_hardware()
{
#ifdef TRACE_RALINK
	set_dprintf_enabled(true);
#endif

	return B_OK;
}


status_t
init_driver()
{
	TRACE((DRIVER_NAME": init driver\n"));

	status_t status = get_module(B_USB_MODULE_NAME,
		(module_info**)&gUSBModule);
	if (status < B_OK)
		return status;

	for (int32 i = 0; i < MAX_DEVICES; i++)
		gDevicesList[i] = NULL;
	for (int32 i = 0; i < MAX_DEVICES + 1; i++)
		gDeviceNames[i] = NULL;
	
	//mutex_init(&gDriverLock, DRIVER_NAME"_devices");
	
	sDescriptor.vendor = VENDOR_ID_RALINK;
	sDescriptor.product = 0x3070;
	gUSBModule->register_driver(DRIVER_NAME, &sDescriptor, 1, NULL);
	gUSBModule->install_notify(DRIVER_NAME, &sNotifyHooks);
	
	return B_OK;
}


void
uninit_driver()
{
	TRACE((DRIVER_NAME": uninit driver\n"));
	
	gUSBModule->uninstall_notify(DRIVER_NAME);
	//mutex_lock(&gDriverLock);

	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (gDevicesList[i]) {
			delete gDevicesList[i];
			gDevicesList[i] = NULL;
		}
	}

	for (int32 i = 0; gDeviceNames[i]; i++) {
		free(gDeviceNames[i]);
		gDeviceNames[i] = NULL;
	}

	//mutex_destroy(&gDriverLock);
	put_module(B_USB_MODULE_NAME);

	//release_settings();
}


const char **
publish_devices(void)
{
	return (const char **)gDeviceNames;
}


device_hooks*
find_device(const char *name)
{
	TRACE(DRIVER_NAME": find device \"%s\"\n", name);

	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (strcmp(gDeviceNames[i], name) == 0)
			return &gDeviceHooks;
	}

	TRACE_ALWAYS((DRIVER_NAME": couldn't find device \"%s\"\n", name));
	return NULL;
}


//#pragma mark -


status_t
ralink_open(const char *name, uint32 flags, void **cookie)
{
	TRACE(DRIVER_NAME": open device %s\n", name);
	//MutexLocker lock(gDriverLock); // released on exit

	*cookie = NULL;
	status_t status = ENODEV;
	int32 index = strtol(name + strlen(sDeviceBaseName), NULL, 10);
	if (index >= 0 && index < MAX_DEVICES && gDevicesList[index]) {
		status = gDevicesList[index]->Open(flags);
		*cookie = gDevicesList[index];
	}

	return status;
}


status_t
ralink_close(void *cookie)
{
	//TRACE((DRIVER_NAME": close device\n"));
	RalinkUSB *device = (RalinkUSB *)cookie;
	return device->Close();
}


status_t
ralink_free(void *cookie)
{
	//TRACE((DRIVER_NAME": free device\n"));
	RalinkUSB *device = (RalinkUSB *)cookie;

	//MutexLocker lock(gDriverLock); // released on exit

	status_t status = device->Free();
	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (gDevicesList[i] == device) {
			// the device is removed already but as it was open the
			// removed hook has not deleted the object
			gDevicesList[i] = NULL;
			delete device;
			free(gDeviceNames[i]);
			gDeviceNames[i] = NULL;
			TRACE("Device at %ld deleted.\n", i);
			break;
		}
	}
	return status;
}


status_t
ralink_control(void *cookie, uint32 op, void *args, size_t length)
{
	TRACE(DRIVER_NAME": control device\n");
	RalinkUSB *device = (RalinkUSB*)cookie;
	return device->Control(op, args, length);
}


status_t
ralink_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	TRACE(DRIVER_NAME": read device\n");
	RalinkUSB *device = (RalinkUSB*)cookie;
	return device->Read(position, buffer, numBytes);
}


status_t
ralink_write(void *cookie, off_t position, const void *buffer, size_t *numBytes)
{
	TRACE(DRIVER_NAME": write device\n");
	RalinkUSB* device = (RalinkUSB*)cookie;
	return device->Write(position, buffer, numBytes);
}
