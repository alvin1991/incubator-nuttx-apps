/****************************************************************************
 *
 *   Copyright (c) 2012-2015 PX4 Development Team. All rights reserved.
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
#include <nuttx/config.h>
#include <nuttx/init.h>

#include <string.h>
#include <syslog.h>
#include <stdio.h>

#include <cstdio>
#include <debug.h>
#include <errno.h>

// #include "platform/cxxinitialize.h"


#include "uORB/orb/uORB.h"
#include "uORB/orb/uORBDevices.hpp"
#include "uORB/orb/uORBManager.hpp"
#include "uORB/orb/uORBCommon.hpp"

extern "C" { int uorb_main(int argc, char *argv[]); }

static uORB::DeviceMaster *g_dev = nullptr;
static void usage()
{
	printf("uorb communication\n");
	printf("start\n");
	printf("Print topic statistics\n");
	printf("Monitor topic publication rates\n");
	printf("print all instead of only currently publishing topics\n");
	printf("<filter1> [<filter2>] topic(s) to match (implies -a)\n");
}

int
uorb_main(int argc, char *argv[])
{
	if (argc < 2) {
		usage();
		return -EINVAL;
	}

	/*
	 * Start/load the driver.
	 */
	if (!strcmp(argv[1], "start")) {

		if (g_dev != nullptr) {
			syslog(LOG_INFO,"already loaded");
			/* user wanted to start uorb, its already running, no error */
			return 0;
		}

		if (!uORB::Manager::initialize()) {
			syslog(LOG_INFO,"uorb manager alloc failed");
			return -ENOMEM;
		}

		/* create the driver */
		g_dev = uORB::Manager::get_instance()->get_device_master();

		if (g_dev == nullptr) {
			return -errno;
		}

		return OK;
	}

	/*
	 * Print driver information.
	 */
	if (!strcmp(argv[1], "status")) {
		if (g_dev != nullptr) {
			g_dev->printStatistics(true);

		} else {
			syslog(LOG_INFO,"uorb is not running\n");
		}

		return OK;
	}

	if (!strcmp(argv[1], "top")) {
		if (g_dev != nullptr) {
			g_dev->showTop(argv + 2, argc - 2);

		} else {
			syslog(LOG_INFO,"uorb is not running\n");
		}

		return OK;
	}

	usage();
	return -EINVAL;
}
