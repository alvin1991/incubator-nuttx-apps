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
 * @file cdev_platform.cpp
 *
 * NuttX Character device functions
 */
#include "uORB/cdev/cdev_platform.hpp"
#include "uORB/cdev/CDev.hpp"

#include <sys/ioctl.h>
#include <poll.h>

#ifdef CONFIG_DISABLE_POLL
# error This driver is not compatible with CONFIG_DISABLE_POLL
#endif

namespace device
{

/*
 * The standard NuttX operation dispatch table can't call C++ member functions
 * directly, so we have to bounce them through this dispatch table.
 */
static int		cdev_open(file_t *filp);
static int		cdev_close(file_t *filp);
static ssize_t	cdev_read(file_t *filp, char *buffer, size_t buflen);
static ssize_t	cdev_write(file_t *filp, const char *buffer, size_t buflen);
static off_t	cdev_seek(file_t *filp, off_t offset, int whence);
static int		cdev_ioctl(file_t *filp, int cmd, unsigned long arg);
static int		cdev_poll(file_t *filp, struct pollfd *fds, bool setup);

/**
 * Character device indirection table.
 *
 * Every cdev we register gets the same function table; we use the private data
 * field in the inode to store the instance pointer.
 *
 * Note that we use the GNU extension syntax here because we don't get designated
 * initialisers in gcc 4.6.
 */
const struct file_operations device::CDev::fops = {
open	: cdev_open,
close	: cdev_close,
read	: cdev_read,
write	: cdev_write,
seek	: cdev_seek,
ioctl	: cdev_ioctl,
poll	: cdev_poll
};

static int
cdev_open(file_t *filp)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->open(filp);
}

static int
cdev_close(file_t *filp)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->close(filp);
}

static ssize_t
cdev_read(file_t *filp, char *buffer, size_t buflen)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->read(filp, buffer, buflen);
}

static ssize_t
cdev_write(file_t *filp, const char *buffer, size_t buflen)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->write(filp, buffer, buflen);
}

static off_t
cdev_seek(file_t *filp, off_t offset, int whence)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->seek(filp, offset, whence);
}

static int
cdev_ioctl(file_t *filp, int cmd, unsigned long arg)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->ioctl(filp, cmd, arg);
}

static int
cdev_poll(file_t *filp, struct pollfd *fds, bool setup)
{
	device::CDev *cdev = (device::CDev *)(filp->f_inode->i_private);

	return cdev->poll(filp, fds, setup);
}

} // namespace device
