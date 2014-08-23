/*
 * Copyright 2014 Stefano Ceccherini <stefano.ceccherini@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
 
#ifndef RALINK_H
#define RALINK_H

#include <KernelExport.h>
#include <USB3.h>
#include <SupportDefs.h>

#include "ether_driver.h"


class RalinkUSB {
public:
						RalinkUSB(usb_device device);
						~RalinkUSB();
						
	status_t			InitCheck() const;

	int32				DeviceID() { return fDeviceID; };

	status_t			Open(uint32 flags);
	bool				IsOpen() { return fOpen; };
	
	status_t			Close();
	status_t			Free();

	status_t			Read(off_t position, void* buffer, size_t*numBytes);
	status_t			Write(off_t position, const void* buffer,
								size_t* numBytes);
	status_t			Control(uint32 op, void* args, size_t length);

	void				Removed();
	bool				IsRemoved() { return fRemoved; };
	
	status_t			CompareAndReattach(usb_device device);
	status_t			SetupDevice(bool deviceReplugged);
	
private:
	int32				fDeviceID;
	usb_device			fDevice;
	ether_address_t		fMACAddress;
	
	status_t			fStatus;
	
	bool				fOpen;
	bool				fRemoved;
	bool				fNonBlocking;
	
	bool				fEFuse;
	
	// pipes for notifications, data io and tx packet size
	usb_pipe			fNotifyEndpoint;
	usb_pipe			fReadEndpoint;
	usb_pipe			fWriteEndpoint;
	uint16				fMaxTXPacketSize;
	
	uint16				fMACVersion;
	uint16				fMACRevision;
	
	uint8				fLeds;
	uint16				fLed[3];
	
	uint16				fRFRevision;
	
	uint8				fTXChainsCount;
	uint8				fRXChainsCount;
	
	bool				fPatchDAC;
	bool				fExt5GhzLNA;
	bool				fExt2GhzLNA;
	bool				fCalib5Ghz;
	bool				fCalib2Ghz;
	bool				fRFSwitch;
	
	int8				fTxPow1[16];
	int8				fTxPow2[16];
	
	status_t			_StartDevice();
	status_t			_SetupEndpoints();
	status_t			_Reset();
	status_t			_LoadMicrocode();
	status_t			_EtherInit();
	
	status_t 			_Write(uint16 reg, uint32 val);
	status_t 			_Write2(uint16 reg, uint16 val);
	status_t			_WriteRegion(uint16 reg, const uint8* buffer, uint16 len);
	
	status_t			_Read(uint16 reg, uint32* val);
	status_t			_ReadRegion(uint16 reg, uint8* buffer, uint16 len);
	status_t			_ReadMACAddress(ether_address_t *address);
	status_t			_ReadEEPROM(uint16 addr, uint16* val);
	status_t			_ReadEFUSE(uint16 addr, uint16* val);
	
	status_t			_SendMCUCommand(uint8 command, uint16 arg);
	
	void				_Delay(int n);

};

#endif // _H
