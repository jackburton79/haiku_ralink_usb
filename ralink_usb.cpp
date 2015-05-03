/*
 * Copyright 2014 Stefano Ceccherini <stefano.ceccherini@gmail.com>
 */
 /*-
 * Copyright (c) 2008,2010 Damien Bergamini <damien.bergamini@free.fr>
 * ported to FreeBSD by Akinori Furukoshi <moonlightakkiy@yahoo.ca>
 * USB Consulting, Hans Petter Selasky <hselasky@freebsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "driver.h"
#include "if_runreg.h"
#include "ralink_usb.h"

#include <ByteOrder.h>

#include <net/if_media.h>
#include <stdlib.h>
#include <string.h>

RalinkUSB::RalinkUSB(usb_device device)
	:
	fDevice(device),
	fDeviceID(0),
	fStatus(B_ERROR),
	fOpen(false),
	fRemoved(false),
	fNonBlocking(false),
	fEFuse(false),
	fNotifyEndpoint(0),
	fReadEndpoint(0),
	fWriteEndpoint(0)
{
	memset(&fMACAddress, 0, sizeof(fMACAddress));
	
	if (_SetupEndpoints() != B_OK) {
		return;
	}
	
	fStatus = B_OK;
}


RalinkUSB::~RalinkUSB()
{
	/*if (fNotifyReadSem >= B_OK)
		delete_sem(fNotifyReadSem);
	if (fNotifyWriteSem >= B_OK)
		delete_sem(fNotifyWriteSem);

	if (!fRemoved) // ???
		gUSBModule->cancel_queued_transfers(fNotifyEndpoint);

	delete fNotifyData;*/
	TRACE("Deleted!\n");
}


status_t
RalinkUSB::InitCheck() const
{
	return fStatus;
}


status_t
RalinkUSB::Open(uint32 flags)
{
	TRACE("RalinkUSB::Open()\n");
	
	if (fOpen)
		return B_BUSY;
	if (fRemoved)
		return B_ERROR;

	//_Reset();
	
	status_t result = _StartDevice();
	if (result != B_OK) {
		return result;
	}

	fNonBlocking = (flags & O_NONBLOCK) == O_NONBLOCK;
	fOpen = true;
	TRACE("Opened: %#010x!\n", result);
	return result;
}
	

status_t
RalinkUSB::Close()
{
	TRACE("usb_ralink: Close()\n");
	if (fRemoved) {
		fOpen = false;
		return B_OK;
	}

	//_EnableInterrupts(false);

	// wait until possible notification handling finished...
	//while (atomic_add(&fInsideNotify, 0) != 0)
	//	snooze(100);
	//gUSBModule->cancel_queued_transfers(fNotifyEndpoint);
	gUSBModule->cancel_queued_transfers(fReadEndpoint);
	gUSBModule->cancel_queued_transfers(fWriteEndpoint);

	fOpen = false;

	status_t result = B_OK;//_StopDevice();
	TRACE(DRIVER_NAME": Closed: %#010x!\n", result);
	return result;
}

	
status_t
RalinkUSB::Free()
{
	TRACE(DRIVER_NAME": Free()\n");
	return B_OK;
}
	

status_t
RalinkUSB::Read(off_t position, void* buffer, size_t*numBytes)
{
	TRACE(DRIVER_NAME": Read()\n");
	return B_ERROR;
}
	

status_t
RalinkUSB::Write(off_t position, const void* buffer, size_t* numBytes)
{
	TRACE(DRIVER_NAME": Write()\n");
	return B_ERROR;
}
	

status_t
RalinkUSB::Control(uint32 op, void* buffer, size_t length)
{
	TRACE(DRIVER_NAME": Control()\n");
	if (fStatus < B_OK)
		return fStatus;
		
	switch (op) {
		case ETHER_INIT: {
			TRACE(DRIVER_NAME": ETHER_INIT\n");
			return _EtherInit();
		}

		case ETHER_GETADDR: {
			TRACE(DRIVER_NAME": ETHER_GETADDR\n");
			memcpy(buffer, &fMACAddress, sizeof(fMACAddress));
			return B_OK;
		}

		case ETHER_GETFRAMESIZE: {
			TRACE(DRIVER_NAME": ETHER_GETFRAMESIZE\n");
			*(uint32 *)buffer = 1500;
			return B_OK;
		}
		
		case ETHER_GET_LINK_STATE: {
			// TODO: For now, should avoid dhcp requests
			memset(buffer, 0, sizeof(ether_link_state));
			return B_OK;
		}
		default:
			TRACE_ALWAYS(DRIVER_NAME": unsupported ioctl 0x%08lx\n", op);
	}

	return B_DEV_INVALID_IOCTL;
}	


void
RalinkUSB::Removed()
{
	fRemoved = true;
	//fHasConnection = false;

	// the notify hook is different from the read and write hooks as it does
	// itself schedule traffic (while the other hooks only release a semaphore
	// to notify another thread which in turn safly checks for the removed
	// case) - so we must ensure that we are not inside the notify hook anymore
	// before returning, as we would otherwise violate the promise not to use
	// any of the pipes after returning from the removed hook
	/*while (atomic_add(&fInsideNotify, 0) != 0)
		snooze(100);

	gUSBModule->cancel_queued_transfers(fNotifyEndpoint);*/
	gUSBModule->cancel_queued_transfers(fReadEndpoint);
	gUSBModule->cancel_queued_transfers(fWriteEndpoint);

	/*if (fLinkStateChangeSem >= B_OK)
		release_sem_etc(fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);*/
}


status_t
RalinkUSB::SetupDevice(bool deviceReplugged)
{
	status_t status = _LoadMicrocode();
	if (status < B_OK)
		return status;
		
	uint32 ver;
	int ntries;
	
	// RUN_LOCK(sc)
	/* wait for the chip to settle */
	for (ntries = 0; ntries < 100; ntries++) {
		if (_Read(RT2860_ASIC_VER_ID, &ver) != B_OK) {
			//RUN_UNLOCK(sc);
			return B_ERROR;
		}
		if (ver != 0 && ver != 0xffffffff)
			break;
		_Delay(10);
	}
	if (ntries == 100) {
		TRACE(DRIVER_NAME": timeout waiting for NIC to initialize\n");
		//RUN_UNLOCK(sc);
		return B_ERROR;
	}
	fMACVersion = ver >> 16;
	fMACRevision = ver & 0xffff;

	TRACE(DRIVER_NAME": mac_version: 0x%x, revision: %d\n", fMACVersion, fMACRevision);
	if (fMACVersion >= 0x3070) {
		uint32 tmp;
		_Read(RT3070_EFUSE_CTRL, &tmp);
		TRACE(DRIVER_NAME"_EFUSE_CTRL=0x%08x\n", tmp);
		if (tmp & RT3070_SEL_EFUSE)
			fEFuse = true;
	}
	
	uint16 value;
	if (_ReadEEPROM(RT2860_EEPROM_VERSION, &value) == B_OK)
		TRACE_ALWAYS(DRIVER_NAME": EEPROM rev=%d, FAE=%d\n", value & 0xff, value >> 8);
	
	//device_printf(sc->sc_dev,
	  //  "MAC/BBP RT%04X (rev 0x%04X), RF %s (MIMO %dT%dR), address %s\n",
	  //  sc->mac_ver, sc->mac_rev, run_get_rf(sc->rf_rev),
	  //  sc->ntxchains, sc->nrxchains, ether_sprintf(sc->sc_bssid));
	    
	ether_address address;
	status_t result = _ReadMACAddress(&address);
	if (result != B_OK) {
		TRACE_ALWAYS(DRIVER_NAME": Error reading MAC address:%#010x\n", result);
		return result;
	}

	TRACE(DRIVER_NAME": MAC address is:%02x:%02x:%02x:%02x:%02x:%02x\n",
				address.ebyte[0], address.ebyte[1], address.ebyte[2],
				address.ebyte[3], address.ebyte[4], address.ebyte[5]);

	if (deviceReplugged) {
		// this might be the same device that was replugged - read the MAC
		// address (which should be at the same index) to make sure
		if (memcmp(&address, &fMACAddress, sizeof(address)) != 0) {
			TRACE_ALWAYS("Cannot replace device with MAC address:"
				"%02x:%02x:%02x:%02x:%02x:%02x\n",
				fMACAddress.ebyte[0], fMACAddress.ebyte[1],
				fMACAddress.ebyte[2], fMACAddress.ebyte[3],
				fMACAddress.ebyte[4], fMACAddress.ebyte[5]);
			return B_BAD_VALUE; // is not the same
		}
	} else
		fMACAddress = address;
	
	/* read vender BBP settings */
	for (int i = 0; i < 10; i++) {
		_ReadEEPROM(RT2860_EEPROM_BBP_BASE + i, &value);
		uint8 bppVal = value & 0xff;
		uint8 bppReg = value >> 8;
		/*sc->bbp[i].val = 
		sc->bbp[i].reg = */
		TRACE(DRIVER_NAME": BBP%d=0x%02x\n", bppReg, bppVal);
	}

	if (fMACVersion >= 0x3071) {
		/* read vendor RF settings */
		for (int i = 0; i < 10; i++) {
			_ReadEEPROM(RT3071_EEPROM_RF_BASE + i, &value);
			/*sc->rf[i].val = val & 0xff;
			sc->rf[i].reg = val >> 8;*/
			uint8 rfVal = value & 0xff;
			uint8 rfReg = value >> 8;
			TRACE(DRIVER_NAME": RF%d=0x%02x\n", rfReg, rfVal);
		}
	}

	/* read RF frequency offset from EEPROM */
	_ReadEEPROM(RT2860_EEPROM_FREQ_LEDS, &value);
	uint8 freq = ((value & 0xff) != 0xff) ? value & 0xff : 0;
	TRACE(DRIVER_NAME": EEPROM freq offset %d\n", freq & 0xff);

	if (value >> 8 != 0xff) {
		/* read LEDs operating mode */
		fLeds = value >> 8;
		_ReadEEPROM(RT2860_EEPROM_LED1, &fLed[0]);
		_ReadEEPROM(RT2860_EEPROM_LED2, &fLed[1]);
		_ReadEEPROM(RT2860_EEPROM_LED3, &fLed[2]);
	} else {
		/* broken EEPROM, use default settings */
		fLeds = 0x01;
		fLed[0] = 0x5555;
		fLed[1] = 0x2221;
		fLed[2] = 0x5627;	/* differs from RT2860 */
	}
	TRACE(DRIVER_NAME": EEPROM LED mode=0x%02x, LEDs=0x%04x/0x%04x/0x%04x\n",
	    fLeds, fLed[0], fLed[1], fLed[2]);

	/* read RF information */
	_ReadEEPROM(RT2860_EEPROM_ANTENNA, &value);
	if (value == 0xffff) {
		TRACE("invalid EEPROM antenna info, using default\n");
		if (fMACRevision == 0x3572) {
			/* default to RF3052 2T2R */
			fRFRevision = RT3070_RF_3052;
			fTXChainsCount = 2;
			fRXChainsCount = 2;
		} else if (fMACRevision >= 0x3070) {
			/* default to RF3020 1T1R */
			fRFRevision = RT3070_RF_3020;
			fTXChainsCount = 1;
			fRXChainsCount = 1;
		} else {
			/* default to RF2820 1T2R */
			fRFRevision = RT2860_RF_2820;
			fTXChainsCount = 1;
			fRXChainsCount = 2;
		}
	} else {
		fRFRevision = (value >> 8) & 0xf;
		fTXChainsCount = (value >> 4) & 0xf;
		fRXChainsCount = value & 0xf;
	}
	TRACE("EEPROM RF rev=0x%02x chains=%dT%dR\n",
	    fRFRevision, fTXChainsCount, fRXChainsCount);

	/* check if RF supports automatic Tx access gain control */
	_ReadEEPROM(RT2860_EEPROM_CONFIG, &value);
	TRACE(DRIVER_NAME": EEPROM CFG 0x%04x\n", value);
	/* check if driver should patch the DAC issue */
	if ((value >> 8) != 0xff)
		fPatchDAC = ((value >> 15) & 1) ? true : false;
	if ((value & 0xff) != 0xff) {
		fExt5GhzLNA = ((value >> 3) & 1) ? true : false;
		fExt2GhzLNA = ((value >> 2) & 1) ? true : false;
		/* check if RF supports automatic Tx access gain control */
		fCalib2Ghz = fCalib5Ghz = (value >> 1) & 1 ? true : false;
		/* check if we have a hardware radio switch */
		fRFSwitch = value & 1 ? true : false;
	}

	/* read power settings for 2GHz channels */
	for (int i = 0; i < 14; i += 2) {
		_ReadEEPROM(RT2860_EEPROM_PWR2GHZ_BASE1 + i / 2, &value);
		fTxPow1[i + 0] = (int8)(value & 0xff);
		fTxPow1[i + 1] = (int8)(value >> 8);

		_ReadEEPROM(RT2860_EEPROM_PWR2GHZ_BASE2 + i / 2, &value);
		fTxPow2[i + 0] = (int8)(value & 0xff);
		fTxPow2[i + 1] = (int8)(value >> 8);
	}

	/* fix broken Tx power entries */
	for (int i = 0; i < 14; i++) {
		if (fTxPow1[i] < 0 || fTxPow1[i] > 31)
			fTxPow1[i] = 5;
		if (fTxPow2[i] < 0 || fTxPow2[i] > 31)
			fTxPow2[i] = 5;
		TRACE("chan %d: power1=%d, power2=%d\n",
		    /*rt2860_rf2850[i].chan*/i, fTxPow1[i], fTxPow2[i]);
	}
	
#if 0
	/* read power settings for 5GHz channels */
	for (i = 0; i < 40; i += 2) {
		run_srom_read(sc, RT2860_EEPROM_PWR5GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 14] = (int8_t)(val & 0xff);
		sc->txpow1[i + 15] = (int8_t)(val >> 8);

		run_srom_read(sc, RT2860_EEPROM_PWR5GHZ_BASE2 + i / 2, &val);
		sc->txpow2[i + 14] = (int8_t)(val & 0xff);
		sc->txpow2[i + 15] = (int8_t)(val >> 8);
	}
	/* fix broken Tx power entries */
	for (i = 0; i < 40; i++) {
		if (sc->txpow1[14 + i] < -7 || sc->txpow1[14 + i] > 15)
			sc->txpow1[14 + i] = 5;
		if (sc->txpow2[14 + i] < -7 || sc->txpow2[14 + i] > 15)
			sc->txpow2[14 + i] = 5;
		DPRINTF("chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[14 + i].chan, sc->txpow1[14 + i],
		    sc->txpow2[14 + i]);
	}

	/* read Tx power compensation for each Tx rate */
	run_srom_read(sc, RT2860_EEPROM_DELTAPWR, &val);
	delta_2ghz = delta_5ghz = 0;
	if ((val & 0xff) != 0xff && (val & 0x80)) {
		delta_2ghz = val & 0xf;
		if (!(val & 0x40))	/* negative number */
			delta_2ghz = -delta_2ghz;
	}
	val >>= 8;
	if ((val & 0xff) != 0xff && (val & 0x80)) {
		delta_5ghz = val & 0xf;
		if (!(val & 0x40))	/* negative number */
			delta_5ghz = -delta_5ghz;
	}
	DPRINTF("power compensation=%d (2GHz), %d (5GHz)\n",
	    delta_2ghz, delta_5ghz);

	for (ridx = 0; ridx < 5; ridx++) {
		uint32_t reg;

		run_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2, &val);
		reg = val;
		run_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2 + 1, &val);
		reg |= (uint32_t)val << 16;

		sc->txpow20mhz[ridx] = reg;
		sc->txpow40mhz_2ghz[ridx] = b4inc(reg, delta_2ghz);
		sc->txpow40mhz_5ghz[ridx] = b4inc(reg, delta_5ghz);

		DPRINTF("ridx %d: power 20MHz=0x%08x, 40MHz/2GHz=0x%08x, "
		    "40MHz/5GHz=0x%08x\n", ridx, sc->txpow20mhz[ridx],
		    sc->txpow40mhz_2ghz[ridx], sc->txpow40mhz_5ghz[ridx]);
	}

	/* read RSSI offsets and LNA gains from EEPROM */
	run_srom_read(sc, RT2860_EEPROM_RSSI1_2GHZ, &val);
	sc->rssi_2ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_2ghz[1] = val >> 8;	/* Ant B */
	run_srom_read(sc, RT2860_EEPROM_RSSI2_2GHZ, &val);
	if (sc->mac_ver >= 0x3070) {
		/*
		 * On RT3070 chips (limited to 2 Rx chains), this ROM
		 * field contains the Tx mixer gain for the 2GHz band.
		 */
		if ((val & 0xff) != 0xff)
			sc->txmixgain_2ghz = val & 0x7;
		DPRINTF("tx mixer gain=%u (2GHz)\n", sc->txmixgain_2ghz);
	} else
		sc->rssi_2ghz[2] = val & 0xff;	/* Ant C */
	sc->lna[2] = val >> 8;		/* channel group 2 */

	run_srom_read(sc, RT2860_EEPROM_RSSI1_5GHZ, &val);
	sc->rssi_5ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_5ghz[1] = val >> 8;	/* Ant B */
	run_srom_read(sc, RT2860_EEPROM_RSSI2_5GHZ, &val);
	if (sc->mac_ver == 0x3572) {
		/*
		 * On RT3572 chips (limited to 2 Rx chains), this ROM
		 * field contains the Tx mixer gain for the 5GHz band.
		 */
		if ((val & 0xff) != 0xff)
			sc->txmixgain_5ghz = val & 0x7;
		DPRINTF("tx mixer gain=%u (5GHz)\n", sc->txmixgain_5ghz);
	} else
		sc->rssi_5ghz[2] = val & 0xff;	/* Ant C */
	sc->lna[3] = val >> 8;		/* channel group 3 */

	run_srom_read(sc, RT2860_EEPROM_LNA, &val);
	sc->lna[0] = val & 0xff;	/* channel group 0 */
	sc->lna[1] = val >> 8;		/* channel group 1 */

	/* fix broken 5GHz LNA entries */
	if (sc->lna[2] == 0 || sc->lna[2] == 0xff) {
		DPRINTF("invalid LNA for channel group %d\n", 2);
		sc->lna[2] = sc->lna[1];
	}
	if (sc->lna[3] == 0 || sc->lna[3] == 0xff) {
		DPRINTF("invalid LNA for channel group %d\n", 3);
		sc->lna[3] = sc->lna[1];
	}

	/* fix broken RSSI offset entries */
	for (ant = 0; ant < 3; ant++) {
		if (sc->rssi_2ghz[ant] < -10 || sc->rssi_2ghz[ant] > 10) {
			DPRINTF("invalid RSSI%d offset: %d (2GHz)\n",
			    ant + 1, sc->rssi_2ghz[ant]);
			sc->rssi_2ghz[ant] = 0;
		}
		if (sc->rssi_5ghz[ant] < -10 || sc->rssi_5ghz[ant] > 10) {
			DPRINTF("invalid RSSI%d offset: %d (5GHz)\n",
			    ant + 1, sc->rssi_5ghz[ant]);
			sc->rssi_5ghz[ant] = 0;
		}
	}
#endif
	return B_OK;
}


status_t
RalinkUSB::CompareAndReattach(usb_device device)
{
	TRACE_ALWAYS(DRIVER_NAME"::CompareAndReattach()\n");
	
	const usb_device_descriptor *deviceDescriptor
		= gUSBModule->get_device_descriptor(device);

	if (deviceDescriptor == NULL) {
		TRACE_ALWAYS(DRIVER_NAME": Error getting USB device descriptor.\n");
		return B_ERROR;
	}

	/*if (deviceDescriptor->vendor_id != fDeviceInfo.VendorId()
		&& deviceDescriptor->product_id != fDeviceInfo.ProductId()) {
		// this certainly isn't the same device
		return B_BAD_VALUE;
	}*/

	// this is the same device that was replugged - clear the removed state,
	// re-setup the endpoints and transfers and open the device if it was
	// previously opened
	fDevice = device;
	fRemoved = false;
	status_t result = _SetupEndpoints();
	if (result != B_OK) {
		fRemoved = true;
		return result;
	}

	result = _LoadMicrocode();
	if (result != B_OK)
		return result;
		
	// we need to setup hardware on device replug
	result = SetupDevice(true);
	if (result != B_OK) {
		return result;
	}

	if (fOpen) {
		fOpen = false;
		result = Open(fNonBlocking ? O_NONBLOCK : 0);
	}

	return result;
}


status_t
RalinkUSB::_StartDevice()
{
	TRACE_ALWAYS(DRIVER_NAME"::_StartDevice()\n");
	status_t status = _LoadMicrocode();
	if (status != B_OK)
		return status;
	return B_OK;	
}


status_t
RalinkUSB::_SetupEndpoints()
{
	TRACE_ALWAYS(DRIVER_NAME"::_SetupEndpoints()\n");
	const usb_configuration_info* config
		= gUSBModule->get_nth_configuration(fDevice, 0);

	if (config == NULL) {
		TRACE_ALWAYS(DRIVER_NAME": Error of getting USB device configuration.\n");
		return B_ERROR;
	}

	if (config->interface_count <= 0) {
		TRACE_ALWAYS(DRIVER_NAME": Error:no interfaces found in USB device configuration\n");
		return B_ERROR;
	}

	usb_interface_info* interface = config->interface[RT2860_IFACE_INDEX].active;
	if (interface == NULL) {
		TRACE_ALWAYS(DRIVER_NAME": Error:invalid active interface in "
												"USB device configuration\n");
		return B_ERROR;
	}

	gUSBModule->set_configuration(fDevice, config);
	
	int notifyEndpoint = -1;
	int readEndpoint   = -1;
	int writeEndpoint  = -1;

	for (size_t ep = 0; ep < interface->endpoint_count; ep++) {
		usb_endpoint_descriptor* epd = interface->endpoint[ep].descr;
		
		dprintf("\tlength: %d\n\tdescriptor_type: %d\n\tendpoint_address: %d",
			epd->length, epd->descriptor_type, epd->endpoint_address);
		dprintf("\n\tattributes: %x\n\tmax_packet_size: %d\n\tinterval: %d\n",
			epd->attributes, epd->max_packet_size, epd->interval);	
		
		if ((epd->attributes & USB_ENDPOINT_ATTR_MASK)
				== USB_ENDPOINT_ATTR_INTERRUPT) {
			notifyEndpoint = ep;
			dprintf("nofify endpoint\n");
			continue;
		}

		if ((epd->attributes & USB_ENDPOINT_ATTR_MASK)
				!= USB_ENDPOINT_ATTR_BULK) {
			TRACE_ALWAYS(DRIVER_NAME": Error: USB endpoint type %#04x is unknown.\n",
					epd->attributes);
			continue;
		}

		if ((epd->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
				== USB_ENDPOINT_ADDR_DIR_IN) {
			readEndpoint = ep;
			dprintf("read endpoint\n");
			continue;
		}

		if ((epd->endpoint_address & USB_ENDPOINT_ADDR_DIR_OUT)
				== USB_ENDPOINT_ADDR_DIR_OUT) {
			writeEndpoint = ep;
			dprintf("write endpoint\n");
			continue;
		}
	}

	if (/*notifyEndpoint == -1 || */readEndpoint == -1 || writeEndpoint == -1) {
		TRACE_ALWAYS(DRIVER_NAME": Error: not all USB endpoints were found: notify:%d; "
			"read:%d; write:%d\n", notifyEndpoint, readEndpoint, writeEndpoint);
		return B_ERROR;
	}

	//fNotifyEndpoint = interface->endpoint[notifyEndpoint].handle;
	fReadEndpoint = interface->endpoint[readEndpoint].handle;
	fWriteEndpoint = interface->endpoint[writeEndpoint].handle;
	fMaxTXPacketSize = interface->endpoint[writeEndpoint].descr->max_packet_size;

	return B_OK;
}


status_t
RalinkUSB::_Reset()
{
	size_t dummy;
	return gUSBModule->send_request(fDevice,
		USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_OUT,
		RT2870_RESET, 1, 0, 0, NULL, &dummy);
}


status_t
RalinkUSB::_LoadMicrocode()
{
	const char* firmware = "/system/non-packaged/data/firmware/ralink/rt2870.bin";
			
	TRACE_ALWAYS(DRIVER_NAME": selected firmware %s\n", firmware);
	int fd = open(firmware, B_READ_ONLY);
	if (fd < 0) {
		TRACE_ALWAYS(DRIVER_NAME": firmware file unavailable\n");
		close(fd);
		return B_ERROR;
	}

	int32 fileSize = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	if (fileSize <= 0) {
		TRACE_ALWAYS(DRIVER_NAME": firmware file seems empty\n");
		close(fd);
		return B_ERROR;
	}

	uint8* buffer = (uint8*)malloc(fileSize);
	if (buffer == NULL) {
		TRACE_ALWAYS(DRIVER_NAME": no memory for firmware buffer\n");
		close(fd);
		return B_NO_MEMORY;
	}

	ssize_t readCount = read(fd, buffer, fileSize);
	close(fd);

	if (readCount != 8192) {
		TRACE_ALWAYS(DRIVER_NAME": invalid firmware size\n");
		free(buffer);
		return B_ERROR;
	}
	/*
	 * RT3071/RT3072 use a different firmware
	 * run-rt2870 (8KB) contains both,
	 * first half (4KB) is for rt2870,
	 * last half is for rt3071.
	 */
	uint8* firmwareBase = buffer;
	if (fMACVersion != 0x2860 &&
	    fMACVersion != 0x2872 &&
	    fMACVersion != 0x3070) { 
		firmwareBase += 4096;
	}

	/* cheap sanity check */
	int64 bytes = *(int64*)buffer;
	if (bytes != B_BENDIAN_TO_HOST_INT64(0xffffff0210280210LL)) {
		TRACE_ALWAYS(DRIVER_NAME": firmware checksum failed\n");
		free(buffer);
		return EINVAL;
	}

	TRACE_ALWAYS(DRIVER_NAME": loading firmware...\n");
	
	uint32 tmp;
	_Read(RT2860_ASIC_VER_ID, &tmp);
	/* write microcode image */
	_WriteRegion(RT2870_FW_BASE, firmwareBase, 4096);
	_Write(RT2860_H2M_MAILBOX_CID, 0xffffffff);
	_Write(RT2860_H2M_MAILBOX_STATUS, 0xffffffff);

	TRACE_ALWAYS(DRIVER_NAME": firmware reset...\n");
	size_t actualLength;
	status_t status = gUSBModule->send_request(fDevice,
		USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_OUT,
		RT2870_RESET, 8, 0, 0, NULL, &actualLength);
	if (status != B_OK)	{
		TRACE_ALWAYS(DRIVER_NAME": firmware reset failed\n");
		free(buffer);
		return status;
	}
	
	TRACE_ALWAYS(DRIVER_NAME": firmware reset completed!\n");
	
	TRACE_ALWAYS(DRIVER_NAME": delay...\n");
	_Delay(10);
	
	TRACE_ALWAYS(DRIVER_NAME": WRITE_MAILBOX\n");
	_Write(RT2860_H2M_MAILBOX, 0);
	
	TRACE_ALWAYS(DRIVER_NAME": _SendMCUCommand()\n");
	if ((status = _SendMCUCommand(RT2860_MCU_CMD_RFRESET, 0)) != B_OK) {
		dprintf("MCU Command Sent\n");
		free(buffer);
		return status;
	}

	TRACE_ALWAYS(DRIVER_NAME": Wait for microcontroller...\n");
	/* wait until microcontroller is ready */
	int ntries;
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((status = _Read(RT2860_SYS_CTRL, &tmp)) != B_OK) {
			free(buffer);
			return status;
		}
		if (tmp & RT2860_MCU_READY)
			break;
		_Delay(10);
	}
	if (ntries == 1000) {
		TRACE_ALWAYS(DRIVER_NAME": timeout waiting for MCU to initialize\n");
		free(buffer);
		return ETIMEDOUT;
	}
	TRACE_ALWAYS(DRIVER_NAME": firmware %s ver. %u.%u loaded\n",
	    (firmwareBase == buffer) ? "RT2870" : "RT3071", 10, 10);
//	    *(firmwareBase + 4092), *(firmwareBase + 4093));

	free(buffer);
	return B_OK;
}


status_t
RalinkUSB::_EtherInit()
{
	return B_OK;
}


status_t
RalinkUSB::_Read(uint16 reg, uint32* val)
{
	uint32 tmp;
	status_t error = _ReadRegion(reg, (uint8*)&tmp, sizeof(tmp));
	if (error == B_OK)
		*val = B_LENDIAN_TO_HOST_INT32(tmp);
	else
		*val = 0xffffffff;
	return (error);
}


status_t
RalinkUSB::_ReadRegion(uint16 reg, uint8* buffer, uint16 size)
{
	size_t actualLength = 0;
	status_t result = gUSBModule->send_request(fDevice,
		USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_IN,
		RT2870_READ_REGION_1, 0, reg, size, buffer, &actualLength);

	if (size != actualLength) {
		TRACE_ALWAYS(DRIVER_NAME": Size mismatch reading register ! asked %d got %d",
			size, actualLength);
	}

	return result;
}


status_t
RalinkUSB::_Write2(uint16 reg, uint16 val)
{
	size_t actualLength = 0;
	status_t result = gUSBModule->send_request(fDevice,
		USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_OUT,
		RT2870_WRITE_2, val, reg, sizeof(val), NULL, &actualLength);
		
	return result;
}


status_t
RalinkUSB::_WriteRegion(uint16 reg, const uint8* buffer, uint16 len)
{
#if 1
	status_t status;
	/*
	 * NB: the WRITE_REGION_1 command is not stable on RT2860.
	 * We thus issue multiple WRITE_2 commands instead.
	 */
	dprintf("RalinkUSB::_WriteRegion(%d, %p, len: %u)\n",
		reg, buffer, len);
	for (int i = 0; i < len && status == B_OK; i += 2)
		status = _Write2(reg + i, buffer[i] | buffer[i + 1] << 8);
	return status;
#else
	return gUSBModule->send_request(fDevice,
		USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_OUT
		RT2870_WRITE_REGION_1, 0, reg, len, buffer, NULL);

#endif
}


status_t
RalinkUSB::_Write(uint16 reg, uint32 val)
{
	status_t status;
	if ((status = _Write2(reg, val & 0xffff)) == B_OK)
		status = _Write2(reg + 2, val >> 16);
	return status;
}


status_t
RalinkUSB::_ReadMACAddress(ether_address_t *address)
{
	uint16 val;
	memset(address, 0, sizeof(ether_address_t));
	
	/* read MAC address */
	status_t result = _ReadEEPROM(RT2860_EEPROM_MAC01, &val);
	if (result == B_OK) {
		address->ebyte[0] = val & 0xff;
		address->ebyte[1] = val >> 8;
		result = _ReadEEPROM(RT2860_EEPROM_MAC23, &val);
	}
	
	if (result == B_OK) {
		address->ebyte[2] = val & 0xff;
		address->ebyte[3] = val >> 8;
		result = _ReadEEPROM(RT2860_EEPROM_MAC45, &val);
	}
	
	if (result == B_OK) {
		address->ebyte[4] = val & 0xff;
		address->ebyte[5] = val >> 8;
	} else
		TRACE_ALWAYS("Error of reading MAC address:%#010x\n", result);

	return result;
}


status_t
RalinkUSB::_ReadEEPROM(uint16 reg, uint16* val)
{
	if (fEFuse)
		return _ReadEFUSE(reg, val);
		
	uint16 tmp;
	size_t actualLength = 0;
	reg *= 2;
	status_t result = gUSBModule->send_request(fDevice,
		USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_IN,
		RT2870_EEPROM_READ, 0, reg, sizeof(tmp), &tmp, &actualLength);

	if (sizeof(tmp) != actualLength) {
		TRACE_ALWAYS("Size mismatch reading register ! asked %d got %d",
			sizeof(tmp), actualLength);
	}

	if (result != B_OK)
		TRACE(strerror(result));
		
	if (result == B_OK)
		*val = B_LENDIAN_TO_HOST_INT16(tmp);
	else
		*val = 0xffff;
	return result;
}


/* Read 16-bit from eFUSE ROM (RT3070 only.) */
status_t
RalinkUSB::_ReadEFUSE(uint16 addr, uint16* val)
{
	uint32 tmp;
	uint16 reg;
	status_t error;
	int ntries;

	if ((error = _Read(RT3070_EFUSE_CTRL, &tmp)) != 0)
		return error;

	addr *= 2;
	/*-
	 * Read one 16-byte block into registers EFUSE_DATA[0-3]:
	 * DATA0: F E D C
	 * DATA1: B A 9 8
	 * DATA2: 7 6 5 4
	 * DATA3: 3 2 1 0
	 */
	tmp &= ~(RT3070_EFSROM_MODE_MASK | RT3070_EFSROM_AIN_MASK);
	tmp |= (addr & ~0xf) << RT3070_EFSROM_AIN_SHIFT | RT3070_EFSROM_KICK;
	_Write(RT3070_EFUSE_CTRL, tmp);
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = _Read(RT3070_EFUSE_CTRL, &tmp)) != 0)
			return error;
		if (!(tmp & RT3070_EFSROM_KICK))
			break;
		_Delay(2);
	}
	if (ntries == 100)
		return ETIMEDOUT;

	if ((tmp & RT3070_EFUSE_AOUT_MASK) == RT3070_EFUSE_AOUT_MASK) {
		*val = 0xffff;	/* address not found */
		return B_OK;
	}
	/* determine to which 32-bit register our 16-bit word belongs */
	reg = RT3070_EFUSE_DATA3 - (addr & 0xc);
	if ((error = _Read(reg, &tmp)) != 0)
		return error;

	*val = (addr & 2) ? tmp >> 16 : tmp & 0xffff;
	return B_OK;
}


status_t
RalinkUSB::_SendMCUCommand(uint8 command, uint16 arg)
{
	uint32 tmp;
	status_t status;
	int ntries;
	for (ntries = 0; ntries < 100; ntries++) {
		if ((status = _Read(RT2860_H2M_MAILBOX, &tmp)) != B_OK)
			return status;
		if (!(tmp & RT2860_H2M_BUSY))
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	tmp = RT2860_H2M_BUSY | RT2860_TOKEN_NO_INTR << 16 | arg;
	if ((status = _Write(RT2860_H2M_MAILBOX, tmp)) == B_OK)
		status = _Write(RT2860_HOST_CMD, command);
	return status;
}


void
RalinkUSB::_Delay(int ms)
{
	spin(ms * 1000);
	//usb_pause_mtx(mtx_owned(&sc->sc_mtx) ? 
	  //  &sc->sc_mtx : NULL, USB_MS_TO_TICKS(ms));
}
