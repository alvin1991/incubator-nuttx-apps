/****************************************************************************
 *
 *   Copyright (c) 2012-2014 PX4 Development Team. All rights reserved.
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
 * @file CDev.cpp
 *
 * Character device base class.
 */

#include <stdio.h>
#include <cstring>
#include <syslog.h>
#include <errno.h>
#include <poll.h>
#include <debug.h>

#include <nuttx/config.h>
#include <nuttx/semaphore.h>

#include "uORB/device/drv_device.h"
#include "uORB/cdev/CDev.hpp"

/* Extern cdev debug as system debug */
#define cdevinfo(format, ...)   _info(format, ##__VA_ARGS__)
#define cdevwarn(format, ...)   _warn(format, ##__VA_ARGS__)
#define cdeverr(format, ...)     _err(format, ##__VA_ARGS__)
namespace device
{

CDev::CDev(const char *name, const char *devname) :
	Device(name),
	_devname(devname)
{
	cdevinfo("[%s] CDev::CDev\n",_name);

	int ret = nxsem_init(&_lock, 0, 1);

	if (ret != 0) {
		cdeverr("[%s] SEM INIT FAIL: ret %d\n",_name ,ret);
	}
}

CDev::~CDev()
{
	cdevinfo("[%s] CDev::~CDev\n",_name);

	if (_registered) {
		unregister_driver(_devname);
	}

	if (_pollset) {
		delete[](_pollset);
	}

	nxsem_destroy(&_lock);
}

int
CDev::register_class_devname(const char *class_devname)
{
	cdevinfo("CDev::register_class_devname [%s]\n", class_devname);

	if (class_devname == nullptr) {
		return -EINVAL;
	}

	int class_instance = 0;
	int ret = -ENOSPC;

	while (class_instance < 4) {
		char name[32];
		snprintf(name, sizeof(name), "%s%d", class_devname, class_instance);
		ret = register_driver(name, &fops, 0666, (void *)this);

		if (ret == OK) {
			break;
		}

		class_instance++;
	}

	if (class_instance == 4) {
		return ret;
	}

	return class_instance;
}

int
CDev::unregister_class_devname(const char *class_devname, unsigned class_instance)
{
	cdevinfo("CDev::unregister_class_devname [%s]\n",class_devname);
	char name[32];
	snprintf(name, sizeof(name), "%s%u", class_devname, class_instance);
	return unregister_driver(name);
}

int
CDev::init()
{
	cdevinfo("[%s] CDev::init\n",_name);

	// base class init first
	int ret = Device::init();

	if (ret != OK) {
		goto out;
	}

	// now register the driver
	if (_devname != nullptr) {
		ret = register_driver(_devname, &fops, 0666, (void *)this);

		if (ret != OK) {
			goto out;
		}

		_registered = true;
	}

out:
	return ret;
}

/*
 * Default implementations of the character device interface
 */
int
CDev::open(file_t *filep)
{
	cdevinfo("[%s] CDev::open\n",_name);
	int ret = OK;

	lock();
	/* increment the open count */
	_open_count++;

	if (_open_count == 1) {

		/* first-open callback may decline the open */
		ret = open_first(filep);

		if (ret != OK) {
			_open_count--;
		}
	}

	unlock();

	return ret;
}

int
CDev::open_first(file_t *filep)
{
	cdevinfo("[%s] CDev::open_first\n",_name);
	return OK;
}

int
CDev::close(file_t *filep)
{
	cdevinfo("[%s] CDev::close\n",_name);
	int ret = OK;

	lock();

	if (_open_count > 0) {
		/* decrement the open count */
		_open_count--;

		/* callback cannot decline the close */
		if (_open_count == 0) {
			ret = close_last(filep);
		}

	} else {
		ret = -EBADF;
	}

	unlock();

	return ret;
}

int
CDev::close_last(file_t *filep)
{
	cdevinfo("[%s] CDev::close_last\n",_name);
	return OK;
}

ssize_t
CDev::read(file_t *filep, char *buffer, size_t buflen)
{
	cdevinfo("[%s] CDev::read\n",_name);
	return -ENOSYS;
}

ssize_t
CDev::write(file_t *filep, const char *buffer, size_t buflen)
{
	cdevinfo("[%s] CDev::write\n",_name);
	return -ENOSYS;
}

off_t
CDev::seek(file_t *filep, off_t offset, int whence)
{
	cdevinfo("[%s] CDev::seek\n",_name);
	return -ENOSYS;
}

int
CDev::ioctl(file_t *filep, int cmd, unsigned long arg)
{
	cdevinfo("[%s] CDev::ioctl\n",_name);
	int ret = -ENOTTY;

	switch (cmd) {

	/* fetch a pointer to the driver's private data */
	case DIOC_GETPRIV:
		*(void **)(uintptr_t)arg = (void *)this;
		ret = OK;
		break;

	case DEVIOCSPUBBLOCK:
		_pub_blocked = (arg != 0);
		ret = OK;
		break;

	case DEVIOCGPUBBLOCK:
		ret = _pub_blocked;
		break;

	case DEVIOCGDEVICEID:
		ret = (int)_device_id.devid;
		break;

	default:
		break;
	}

	return ret;
}

int
CDev::poll(file_t *filep, struct pollfd *fds, bool setup)
{
	cdevinfo("[%s] CDev::Poll %s\n", setup ? "setup" : "teardown",_name);
	int ret = OK;

	/*
	 * Lock against pollnotify() (and possibly other callers)
	 */
	lock();

	if (setup) {
		/*
		 * Save the file pointer in the pollfd for the subclass'
		 * benefit.
		 */
		fds->priv = (void *)filep;
		cdevinfo("[%s] CDev::poll: fds->priv = %p\n",_name , filep);

		/*
		 * Handle setup requests.
		 */
		ret = store_poll_waiter(fds);

		if (ret == OK) {

			/*
			 * Check to see whether we should send a poll notification
			 * immediately.
			 */
			fds->revents |= fds->events & poll_state(filep);

			/* yes? post the notification */
			if (fds->revents != 0) {
				nxsem_post(fds->sem);
			}

		} else {
			cdeverr("[%s] Store Poll Waiter error.\n",_name);
		}

	} else {
		/*
		 * Handle a teardown request.
		 */
		ret = remove_poll_waiter(fds);
	}

	unlock();

	return ret;
}

void
CDev::poll_notify(pollevent_t events)
{
	cdevinfo("[%s] CDev::poll_notify events = %0x\n",_name , events);

	/* lock against poll() as well as other wakeups */
	ATOMIC_ENTER;

	for (unsigned i = 0; i < _max_pollwaiters; i++) {
		if (nullptr != _pollset[i]) {
			poll_notify_one(_pollset[i], events);
		}
	}

	ATOMIC_LEAVE;
}

void
CDev::poll_notify_one(struct pollfd *fds, pollevent_t events)
{
	cdevinfo("[%s] CDev::poll_notify_one\n",_name);

	int value = fds->sem->semcount;


	/* update the reported event set */
	fds->revents |= fds->events & events;

	cdevinfo("[%s] Events fds=%p %0x %0x %0x %d\n",_name, fds, fds->revents, fds->events, events, value);

	/* if the state is now interesting, wake the waiter if it's still asleep */
	/* XXX semcount check here is a vile hack; counting semphores should not be abused as cvars */
	if ((fds->revents != 0) && (value <= 0)) {
		nxsem_post(fds->sem);
	}
}

pollevent_t
CDev::poll_state(file_t *filep)
{
	cdevinfo("[%s] CDev::poll_notify\n",_name);
	/* by default, no poll events to report */
	return 0;
}

int
CDev::store_poll_waiter(struct pollfd *fds)
{
	/*
	 * Look for a free slot.
	 */
	cdevinfo("[%s] CDev::store_poll_waiter\n",_name);

	for (unsigned i = 0; i < _max_pollwaiters; i++) {
		if (nullptr == _pollset[i]) {

			/* save the pollfd */
			_pollset[i] = fds;

			return OK;
		}
	}

	/* No free slot found. Resize the pollset */

	if (_max_pollwaiters >= 256 / 2) { //_max_pollwaiters is uint8_t
		return -ENOMEM;
	}

	const uint8_t new_count = _max_pollwaiters > 0 ? _max_pollwaiters * 2 : 1;
	struct pollfd **new_pollset = new struct pollfd *[new_count];

	if (!new_pollset) {
		return -ENOMEM;
	}

	if (_max_pollwaiters > 0) {
		memset(new_pollset + _max_pollwaiters, 0, sizeof(struct pollfd *) * (new_count - _max_pollwaiters));
		memcpy(new_pollset, _pollset, sizeof(struct pollfd *) * _max_pollwaiters);
		delete[](_pollset);
	}

	_pollset = new_pollset;
	_pollset[_max_pollwaiters] = fds;
	_max_pollwaiters = new_count;
	return OK;
}

int
CDev::remove_poll_waiter(struct pollfd *fds)
{
	cdevinfo("[%s] CDev::remove_poll_waiter\n",_name);

	for (unsigned i = 0; i < _max_pollwaiters; i++) {
		if (fds == _pollset[i]) {

			_pollset[i] = nullptr;
			return OK;

		}
	}

	cdeverr("[%s] poll: bad fd state\n",_name);
	return -EINVAL;
}

} // namespace device
