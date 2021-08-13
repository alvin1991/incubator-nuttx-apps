/****************************************************************************
 *
 *   Copyright (c) 2012-2016 PX4 Development Team. All rights reserved.
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdlib.h>

#define FILE_FLAGS(filp) filp->f_oflags
#define FILE_PRIV(filp) filp->f_priv
#define ITERATE_NODE_MAP() \
	for (ORBMap::Node *node_iter = _node_map.top(); node_iter; node_iter = node_iter->next)
#define INIT_NODE_MAP_VARS(node_obj, node_name_str) \
	DeviceNode *node_obj = node_iter->node; \
	const char *node_name_str = node_iter->node_name; \
	UNUSED(node_name_str);

#include "uORB/cdev/cdev_platform.hpp"

#include "uORB/orb/uORBDevices.hpp"
#include "uORB/orb/uORBUtils.hpp"
#include "uORB/orb/uORBManager.hpp"

#ifdef ORB_COMMUNICATOR
#include "uORB/orb/uORBCommunicator.hpp"
#endif /* ORB_COMMUNICATOR */



using namespace device;

uORB::DeviceNode::SubscriberData *uORB::DeviceNode::filp_to_sd(device::file_t *filp)
{
	return (SubscriberData *)(FILE_PRIV(filp));
}

uORB::DeviceNode::DeviceNode(const struct orb_metadata *meta, const char *name, const char *path,
			     int priority, unsigned int queue_size) :
	CDev(name, path),
	_meta(meta),
	_priority((uint8_t)priority),
	_queue_size(queue_size)
{
}

uORB::DeviceNode::~DeviceNode()
{
	if (_data != nullptr) {
		delete[] _data;
	}

}

int
uORB::DeviceNode::open(device::file_t *filp)
{
	int ret;

	/* is this a publisher? */
	if (FILE_FLAGS(filp) == O_WRONLY) {

		/* become the publisher if we can */
		lock();

		if (_publisher == 0) {
			_publisher = getpid();
			ret = OK;

		} else {
			ret = -EBUSY;
		}

		unlock();

		/* now complete the open */
		if (ret == OK) {
			ret = CDev::open(filp);

			/* open failed - not the publisher anymore */
			if (ret != OK) {
				_publisher = 0;
			}
		}

		return ret;
	}

	/* is this a new subscriber? */
	if (FILE_FLAGS(filp) == O_RDONLY) {

		/* allocate subscriber data */
		SubscriberData *sd = new SubscriberData{};

		if (nullptr == sd) {
			return -ENOMEM;
		}

		/* If queue size >1, allow the subscriber to read the data in the queue. Otherwise, assume subscriber is up to date.*/
		if (_queue_size <= 1) {
			sd->generation = _generation;

		} else {
			sd->generation = _generation - (_queue_size < _generation ? _queue_size : _generation);
		}

		/* set priority */
		sd->set_priority(_priority);

		FILE_PRIV(filp) = (void *)sd;

		ret = CDev::open(filp);

		add_internal_subscriber();

		if (ret != OK) {
			syslog(LOG_ERR,"CDev::open failed\n");
			delete sd;
		}

		return ret;
	}

	if (FILE_FLAGS(filp) == 0) {
		return CDev::open(filp);
	}

	/* can only be pub or sub, not both */
	return -EINVAL;
}

int
uORB::DeviceNode::close(device::file_t *filp)
{
	/* is this the publisher closing? */
	if (getpid() == _publisher) {
		_publisher = 0;

	} else {
		SubscriberData *sd = filp_to_sd(filp);

		if (sd != nullptr) {
			if (sd->update_interval) {
				hrt_cancel(&sd->update_interval->update_call);
			}

			remove_internal_subscriber();

			delete sd;
			sd = nullptr;
		}
	}

	return CDev::close(filp);
}

ssize_t
uORB::DeviceNode::read(device::file_t *filp, char *buffer, size_t buflen)
{
	SubscriberData *sd = (SubscriberData *)filp_to_sd(filp);

	/* if the object has not been written yet, return zero */
	if (_data == nullptr) {
		return 0;
	}

	/* if the caller's buffer is the wrong size, that's an error */
	if (buflen != _meta->o_size) {
		return -EIO;
	}

	/*
	 * Perform an atomic copy & state update
	 */
	ATOMIC_ENTER;

	if (_generation > sd->generation + _queue_size) {
		/* Reader is too far behind: some messages are lost */
		_lost_messages += _generation - (sd->generation + _queue_size);
		sd->generation = _generation - _queue_size;
	}

	if (_generation == sd->generation && sd->generation > 0) {
		/* The subscriber already read the latest message, but nothing new was published yet.
		 * Return the previous message
		 */
		--sd->generation;
	}

	/* if the caller doesn't want the data, don't give it to them */
	if (nullptr != buffer) {
		memcpy(buffer, _data + (_meta->o_size * (sd->generation % _queue_size)), _meta->o_size);
	}

	if (sd->generation < _generation) {
		++sd->generation;
	}

	/* set priority */
	sd->set_priority(_priority);

	/*
	 * Clear the flag that indicates that an update has been reported, as
	 * we have just collected it.
	 */
	sd->set_update_reported(false);

	ATOMIC_LEAVE;

	return _meta->o_size;
}

ssize_t
uORB::DeviceNode::write(device::file_t *filp, const char *buffer, size_t buflen)
{
	/*
	 * Writes are legal from interrupt context as long as the
	 * object has already been initialised from thread context.
	 *
	 * Writes outside interrupt context will allocate the object
	 * if it has not yet been allocated.
	 *
	 * Note that filp will usually be NULL.
	 */
	if (nullptr == _data) {

		if (!up_interrupt_context()) {

			lock();

			/* re-check size */
			if (nullptr == _data) {
				_data = new uint8_t[_meta->o_size * _queue_size];
			}

			unlock();
		}



		/* failed or could not allocate */
		if (nullptr == _data) {
			return -ENOMEM;
		}
	}

	/* If write size does not match, that is an error */
	if (_meta->o_size != buflen) {
		return -EIO;
	}

	/* Perform an atomic copy. */
	ATOMIC_ENTER;
	memcpy(_data + (_meta->o_size * (_generation % _queue_size)), buffer, _meta->o_size);

	/* update the timestamp and generation count */
	_last_update = hrt_absolute_time();
	/* wrap-around happens after ~49 days, assuming a publisher rate of 1 kHz */
	_generation++;

	_published = true;

	ATOMIC_LEAVE;

	/* notify any poll waiters */
	poll_notify(POLLIN);

	return _meta->o_size;
}

int
uORB::DeviceNode::ioctl(device::file_t *filp, int cmd, unsigned long arg)
{
	SubscriberData *sd = filp_to_sd(filp);

	switch (cmd) {
	case ORBIOCLASTUPDATE: {
			ATOMIC_ENTER;
			*(hrt_abstime *)arg = _last_update;
			ATOMIC_LEAVE;
			return OK;
		}

	case ORBIOCUPDATED:

		*(bool *)arg = appears_updated(sd);
		return OK;

	case ORBIOCSETINTERVAL: {
			int ret = OK;
			lock();

			if (arg == 0) {
				if (sd->update_interval) {
					delete (sd->update_interval);
					sd->update_interval = nullptr;
				}

			} else {
				if (sd->update_interval) {
					sd->update_interval->interval = arg;
					sd->update_interval->last_update = hrt_absolute_time();

				} else {
					sd->update_interval = new UpdateIntervalData();

					if (sd->update_interval) {
						memset(&sd->update_interval->update_call, 0, sizeof(hrt_call));
						sd->update_interval->interval = arg;
						sd->update_interval->last_update = hrt_absolute_time();

					} else {
						ret = -ENOMEM;
					}
				}
			}

			unlock();
			return ret;
		}

	case ORBIOCGADVERTISER:
		*(uintptr_t *)arg = (uintptr_t)this;
		return OK;

	case ORBIOCGPRIORITY:
		*(int *)arg = sd->priority();
		return OK;

	case ORBIOCSETQUEUESIZE:
		//no need for locking here, since this is used only during the advertisement call,
		//and only one advertiser is allowed to open the DeviceNode at the same time.
		return update_queue_size(arg);

	case ORBIOCGETINTERVAL:
		if (sd->update_interval) {
			*(unsigned *)arg = sd->update_interval->interval;

		} else {
			*(unsigned *)arg = 0;
		}

		return OK;

	case ORBIOCISPUBLISHED:
		*(unsigned long *)arg = _published;

		return OK;

	default:
		/* give it to the superclass */
		return CDev::ioctl(filp, cmd, arg);
	}
}

ssize_t
uORB::DeviceNode::publish(const orb_metadata *meta, orb_advert_t handle, const void *data)
{
	uORB::DeviceNode *devnode = (uORB::DeviceNode *)handle;
	int ret;

	/* check if the device handle is initialized */
	if ((devnode == nullptr) || (meta == nullptr)) {
		errno = EFAULT;
		return -1;
	}

	/* check if the orb meta data matches the publication */
	if (devnode->_meta != meta) {
		errno = EINVAL;
		return -1;
	}

	/* call the devnode write method with no file pointer */
	ret = devnode->write(nullptr, (const char *)data, meta->o_size);

	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	if (ret != (int)meta->o_size) {
		errno = EIO;
		return -1;
	}

#ifdef ORB_COMMUNICATOR
	/*
	 * if the write is successful, send the data over the Multi-ORB link
	 */
	uORBCommunicator::IChannel *ch = uORB::Manager::get_instance()->get_uorb_communicator();

	if (ch != nullptr) {
		if (ch->send_message(meta->o_name, meta->o_size, (uint8_t *)data) != 0) {
			syslog(LOG_ERR,("Error Sending [%s] topic data over comm_channel\n", meta->o_name);
			return -1;
		}
	}

#endif /* ORB_COMMUNICATOR */

	return OK;
}

int uORB::DeviceNode::unadvertise(orb_advert_t handle)
{
	if (handle == nullptr) {
		return -EINVAL;
	}

	uORB::DeviceNode *devnode = (uORB::DeviceNode *)handle;

	/*
	 * We are cheating a bit here. First, with the current implementation, we can only
	 * have multiple publishers for instance 0. In this case the caller will have
	 * instance=nullptr and _published has no effect at all. Thus no unadvertise is
	 * necessary.
	 * In case of multiple instances, we have at most 1 publisher per instance and
	 * we can signal an instance as 'free' by setting _published to false.
	 * We never really free the DeviceNode, for this we would need reference counting
	 * of subscribers and publishers. But we also do not have a leak since future
	 * publishers reuse the same DeviceNode object.
	 */
	devnode->_published = false;

	return OK;
}

#ifdef ORB_COMMUNICATOR
int16_t uORB::DeviceNode::topic_advertised(const orb_metadata *meta, int priority)
{
	uORBCommunicator::IChannel *ch = uORB::Manager::get_instance()->get_uorb_communicator();

	if (ch != nullptr && meta != nullptr) {
		return ch->topic_advertised(meta->o_name);
	}

	return -1;
}

/*
//TODO: Check if we need this since we only unadvertise when things all shutdown and it doesn't actually remove the device
int16_t uORB::DeviceNode::topic_unadvertised(const orb_metadata *meta, int priority)
{
	uORBCommunicator::IChannel *ch = uORB::Manager::get_instance()->get_uorb_communicator();
	if (ch != nullptr && meta != nullptr) {
		return ch->topic_unadvertised(meta->o_name);
	}
	return -1;
}
*/
#endif /* ORB_COMMUNICATOR */

pollevent_t
uORB::DeviceNode::poll_state(device::file_t *filp)
{
	SubscriberData *sd = filp_to_sd(filp);

	/*
	 * If the topic appears updated to the subscriber, say so.
	 */
	if (appears_updated(sd)) {
		return POLLIN;
	}

	return 0;
}

void
uORB::DeviceNode::poll_notify_one(struct pollfd *fds, short events)
{
	SubscriberData *sd = filp_to_sd((device::file_t *)fds->priv);

	/*
	 * If the topic looks updated to the subscriber, go ahead and notify them.
	 */
	if (appears_updated(sd)) {
		CDev::poll_notify_one(fds, events);
	}
}

bool
uORB::DeviceNode::appears_updated(SubscriberData *sd)
{
	/* assume it doesn't look updated */
	bool ret = false;

	/* avoid racing between interrupt and non-interrupt context calls */
	irqstate_t state = enter_critical_section();

	/* check if this topic has been published yet, if not bail out */
	if (_data == nullptr) {
		ret = false;
		goto out;
	}

	/*
	 * If the subscriber's generation count matches the update generation
	 * count, there has been no update from their perspective; if they
	 * don't match then we might have a visible update.
	 */
	while (sd->generation != _generation) {

		/*
		 * Handle non-rate-limited subscribers.
		 */
		if (sd->update_interval == nullptr) {
			ret = true;
			break;
		}

		/*
		 * If we have previously told the subscriber that there is data,
		 * and they have not yet collected it, continue to tell them
		 * that there has been an update.  This mimics the non-rate-limited
		 * behaviour where checking / polling continues to report an update
		 * until the topic is read.
		 */
		if (sd->update_reported()) {
			ret = true;
			break;
		}

		/*
		 * If the interval timer is still running, the topic should not
		 * appear updated, even though at this point we know that it has.
		 * We have previously been through here, so the subscriber
		 * must have collected the update we reported, otherwise
		 * update_reported would still be true.
		 */
		if (!hrt_called(&sd->update_interval->update_call)) {
			break;
		}

		/*
		 * Make sure that we don't consider the topic to be updated again
		 * until the interval has passed once more by restarting the interval
		 * timer and thereby re-scheduling a poll notification at that time.
		 */
		hrt_call_after(&sd->update_interval->update_call,
			       sd->update_interval->interval,
			       &uORB::DeviceNode::update_deferred_trampoline,
			       (void *)this);

		/*
		 * Remember that we have told the subscriber that there is data.
		 */
		sd->set_update_reported(true);
		ret = true;

		break;
	}

out:
	leave_critical_section(state);

	/* consider it updated */
	return ret;
}


void
uORB::DeviceNode::update_deferred()
{
	/*
	 * Instigate a poll notification; any subscribers whose intervals have
	 * expired will be woken.
	 */
	poll_notify(POLLIN);
}

void
uORB::DeviceNode::update_deferred_trampoline(void *arg)
{
	uORB::DeviceNode *node = (uORB::DeviceNode *)arg;

	node->update_deferred();
}

bool
uORB::DeviceNode::print_statistics(bool reset)
{
	if (!_lost_messages) {
		return false;
	}

	lock();
	//This can be wrong: if a reader never reads, _lost_messages will not be increased either
	uint32_t lost_messages = _lost_messages;

	if (reset) {
		_lost_messages = 0;
	}

	unlock();

	syslog(LOG_INFO,"%s: %ld\n", _meta->o_name, lost_messages);
	return true;
}

void uORB::DeviceNode::add_internal_subscriber()
{
	lock();
	_subscriber_count++;

#ifdef ORB_COMMUNICATOR
	uORBCommunicator::IChannel *ch = uORB::Manager::get_instance()->get_uorb_communicator();

	if (ch != nullptr && _subscriber_count > 0) {
		unlock(); //make sure we cannot deadlock if add_subscription calls back into DeviceNode
		ch->add_subscription(_meta->o_name, 1);

	} else
#endif /* ORB_COMMUNICATOR */

	{
		unlock();
	}
}

void uORB::DeviceNode::remove_internal_subscriber()
{
	lock();
	_subscriber_count--;

#ifdef ORB_COMMUNICATOR
	uORBCommunicator::IChannel *ch = uORB::Manager::get_instance()->get_uorb_communicator();

	if (ch != nullptr && _subscriber_count == 0) {
		unlock(); //make sure we cannot deadlock if remove_subscription calls back into DeviceNode
		ch->remove_subscription(_meta->o_name);

	} else
#endif /* ORB_COMMUNICATOR */
	{
		unlock();
	}
}

#ifdef ORB_COMMUNICATOR
int16_t uORB::DeviceNode::process_add_subscription(int32_t rateInHz)
{
	// if there is already data in the node, send this out to
	// the remote entity.
	// send the data to the remote entity.
	uORBCommunicator::IChannel *ch = uORB::Manager::get_instance()->get_uorb_communicator();

	if (_data != nullptr && ch != nullptr) { // _data will not be null if there is a publisher.
		ch->send_message(_meta->o_name, _meta->o_size, _data);
	}

	return OK;
}

int16_t uORB::DeviceNode::process_remove_subscription()
{
	return OK;
}

int16_t uORB::DeviceNode::process_received_message(int32_t length, uint8_t *data)
{
	int16_t ret = -1;

	if (length != (int32_t)(_meta->o_size)) {
		syslog(LOG_ERR,("Received DataLength[%d] != ExpectedLen[%d]\n", _meta->o_name, (int)length, (int)_meta->o_size);
		return syslog(LOG_ERR,OR;
	}

	/* call the devnode write method with no file pointer */
	ret = write(nullptr, (const char *)data, _meta->o_size);

	if (ret < 0) {
		return syslog(LOG_ERR,OR;
	}

	if (ret != (int)_meta->o_size) {
		errno = EIO;
		return syslog(LOG_ERR,OR;
	}

	return OK;
}
#endif /* ORB_COMMUNICATOR */

int uORB::DeviceNode::update_queue_size(unsigned int queue_size)
{
	if (_queue_size == queue_size) {
		return OK;
	}

	//queue size is limited to 255 for the single reason that we use uint8 to store it
	if (_data || _queue_size > queue_size || queue_size > 255) {
		return -1;
	}

	_queue_size = queue_size;
	return OK;
}

uORB::DeviceMaster::DeviceMaster() :
	CDev("obj_master", TOPIC_MASTER_DEVICE_PATH)
{
	_last_statistics_output = hrt_absolute_time();
}

int
uORB::DeviceMaster::ioctl(device::file_t *filp, int cmd, unsigned long arg)
{
	int ret;

	switch (cmd) {
	case ORBIOCADVERTISE: {
			const struct orb_advertdata *adv = (const struct orb_advertdata *)arg;
			const struct orb_metadata *meta = adv->meta;
			char nodepath[orb_maxpath];

			/* construct a path to the node - this also checks the node name */
			ret = uORB::Utils::node_mkpath(nodepath, meta, adv->instance);

			if (ret != OK) {
				return ret;
			}

			ret = -1;

			/* try for topic groups */
			const unsigned max_group_tries = (adv->instance != nullptr) ? ORB_MULTI_MAX_INSTANCES : 1;
			unsigned group_tries = 0;

			if (adv->instance) {
				/* for an advertiser, this will be 0, but a for subscriber that requests a certain instance,
				 * we do not want to start with 0, but with the instance the subscriber actually requests.
				 */
				group_tries = *adv->instance;

				if (group_tries >= max_group_tries) {
					return -ENOMEM;
				}
			}

			//SmartLock smart_lock(_lock);

			lock();

			do {
				/* if path is modifyable change try index */
				if (adv->instance != nullptr) {
					/* replace the number at the end of the string */
					nodepath[strlen(nodepath) - 1] = '0' + group_tries;
					*(adv->instance) = group_tries;
				}

				const char *objname = meta->o_name; //no need for a copy, meta->o_name will never be freed or changed

				/* driver wants a permanent copy of the path, so make one here */
				const char *devpath = strdup(nodepath);

				if (devpath == nullptr) {
					return -ENOMEM;
				}

				/* construct the new node */
				uORB::DeviceNode *node = new uORB::DeviceNode(meta, objname, devpath, adv->priority);

				/* if we didn't get a device, that's bad */
				if (node == nullptr) {
					free((void *)devpath);
					return -ENOMEM;
				}

				/* initialise the node - this may fail if e.g. a node with this name already exists */
				ret = node->init();

				/* if init failed, discard the node and its name */
				if (ret != OK) {
					delete node;

					if (ret == -EEXIST) {
						/* if the node exists already, get the existing one and check if
						 * something has been published yet. */
						uORB::DeviceNode *existing_node = getDeviceNodeLocked(devpath);

						if ((existing_node != nullptr) && !(existing_node->is_published())) {
							/* nothing has been published yet, lets claim it */
							existing_node->set_priority(adv->priority);
							ret = OK;

						} else {
							/* otherwise: data has already been published, keep looking */
						}
					}

					/* also discard the name now */
					free((void *)devpath);

				} else {
					// add to the node map;.
					_node_map.insert(devpath, node);
				}

				group_tries++;

			} while (ret != OK && (group_tries < max_group_tries));

			if (ret != OK && group_tries >= max_group_tries) {
				ret = -ENOMEM;
			}

			unlock();
			return ret;
		}

	nxsem_post(&_lock);
	default:
		/* give it to the superclass */
		return CDev::ioctl(filp, cmd, arg);
	}
}

void uORB::DeviceMaster::printStatistics(bool reset)
{
	hrt_abstime current_time = hrt_absolute_time();
	syslog(LOG_INFO,"Statistics, since last output (%i ms):",
		 (int)((current_time - _last_statistics_output) / 1000));
	_last_statistics_output = current_time;

	syslog(LOG_INFO,"TOPIC, NR LOST MSGS\n");
	bool had_print = false;

	lock();
	ITERATE_NODE_MAP() {
		INIT_NODE_MAP_VARS(node, node_name)

		if (node->print_statistics(reset)) {
			had_print = true;
		}
	}

	unlock();

	if (!had_print) {
		syslog(LOG_INFO,"No lost messages\n");
	}
}

void uORB::DeviceMaster::addNewDeviceNodes(DeviceNodeStatisticsData **first_node, int &num_topics,
		size_t &max_topic_name_length,
		char **topic_filter, int num_filters)
{
	DeviceNodeStatisticsData *cur_node;
	num_topics = 0;
	DeviceNodeStatisticsData *last_node = *first_node;

	if (last_node) {
		while (last_node->next) {
			last_node = last_node->next;
		}
	}


	ITERATE_NODE_MAP() {
		INIT_NODE_MAP_VARS(node, node_name)
		++num_topics;

		//check if already added
		cur_node = *first_node;

		while (cur_node && cur_node->node != node) {
			cur_node = cur_node->next;
		}

		if (cur_node) {
			continue;
		}

		if (num_filters > 0 && topic_filter) {
			bool matched = false;

			for (int i = 0; i < num_filters; ++i) {
				if (strstr(node->get_meta()->o_name, topic_filter[i])) {
					matched = true;
				}
			}

			if (!matched) {
				continue;
			}
		}

		if (last_node) {
			last_node->next = new DeviceNodeStatisticsData();
			last_node = last_node->next;

		} else {
			*first_node = last_node = new DeviceNodeStatisticsData();
		}

		if (!last_node) {
			syslog(LOG_ERR,"mem alloc failed\n");
			break;
		}

		last_node->node = node;
		int node_name_len = strlen(node_name);
		last_node->instance = (uint8_t)(node_name[node_name_len - 1] - '0');
		size_t name_length = strlen(last_node->node->get_meta()->o_name);

		if (name_length > max_topic_name_length) {
			max_topic_name_length = name_length;
		}

		last_node->last_lost_msg_count = last_node->node->lost_message_count();
		last_node->last_pub_msg_count = last_node->node->published_message_count();
	}
}

#define CLEAR_LINE "\033[K"

void uORB::DeviceMaster::showTop(char **topic_filter, int num_filters)
{

	bool print_active_only = true;

	if (topic_filter && num_filters > 0) {
		if (!strcmp("-a", topic_filter[0])) {
			num_filters = 0;
		}

		print_active_only = false; // print non-active if -a or some filter given
	}

	printf("\033[2J\n"); //clear screen

	lock();

	if (_node_map.empty()) {
		unlock();
		syslog(LOG_INFO,"no active topics\n");
		return;
	}

	DeviceNodeStatisticsData *first_node = nullptr;
	DeviceNodeStatisticsData *cur_node = nullptr;
	size_t max_topic_name_length = 0;
	int num_topics = 0;
	addNewDeviceNodes(&first_node, num_topics, max_topic_name_length, topic_filter, num_filters);

	/* a DeviceNode is never deleted, so it's save to unlock here and still access the DeviceNodes */
	unlock();


	const int stdin_fileno = 0;

	struct pollfd fds;
	fds.fd = stdin_fileno;
	fds.events = POLLIN;

	bool quit = false;

	hrt_abstime start_time = hrt_absolute_time();

	while (!quit) {


		/* Sleep 200 ms waiting for user input five times ~ 1s */
		for (int k = 0; k < 5; k++) {
			char c;

			int ret = ::poll(&fds, 1, 0); //just want to check if there is new data available

			if (ret > 0) {

				ret = ::read(stdin_fileno, &c, 1);

				if (ret) {
					quit = true;
					break;
				}
			}

			usleep(200000);
		}

		if (!quit) {

			//update the stats
			hrt_abstime current_time = hrt_absolute_time();
			float dt = (current_time - start_time) / 1.e6f;
			cur_node = first_node;

			while (cur_node) {
				uint32_t num_lost = cur_node->node->lost_message_count();
				unsigned int num_msgs = cur_node->node->published_message_count();
				cur_node->pub_msg_delta = (num_msgs - cur_node->last_pub_msg_count) / dt;
				cur_node->lost_msg_delta = (num_lost - cur_node->last_lost_msg_count) / dt;
				cur_node->last_lost_msg_count = num_lost;
				cur_node->last_pub_msg_count = num_msgs;
				cur_node = cur_node->next;
			}

			start_time = current_time;


			printf("\033[H"); // move cursor home and clear screen
			printf(CLEAR_LINE "update: 1s, num topics: %i\n", num_topics);
			printf(CLEAR_LINE "%*s INST #SUB #MSG #LOST #QSIZE\n", (int)max_topic_name_length - 2, "TOPIC NAME");

			cur_node = first_node;

			while (cur_node) {

				if (!print_active_only || cur_node->pub_msg_delta > 0) {
					printf(CLEAR_LINE "%*s %2d %4d %4d %5d %d\n", (int)max_topic_name_length,

					       cur_node->node->get_meta()->o_name, (int)cur_node->instance,
					       (int)cur_node->node->subscriber_count(), cur_node->pub_msg_delta,
					       (int)cur_node->lost_msg_delta, cur_node->node->get_queue_size());
				}

				cur_node = cur_node->next;
			}

			lock();
			addNewDeviceNodes(&first_node, num_topics, max_topic_name_length, topic_filter, num_filters);
			unlock();
		}
	}

	//cleanup
	cur_node = first_node;

	while (cur_node) {
		DeviceNodeStatisticsData *next_node = cur_node->next;
		delete cur_node;
		cur_node = next_node;
	}
}

#undef CLEAR_LINE

uORB::DeviceNode *uORB::DeviceMaster::getDeviceNode(const char *nodepath)
{
	lock();
	uORB::DeviceNode *node = getDeviceNodeLocked(nodepath);
	unlock();
	//We can safely return the node that can be used by any thread, because
	//a DeviceNode never gets deleted.
	return node;
}


uORB::DeviceNode *uORB::DeviceMaster::getDeviceNodeLocked(const char *nodepath)
{
	uORB::DeviceNode *rc = nullptr;

	if (_node_map.find(nodepath)) {
		rc = _node_map.get(nodepath);
	}

	return rc;
}

