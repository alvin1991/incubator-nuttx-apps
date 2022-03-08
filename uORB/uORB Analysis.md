## uORB微消息代理器
------------------
#### 1 结构框架
整个uORB的框架层级如下图所示:<br>
`PS:这个只是包含了最基础uORB的模块，远程功能（remote）等其他功能未列出`<br>

::: mermaid
graph LR;
    uORB-.-DeviceNode;
    uORB-.-DeviceMaster;
    uORB-.-Manager;
    uORB-.-Utils;
    DeviceNode-.-CDev;
    CDev-.-Device;
    DeviceMaster-.-CDev; 
::: 

#### 2 模块分析

##### 2.1 class Device 

&nbsp; &nbsp; &nbsp; &nbsp;class Device面向的对象是（硬件）设备，对总线设备进行了抽象，并定义了接口方法。<br>
`总线类型：`<br>
 - `I2C` <br>
 - `SPI` <br>
 - `UAVCAN` <br>
 - `UNKNOWN` <br>
  
`接口方法：`<br>
 - `init(),抽象接口，由设备实例实现初始化` <br>
 - `read()，抽象接口，由设备实例实现数据读取` <br>
 - `write(),抽象接口，由设备实例实现数据写入` <br>
 - `ioctl(),抽象接口，由设备实例实现IO控制` <br>
 - `set_device_type(),方法，设置该设备总线类型` <br>
 - `set_device_address(),方法，设置设备总线地址` <br>
 - `get_device_bus(),方法，获取设备总线，同一类型的总线，片上可能存在多条eg:I2C1、I2C2.` <br>
 - `get_device_bus_type(),方法，获取设备总线类型` <br>
 - `get_device_address(),方法，获取设备地址` <br>

##### 2.2 class CDev
&nbsp; &nbsp; &nbsp; &nbsp;class CDev面向的对象是设备（驱动）公共继承了class Device并在Device基础上进一步抽象了部分接口，同时定义了设备注册、信号锁、轮询等抽象接口<br>
`公有接口方法：`<br>
 - `init(),抽象接口，由设备实例实现初始化` <br>
 - `open(),抽象接口，由设备实例实现设备打开` <br>
 - `close(),抽象接口，由设备实例实现设备关闭` <br>  
 - `read()，抽象接口，由设备实例实现数据读取` <br>
 - `write(),抽象接口，由设备实例实现数据写入` <br>
 - `seek(),抽象接口，由设备实例实现seek控制` <br> 
 - `ioctl(),抽象接口，由设备实例实现IO控制` <br>
 - `poll(),抽象接口，由设备实例实现轮询逻辑` <br>
`私有接口方法：`<br>
 - `poll_state(),方法，获取设备当前事件状态（外部poll会根据设备事件状态进行处理）` <br>
 - `poll_notify()，方法，设备事件状态发现改变，将调用该接口通知所有poll waiter` <br>
 - `poll_notify_one(),方法，对poll_notify的内部实现` <br>
 - `open_first(),方法，设备第一次被打开时的回调函数` <br>
 - `close_last(),方法，设备最后关闭时的回调函数` <br>
 - `register_class_devname(),方法，注册设备驱动到系统` <br>
 - `unregister_class_devname(),方法，注销设备驱动` <br>
 - `get_devname(),方法，获取设备名称` <br>
 - `lock(),方法，锁定设备，任何对设备的操作将被挂起` <br>
 - `unlock(),方法，解除设备锁定，因为该设备被挂起的线程将进入就绪状态` <br>
 - `_pub_blocked(),方法，锁定设备publish功能，这个主要是针对uORB消息节点` <br>
##### 2.3 class DeviceNode
&nbsp; &nbsp; &nbsp; &nbsp;class DeviceNode面向的对象是uORB消息节点，公共继承了class CDev增加了消息发布、取消消息登记等接口<br>
`公有接口方法：`<br>
 - `open(),抽象接口，由消息实例实现设备打开` <br>
 - `close(),抽象接口，由消息实例实现设备关闭` <br>  
 - `read()，抽象接口，由消息实例实现数据读取` <br>
 - `write(),抽象接口，由消息实例实现数据写入` <br>
 - `ioctl(),抽象接口，由设备实例实现IO控制` <br>
 - `publish(),方法，发布数据到消息节点` <br> 
 - `unadvertise(),方法，取消消息节点登记` <br>
 - `add_internal_subscriber(),方法，添加subscriber到当前消息节点订阅列表` <br>
 - `remove_internal_subscriber(),方法，从当前消息节点订阅列表删除subscriber` <br>
 - `is_published(),方法，获取当前消息节点是否已经发布` <br>
 - `update_queue_size(),方法，修改消息节点队列长度` <br>
 - `set_priority(),方法，设置消息节点优先级` <br>
`私有接口方法：`<br>
 - `update_deferred(),方法，延迟消息更新（针对rate-limited subscriber）` <br>
 - `update_deferred_trampoline()，方法，通过hrt时间片来实现延迟更新` <br>
 - `appears_updated(),方法，检查消息节点是否更新` <br>

`ps:uORB是每个线程来调用这些方法实现，所以一定要从调用的线程角度来理解这些接口！！！`
##### 2.3 class DeviceMaster
&nbsp; &nbsp; &nbsp; &nbsp;class DeviceMaster面向的对象是uORB，公共继承了class CDev，主要是对uORB的消息节点进行管理<br>
`公有接口方法：`<br>
 - `getDeviceNode(),方法，格局消息名称获取消息节点` <br>
 - `printStatistics(),方法，统计每个消息节点的状态` <br>
 - `showTop(),方法，连续打印输出统计每个消息节点的状态` <br>
`私有接口方法：`<br>
 - `addNewDeviceNodes(),方法，增加一个消息节点` <br>
 - `getDeviceNodeLocked()，方法，根据消息名称获取消息节点` <br>
##### 2.4 class Manager
&nbsp; &nbsp; &nbsp; &nbsp;class Manager面向的用户，提供了使用uORB的接口，主要提供uORB的消息节点的登记、发布、订阅、查询等等<br>
`公有接口方法：`<br>
 - `initialize(),方法，初始化整个uORB框架（实例化各个类）` <br>
 - `get_instance(),方法，获取Manager的对象` <br>
 - `get_device_master(),方法，获取DeviceMaster对象` <br>
 - `orb_advertise()，方法，登记消息节点` <br> 
 - `orb_unadvertise()，方法，注销消息节点` <br> 
 - `orb_publish()，方法，发布数据到消息节点` <br> 
 - `orb_subscribe()，方法，订阅消息节点` <br> 
 - `orb_unsubscribe()，方法，退订消息节点` <br> 
 - `orb_copy()，方法，从消息节点拷贝数据` <br> 
 - `orb_check()，方法，检查消息节点是否更新` <br> 
 - `orb_stat()，方法，获取消息节点状态` <br>
 - `orb_exists()，方法，判断消息节点是否存在` <br> 
 - `orb_priority()，方法，返回消息节点优先级` <br> 
 - `orb_set_interval()，方法，设置消息节点限制时间` <br> 
 - `orb_get_interval()，方法，获取消息节点限制时间` <br> 
`私有接口方法：`<br>
 - `node_advertise()，方法，登记一个消息节点` <br> 
 - `node_open()，方法，对登记和订阅的方法实现` <br> 
#### 3 流程分析
##### 3.1框架入口
* 实例化一个Manager的类 
```c++
bool uORB::Manager::initialize()
{
	if (_Instance == nullptr) {
		_Instance = new uORB::Manager();
	}

	return _Instance != nullptr;
}
```
* 实例化DeviceMaster
  
```c++
int
CDev::init()
{
	cdevinfo("[%s] CDev::init\n",_name);

	// base class init first
	int ret = Device::init(); // 虚函数未实现

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

uORB::DeviceMaster *uORB::Manager::get_device_master()
{
	if (!_device_master) {
		_device_master = new DeviceMaster();

		if (_device_master) {
            // DeviceMaster是公共继承了CDev，实际完成OS层面的字符驱动注册
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
```
> 框架的入口主要是对class Manager、class DeviceMaster实例化
##### 3.2消息入口
- uORB API接口
> *  uORB::Manager::orb_advertise()
> *  uORB::Manager::orb_advertise_multi()
> *  uORB::Manager::orb_unadvertise()
> *  uORB::Manager::orb_subscribe()
> *  uORB::Manager::orb_subscribe_multi()
> *  uORB::Manager::orb_unsubscribe()
> *  uORB::Manager::orb_copy()
> *  uORB::Manager::orb_publish()
> *  uORB::Manager::orb_check()
> *  uORB::Manager::orb_stat()
> *  uORB::Manager::orb_exists()
> *  uORB::Manager::orb_priority()
> *  uORB::Manager::orb_set_interval()
> *  uORB::Manager::orb_get_interval()
- 消息登记
> * orb_advertise()
> * orb_advertise_multi() <br>
> &nbsp; &nbsp; &nbsp; &nbsp;消息登记主要是完成消息话题在vfs文件系统下的注册,节点注册完成后在（/obj/_obj_）对应目录下会产生相应的节点。<br>
> &nbsp; &nbsp; &nbsp; &nbsp;在软件上则是产生一个 class DeviceNode的实例与之对应。
```c++
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
```
这里涉及到对class DeviceNode 和 class DeviceMaster的类解析，这个放到模块分析部分。

- 消息发布
> *  uORB::Manager::orb_publish()<br>
> 
> &nbsp; &nbsp; &nbsp; &nbsp;简单来所，在登记网消息话题后，文件系统中已经存在消息节点，消息发布就是将指定格式的数据写入到指定消息节点。
```c++
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
```

- 消息订阅
> * uORB::Manager::orb_subscribe()<br>
&nbsp; &nbsp; &nbsp; &nbsp;消息订阅本质上来说和消息登记是一样的，因为都是调用了Manager::node_open()打开指定名称的消息节点去获取文件描述符。
```c++
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
```
- 消息查询
> * uORB::Manager::orb_check()<br>
&nbsp; &nbsp; &nbsp; &nbsp;消息查询是通过查询消息节点（DeviceNode）是否存在更新，这个判断更新的方式很巧妙，原话是这样的：<br>
If the subscriber's generation count matches the update <br>generation count, there has been no update from their perspective; if they<br>
don't match then we might have a visible update.<br>
&nbsp; &nbsp; &nbsp; &nbsp;消息节点会有一个更新计数，订阅者也有一个更新计数，如果两者不同步，意味着更新了，否则就是没有更新。<br>
&nbsp; &nbsp; &nbsp; &nbsp;同时消息节点还支持订阅者查询超时返回，就是查询该主题在设定时间interval（通过Manager::orb_set_interval()设置）内发生多次更新，则以设定的时间耗尽后再返回。
```c++
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
```
- 消息拷贝
> * uORB::Manager::orb_copy()<br>
&nbsp; &nbsp; &nbsp; &nbsp;消息拷贝就更简单了，只需要通过消息节点的文件描述符，直接读取内容到内存就可以了。
```c++
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
```
消息登记、消息发布、消息订阅、消息拷贝基本上覆盖了uORB的最基础的几个功能，有了这个感性的认识，就可以使用uORB来实现内部进程间的消息（一对多）传递了。



