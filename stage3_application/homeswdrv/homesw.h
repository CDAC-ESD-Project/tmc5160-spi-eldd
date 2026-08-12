#ifndef	_HOMESW_H
#define	_HOMESW_H

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define HOMESW_DEBOUNCE_MS	15

struct homesw_dev {
	struct gpio_desc	*gpio;
	int					 irq;
	atomic_t			 triggered;
	struct hrtimer		 debounce_timer;
	wait_queue_head_t	 wq;
	struct cdev			 cdev;
	dev_t				 devt;
	struct device		*device;
};

char *homesw_devnode(struct device *dev, umode_t *mode);

int homesw_probe(struct platform_device *pdev);
int homesw_remove(struct platform_device *pdev);

#endif
