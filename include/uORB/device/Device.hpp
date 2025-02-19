/****************************************************************************
 *
 *   Copyright (c) 2012-2017 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file Device.hpp
 *
 * Definitions for the generic base classes in the device framework.
 */

#ifndef _DEVICE_DEVICE_HPP
#define _DEVICE_DEVICE_HPP

/*
 * Includes here should only cover the needs of the framework definitions.
 */
#include <arch/types.h>
#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <syslog.h>
#include <errno.h>
#include <debug.h>



#  define EXTRA_FMT "%s: "
#  define EXTRA_ARG ,__FUNCTION__

#define __EXPORT __attribute__ ((visibility ("default")))

#define DEVICE_LOG(FMT, ...) 	syslog(LOG_INFO, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define DEVICE_DEBUG(FMT, ...) 	syslog(LOG_INFO, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)

/**
 * Namespace encapsulating all device framework classes, functions and data.
 */
namespace device
{

/**
 * Fundamental base class for all physical drivers (I2C, SPI).
 *
 * This class provides the basic driver template for I2C and SPI devices
 */
class __EXPORT Device
{
public:
	/**
	 * Destructor.
	 *
	 * Public so that anonymous devices can be destroyed.
	 */
	virtual ~Device() = default;

	/*
	 * Direct access methods.
	 */

	/**
	 * Initialise the driver and make it ready for use.
	 *
	 * @return	OK if the driver initialized OK, negative errno otherwise;
	 */
	virtual int	init() { return 0; }

	/**
	 * Read directly from the device.
	 *
	 * The actual size of each unit quantity is device-specific.
	 *
	 * @param offset	The device address at which to start reading
	 * @param data		The buffer into which the read values should be placed.
	 * @param count		The number of items to read.
	 * @return		The number of items read on success, negative errno otherwise.
	 */
	virtual int	read(unsigned address, void *data, unsigned count) { return -ENODEV; }

	/**
	 * Write directly to the device.
	 *
	 * The actual size of each unit quantity is device-specific.
	 *
	 * @param address	The device address at which to start writing.
	 * @param data		The buffer from which values should be read.
	 * @param count		The number of items to write.
	 * @return		The number of items written on success, negative errno otherwise.
	 */
	virtual int	write(unsigned address, void *data, unsigned count) { return -ENODEV; }

	/**
	 * Perform a device-specific operation.
	 *
	 * @param operation	The operation to perform.
	 * @param arg		An argument to the operation.
	 * @return		Negative errno on error, OK or positive value on success.
	 */
	virtual int	ioctl(unsigned operation, unsigned &arg)
	{
//		switch (operation) {
//		case DEVIOCGDEVICEID:
//			return (int)_device_id.devid;
//		}

		return -ENODEV;
	}

	/** Device bus types for DEVID */
	enum DeviceBusType {
		DeviceBusType_UNKNOWN = 0,
		DeviceBusType_I2C     = 1,
		DeviceBusType_SPI     = 2,
		DeviceBusType_UAVCAN  = 3,
	};

	/**
	 * Return the bus ID the device is connected to.
	 *
	 * @return The bus ID
	 */
	uint8_t get_device_bus() { return _device_id.devid_s.bus; }

	/**
	 * Return the bus type the device is connected to.
	 *
	 * @return The bus type
	 */
	DeviceBusType get_device_bus_type() { return _device_id.devid_s.bus_type; }

	/**
	 * Return the bus address of the device.
	 *
	 * @return The bus address
	 */
	uint8_t get_device_address() { return _device_id.devid_s.address; }

	void set_device_address(int address) { _device_id.devid_s.address = address; }

	/**
	 * Set the device type
	 *
	 * @return The device type
	 */
	void set_device_type(uint8_t devtype) { _device_id.devid_s.devtype = devtype; }

	virtual bool external() { return false; }

	/*
	  broken out device elements. The bitfields are used to keep
	  the overall value small enough to fit in a float accurately,
	  which makes it possible to transport over the MAVLink
	  parameter protocol without loss of information.
	 */
	struct DeviceStructure {
		enum DeviceBusType bus_type : 3;
		uint8_t bus: 5;    // which instance of the bus type
		uint8_t address;   // address on the bus (eg. I2C address)
		uint8_t devtype;   // device class specific device type
	};

	union DeviceId {
		struct DeviceStructure devid_s;
		uint32_t devid;
	};

protected:
	union DeviceId	_device_id;             /**< device identifier information */

	const char	*_name;			/**< driver name */
	bool		_debug_enabled{false};		/**< if true, debug messages are printed */

	Device(const char *name) : _name(name)
	{
		/* setup a default device ID. When bus_type is UNKNOWN the
		   other fields are invalid */
		_device_id.devid = 0;
		_device_id.devid_s.bus_type = DeviceBusType_UNKNOWN;
		_device_id.devid_s.bus = 0;
		_device_id.devid_s.address = 0;
		_device_id.devid_s.devtype = 0;
	}

	Device(DeviceBusType bus_type, uint8_t bus, uint8_t address, uint8_t devtype = 0)
	{
		_device_id.devid = 0;
		_device_id.devid_s.bus_type = bus_type;
		_device_id.devid_s.bus = bus;
		_device_id.devid_s.address = address;
		_device_id.devid_s.devtype = devtype;
	}

	// no copy, assignment, move, move assignment
	Device(const Device &) = delete;
	Device &operator=(const Device &) = delete;
	Device(Device &&) = delete;
	Device &operator=(Device &&) = delete;

};

} // namespace device

#endif /* _DEVICE_DEVICE_HPP */
