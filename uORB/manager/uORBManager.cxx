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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#include <nuttx/fs/fs.h>

#include "uORB/orb/uORBUtils.hpp"
#include "uORB/orb/uORBManager.hpp"
#include "uORB/orb/uORBDevices.hpp"



//=========================  Static initializations =================
uORB::Manager *uORB::Manager::_Instance = nullptr;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool uORB::Manager::initialize()
{
	if (_Instance == nullptr) {
		_Instance = new uORB::Manager();
	}

	return _Instance != nullptr;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
uORB::Manager::Manager()
{
#ifdef ORB_USE_PUBLISHER_RULES
	const char *file_name = "./rootfs/orb_publisher.rules";
	int ret = readPublisherRulesFromFile(file_name, _publisher_rule);

	if (ret == OK) {
		_has_publisher_rules = true;
		syslog(LOG_INFO,"Using orb rules from %s\n", file_name);

	} else {
		syslog(LOG_ERR,"Failed to read publisher rules file %s (%s)\n", file_name, strerror(-ret));
	}

#endif /* ORB_USE_PUBLISHER_RULES */

}

uORB::Manager::~Manager()
{
	delete _device_master;
}

uORB::DeviceMaster *uORB::Manager::get_device_master()
{
	if (!_device_master) {
		_device_master = new DeviceMaster();

		if (_device_master) {
			int ret = _device_master->init();

			if (ret != OK) {
				syslog(LOG_ERR,"Initialization of DeviceMaster failed (%i)\n", ret);
				errno = -ret;
				delete _device_master;
				_device_master = nullptr;
			}

		} else {
			syslog(LOG_ERR,"Failed to allocate DeviceMaster\n");
			errno = ENOMEM;
		}
	}

	return _device_master;
}

int uORB::Manager::orb_exists(const struct orb_metadata *meta, int instance)
{
	/*
	 * Generate the path to the node and try to open it.
	 */
	char path[orb_maxpath];
	int inst = instance;
	int ret = uORB::Utils::node_mkpath(path, meta, &inst);

	if (ret != OK) {
		errno = -ret;
		return -1;
	}


	struct stat buffer;
	ret = stat(path, &buffer);

#ifdef ORB_COMMUNICATOR

	if (ret == -1 && meta != nullptr && !_remote_topics.empty()) {
		ret = (_remote_topics.find(meta->o_name) != _remote_topics.end()) ? OK : -1;
	}

#endif /* ORB_COMMUNICATOR */


	if (ret == 0) {
		// we know the topic exists, but it's not necessarily advertised/published yet (for example
		// if there is only a subscriber)
		// The open() will not lead to memory allocations.
		int fd = open(path, 0);

		if (fd >= 0) {
			unsigned long is_published;

			if (ioctl(fd, ORBIOCISPUBLISHED, (unsigned long)&is_published) == 0) {
				if (!is_published) {
					ret = -1;
				}
			}

			close(fd);
		}
	}

	return ret;
}

orb_advert_t uORB::Manager::orb_advertise_multi(const struct orb_metadata *meta, const void *data, int *instance,
		int priority, unsigned int queue_size)
{
#ifdef ORB_USE_PUBLISHER_RULES

	// check publisher rule
	if (_has_publisher_rules) {
		const char *prog_name = px4_get_taskname();

		if (strcmp(_publisher_rule.module_name, prog_name) == 0) {
			if (_publisher_rule.ignore_other_topics) {
				if (!findTopic(_publisher_rule, meta->o_name)) {
					syslog(LOG_DEBUG,"not allowing %s to publish topic %s\n", prog_name, meta->o_name);
					return (orb_advert_t)_Instance;
				}
			}

		} else {
			if (findTopic(_publisher_rule, meta->o_name)) {
				syslog(LOG_DEBUG,"not allowing %s to publish topic %s\n", prog_name, meta->o_name);
				return (orb_advert_t)_Instance;
			}
		}
	}

#endif /* ORB_USE_PUBLISHER_RULES */

	int result, fd;
	orb_advert_t advertiser;

	/* open the node as an advertiser */
	fd = node_open(meta, data, true, instance, priority);

	if (fd == -1) {
		syslog(LOG_ERR,"%s advertise failed\n", meta->o_name);
		return nullptr;
	}

	/* Set the queue size. This must be done before the first publication; thus it fails if
	 * this is not the first advertiser.
	 */
	result = ioctl(fd, ORBIOCSETQUEUESIZE, (unsigned long)queue_size);

	if (result < 0 && queue_size > 1) {
		syslog(LOG_ERR,"orb_advertise_multi: failed to set queue size\n");
	}

	/* get the advertiser handle and close the node */
	result = ioctl(fd, ORBIOCGADVERTISER, (unsigned long)&advertiser);
	close(fd);

	if (result == -1) {
		syslog(LOG_ERR,"ioctl ORBIOCGADVERTISER failed. fd = %d\n", fd);
		return nullptr;
	}

#ifdef ORB_COMMUNICATOR
	// For remote systems call over and inform them
	uORB::DeviceNode::topic_advertised(meta, priority);
#endif /* ORB_COMMUNICATOR */

	/* the advertiser must perform an initial publish to initialise the object */
	result = orb_publish(meta, advertiser, data);

	if (result == -1) {
		syslog(LOG_ERR,"orb_publish failed\n");
		return nullptr;
	}

	return advertiser;
}

int uORB::Manager::orb_unadvertise(orb_advert_t handle)
{
#ifdef ORB_USE_PUBLISHER_RULES

	if (handle == _Instance) {
		return OK; //pretend success
	}

#endif /* ORB_USE_PUBLISHER_RULES */

	return uORB::DeviceNode::unadvertise(handle);
}

int uORB::Manager::orb_subscribe(const struct orb_metadata *meta)
{
	return node_open(meta, nullptr, false);
}

int uORB::Manager::orb_subscribe_multi(const struct orb_metadata *meta, unsigned instance)
{
	int inst = instance;
	return node_open(meta, nullptr, false, &inst);
}

int uORB::Manager::orb_unsubscribe(int fd)
{
	return close(fd);
}

int uORB::Manager::orb_publish(const struct orb_metadata *meta, orb_advert_t handle, const void *data)
{
#ifdef ORB_USE_PUBLISHER_RULES

	if (handle == _Instance) {
		return OK; //pretend success
	}

#endif /* ORB_USE_PUBLISHER_RULES */

	return uORB::DeviceNode::publish(meta, handle, data);
}

int uORB::Manager::orb_copy(const struct orb_metadata *meta, int handle, void *buffer)
{
	int ret;

	ret = read(handle, buffer, meta->o_size);

	if (ret < 0) {
		return -1;
	}

	if (ret != (int)meta->o_size) {
		errno = EIO;
		return -1;
	}

	return OK;
}

int uORB::Manager::orb_check(int handle, bool *updated)
{
	/* Set to false here so that if `ioctl` fails to false. */
	*updated = false;
	return ioctl(handle, ORBIOCUPDATED, (unsigned long)(uintptr_t)updated);
}

int uORB::Manager::orb_stat(int handle, uint64_t *time)
{
	return ioctl(handle, ORBIOCLASTUPDATE, (unsigned long)(uintptr_t)time);
}

int uORB::Manager::orb_priority(int handle, int32_t *priority)
{
	return ioctl(handle, ORBIOCGPRIORITY, (unsigned long)(uintptr_t)priority);
}

int uORB::Manager::orb_set_interval(int handle, unsigned interval)
{
	return ioctl(handle, ORBIOCSETINTERVAL, interval * 1000);
}

int uORB::Manager::orb_get_interval(int handle, unsigned *interval)
{
	int ret = ioctl(handle, ORBIOCGETINTERVAL, (unsigned long)interval);
	*interval /= 1000;
	return ret;
}

int uORB::Manager::node_advertise(const struct orb_metadata *meta, int *instance, int priority)
{
	int fd = -1;
	int ret = -1;

	/* fill advertiser data */
	const struct orb_advertdata adv = { meta, instance, priority };

	/* open the control device */
	fd = open(TOPIC_MASTER_DEVICE_PATH, 0);

	if (fd < 0) {
		goto out;
	}

	/* advertise the object */
	ret = ioctl(fd, ORBIOCADVERTISE, (unsigned long)(uintptr_t)&adv);

	/* it's OK if it already exists */
	if ((OK != ret) && (EEXIST == errno)) {
		ret = OK;
	}

out:

	if (fd >= 0) {
		close(fd);
	}

	return ret;
}

int uORB::Manager::node_open(const struct orb_metadata *meta, const void *data, bool advertiser, int *instance,
			     int priority)
{
	char path[orb_maxpath];
	int fd = -1, ret;

	/*
	 * If meta is null, the object was not defined, i.e. it is not
	 * known to the system.  We can't advertise/subscribe such a thing.
	 */
	if (nullptr == meta) {
		errno = ENOENT;
		return -1;
	}

	/*
	 * Advertiser must publish an initial value.
	 */
	if (advertiser && (data == nullptr)) {
		errno = EINVAL;
		return -1;
	}

	/* if we have an instance and are an advertiser, we will generate a new node and set the instance,
	 * so we do not need to open here */
	if (!instance || !advertiser) {
		/*
		 * Generate the path to the node and try to open it.
		 */
		ret = uORB::Utils::node_mkpath(path, meta, instance);

		if (ret != OK) {
			errno = -ret;
			return -1;
		}

		/* open the path as either the advertiser or the subscriber */
		fd = open(path, advertiser ? O_WRONLY : O_RDONLY);

	} else {
		*instance = 0;
	}

	/* we may need to advertise the node... */
	if (fd < 0) {

		/* try to create the node */
		ret = node_advertise(meta, instance, priority);

		if (ret == OK) {
			/* update the path, as it might have been updated during the node_advertise call */
			ret = uORB::Utils::node_mkpath(path, meta, instance);

			if (ret != OK) {
				errno = -ret;
				return -1;
			}
		}

		/* on success, try the open again */
		if (ret == OK) {
			fd = open(path, (advertiser) ? O_WRONLY : O_RDONLY);
		}
	}

	/*
	 else if (advertiser) {
		 * We have a valid fd and are an advertiser.
		 * This can happen if the topic is already subscribed/published, and orb_advertise() is called,
		 * where instance==nullptr.
		 * We would need to set the priority here (via ioctl(fd, ...) and a new IOCTL), but orb_advertise()
		 * uses ORB_PRIO_DEFAULT, and a subscriber also creates the node with ORB_PRIO_DEFAULT. So we don't need
		 * to do anything here.
	 }
	 */

	if (fd < 0) {
		errno = EIO;
		return -1;
	}

	/* everything has been OK, we can return the handle now */
	return fd;
}

#ifdef ORB_COMMUNICATOR
void uORB::Manager::set_uorb_communicator(uORBCommunicator::IChannel *channel)
{
	_comm_channel = channel;

	if (_comm_channel != nullptr) {
		_comm_channel->register_handler(this);
	}
}

uORBCommunicator::IChannel *uORB::Manager::get_uorb_communicator()
{
	return _comm_channel;
}

int16_t uORB::Manager::process_remote_topic(const char *topic_name, bool isAdvertisement)
{
	int16_t rc = 0;

	if (isAdvertisement) {
		_remote_topics.insert(topic_name);

	} else {
		_remote_topics.erase(topic_name);
	}

	return rc;
}

int16_t uORB::Manager::process_add_subscription(const char *messageName, int32_t msgRateInHz)
{
	syslog(LOG_DEBUG,"entering Manager_process_add_subscription: name: %s\n", messageName);

	int16_t rc = 0;
	_remote_subscriber_topics.insert(messageName);
	char nodepath[orb_maxpath];
	int ret = uORB::Utils::node_mkpath(nodepath, messageName);
	DeviceMaster *device_master = get_device_master();

	if (ret == OK && device_master) {
		uORB::DeviceNode *node = device_master->getDeviceNode(nodepath);

		if (node == nullptr) {
			syslog(LOG_DEBUG,"DeviceNode(%s) not created yet\n", messageName);

		} else {
			// node is present.
			node->process_add_subscription(msgRateInHz);
		}

	} else {
		rc = -1;
	}

	return rc;
}

int16_t uORB::Manager::process_remove_subscription(const char *messageName)
{
	int16_t rc = -1;
	_remote_subscriber_topics.erase(messageName);
	char nodepath[orb_maxpath];
	int ret = uORB::Utils::node_mkpath(nodepath, messageName);
	DeviceMaster *device_master = get_device_master();

	if (ret == OK && device_master) {
		uORB::DeviceNode *node = device_master->getDeviceNode(nodepath);

		// get the node name.
		if (node == nullptr) {
			syslog(LOG_DEBUG,"[posix-uORB::Manager::process_remove_subscription(%d)]Error No existing subscriber found for message: [%s]\n",
				  __LINE__, messageName);

		} else {
			// node is present.
			node->process_remove_subscription();
			rc = 0;
		}
	}

	return rc;
}

int16_t uORB::Manager::process_received_message(const char *messageName, int32_t length, uint8_t *data)
{
	int16_t rc = -1;
	char nodepath[orb_maxpath];
	int ret = uORB::Utils::node_mkpath(nodepath, messageName);
	DeviceMaster *device_master = get_device_master();

	if (ret == OK && device_master) {
		uORB::DeviceNode *node = device_master->getDeviceNode(nodepath);

		// get the node name.
		if (node == nullptr) {
			syslog(LOG_DEBUG,"No existing subscriber found for message: [%s] nodepath:[%s]\n", messageName, nodepath);

		} else {
			// node is present.
			node->process_received_message(length, data);
			rc = 0;
		}
	}

	return rc;
}

bool uORB::Manager::is_remote_subscriber_present(const char *messageName)
{
	return _remote_subscriber_topics.find(messageName);
}
#endif /* ORB_COMMUNICATOR */

#ifdef ORB_USE_PUBLISHER_RULES

bool uORB::Manager::startsWith(const char *pre, const char *str)
{
	size_t lenpre = strlen(pre),
	       lenstr = strlen(str);
	return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

bool uORB::Manager::findTopic(const PublisherRule &rule, const char *topic_name)
{
	const char **topics_ptr = rule.topics;

	while (*topics_ptr) {
		if (strcmp(*topics_ptr, topic_name) == 0) {
			return true;
		}

		++topics_ptr;
	}

	return false;
}

void uORB::Manager::strTrim(const char **str)
{
	while (**str == ' ' || **str == '\t') { ++(*str); }
}

int uORB::Manager::readPublisherRulesFromFile(const char *file_name, PublisherRule &rule)
{
	FILE *fp;
	static const int line_len = 1024;
	int ret = OK;
	char *line = new char[line_len];

	if (!line) {
		return -ENOMEM;
	}

	fp = fopen(file_name, "r");

	if (fp == NULL) {
		delete[](line);
		return -errno;
	}

	const char *restrict_topics_str = "restrict_topics:";
	const char *module_str = "module:";
	const char *ignore_others = "ignore_others:";

	rule.ignore_other_topics = false;
	rule.module_name = nullptr;
	rule.topics = nullptr;

	while (fgets(line, line_len, fp) && ret == OK) {

		if (strlen(line) < 2 || line[0] == '#') {
			continue;
		}

		if (startsWith(restrict_topics_str, line)) {
			//read topics list
			char *start = line + strlen(restrict_topics_str);
			strTrim((const char **)&start);
			char *topics = strdup(start);
			int topic_len = 0, num_topics = 0;

			for (int i = 0; topics[i]; ++i) {
				if (topics[i] == ',' || topics[i] == '\n') {
					if (topic_len > 0) {
						topics[i] = 0;
						++num_topics;
					}

					topic_len = 0;

				} else {
					++topic_len;
				}
			}

			if (num_topics > 0) {
				rule.topics = new const char *[num_topics + 1];
				int topic = 0;
				strTrim((const char **)&topics);
				rule.topics[topic++] = topics;

				while (topic < num_topics) {
					if (*topics == 0) {
						++topics;
						strTrim((const char **)&topics);
						rule.topics[topic++] = topics;

					} else {
						++topics;
					}
				}

				rule.topics[num_topics] = nullptr;
			}

		} else if (startsWith(module_str, line)) {
			//read module name
			char *start = line + strlen(module_str);
			strTrim((const char **)&start);
			int len = strlen(start);

			if (len > 0 && start[len - 1] == '\n') {
				start[len - 1] = 0;
			}

			rule.module_name = strdup(start);

		} else if (startsWith(ignore_others, line)) {
			const char *start = line + strlen(ignore_others);
			strTrim(&start);

			if (startsWith("true", start)) {
				rule.ignore_other_topics = true;
			}

		} else {
			syslog(LOG_ERR,"orb rules file: wrong format: %s\n", line);
			ret = -EINVAL;
		}
	}

	if (ret == OK && (!rule.module_name || !rule.topics)) {
		syslog(LOG_ERR,"Wrong format in orb publisher rules file\n");
		ret = -EINVAL;
	}

	delete[](line);
	fclose(fp);
	return ret;
}
#endif /* ORB_USE_PUBLISHER_RULES */
