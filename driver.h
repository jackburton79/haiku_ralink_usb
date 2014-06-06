#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <SupportDefs.h>
#include <KernelExport.h>
#include <USB3.h>

extern usb_module_info* gUSBModule;

#define	DRIVER_NAME			"ralink_usb"
#define	DRIVER_VERSION		1.0.0
#define	DRIVER_DESCRIPTION	"Ralink(R) USB Wireless 2870/3070 Driver"

#define MAX_DEVICES		3
#define VENDOR_ID_RALINK	0x148f

#define TRACE_RALINK
#ifdef TRACE_RALINK
#define TRACE			dprintf
#define TRACE_ALWAYS	dprintf
#else
#define TRACE ;				/* nothing */
#define TRACE_ALWAYS		dprintf
#endif

#endif // _DRIVER_H_
