
#include "homesw.h"

static struct class *homesw_class;
static int homesw_major;

char *homesw_devnode(struct device *dev, umode_t *mode) {
	if (mode) {
		*mode = 0666;
	}
	return NULL;
}

static enum hrtimer_restart homesw_debounce_fn(struct hrtimer *timer) {
	struct homesw_dev *dev = container_of(timer, struct homesw_dev, debounce_timer);
	int level = gpiod_get_value(dev->gpio);
	int prev = atomic_read(&dev->triggered);
	if (level != prev) {
		atomic_set(&dev->triggered, level);
		wake_up_interruptible(&dev->wq);
	}
	return HRTIMER_NORESTART;
}

static irqreturn_t homesw_irq_handler(int irq, void *data) {
	struct homesw_dev *dev = data;
	hrtimer_start(&dev->debounce_timer, ms_to_ktime(HOMESW_DEBOUNCE_MS), HRTIMER_MODE_REL);
	return IRQ_HANDLED;
}

static int homesw_open(struct inode *inode, struct file *file) {
	struct homesw_dev *dev = container_of(inode->i_cdev, struct homesw_dev, cdev);
	file->private_data = dev;
	return 0;
}

static ssize_t homesw_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
	struct homesw_dev *dev = file->private_data;
	char val;
	if (count < 1) {
		return -EINVAL;
	}
	val = atomic_read(&dev->triggered) ? '1' : '0';
	if (copy_to_user(buf, &val, 1)) {
		return -EFAULT;
	}
	return 1;
}

static __poll_t homesw_poll(struct file *file, poll_table *wait) {
	struct homesw_dev *dev = file->private_data;
	poll_wait(file, &dev->wq, wait);
	if (atomic_read(&dev->triggered)) {
		return EPOLLIN | EPOLLPRI | EPOLLRDNORM;
	}
	return 0;
}

static const struct file_operations homesw_fops = {
	.owner   = THIS_MODULE,
	.open    = homesw_open,
	.read    = homesw_read,
	.poll    = homesw_poll,
};

static ssize_t triggered_show(struct device *device, struct device_attribute *attr, char *buf) {
	struct homesw_dev *dev = dev_get_drvdata(device);
	return sysfs_emit(buf, "%d\n", atomic_read(&dev->triggered));
}

static DEVICE_ATTR_RO(triggered);

static struct attribute *homesw_attrs[] = {
	&dev_attr_triggered.attr,
	NULL,
};

ATTRIBUTE_GROUPS(homesw);

static int homesw_probe(struct platform_device *pdev) {
	struct homesw_dev *dev;
	int ret;
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, dev);
	dev->gpio = devm_gpiod_get(&pdev->dev, "home", GPIOD_IN);
	if (IS_ERR(dev->gpio)) {
		dev_err(&pdev->dev, "failed to get home-gpios\n");
		return PTR_ERR(dev->gpio);
	}
	dev->irq = gpiod_to_irq(dev->gpio);
	if (dev->irq < 0) {
		dev_err(&pdev->dev, "failed to map GPIO to IRQ\n");
		return dev->irq;
	}
	init_waitqueue_head(&dev->wq);
	atomic_set(&dev->triggered, gpiod_get_value(dev->gpio));
	hrtimer_init(&dev->debounce_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dev->debounce_timer.function = homesw_debounce_fn;
	ret = devm_request_irq(&pdev->dev, dev->irq, homesw_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "homesw", dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ %d: %d\n", dev->irq, ret);
		return ret;
	}
	ret = alloc_chrdev_region(&dev->devt, 0, 1, HOMESW_DEVICE_NAME);
	if (ret) {
		return ret;
	}
	cdev_init(&dev->cdev, &homesw_fops);
	dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev->cdev, dev->devt, 1);
	if (ret) {
		goto err_unregister_chrdev;
	}
	dev->device = device_create_with_groups(homesw_class, &pdev->dev, dev->devt, dev, homesw_groups, HOMESW_DEVICE_NAME);
	if (IS_ERR(dev->device)) {
		ret = PTR_ERR(dev->device);
		goto err_cdev_del;
	}
	dev_info(&pdev->dev, "homesw ready, initial state=%d\n", atomic_read(&dev->triggered))
	return 0;

err_cdev_del:
	cdev_del(&dev->cdev);
err_unregister_chrdev:
	unregister_chrdev_region(dev->devt, 1);
	return ret;
}

static int homesw_remove(struct platform_device *pdev) {
	struct homesw_dev *dev = platform_get_drvdata(pdev);
	hrtimer_cancel(&dev->debounce_timer);
	device_destroy(homesw_class, dev->devt);
	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->devt, 1);
	return 0;
}

static const struct of_device_id homesw_of_match[] = {
	{ .compatible = "cdac,homesw" },
	{ }
};

MODULE_DEVICE_TABLE(of, homesw_of_match);

static struct platform_driver homesw_driver = {
	.driver = {
		.name = HOMESW_DEVICE_NAME,
		.of_match_table = homesw_of_match,
	},
	.probe  = homesw_probe,
	.remove = homesw_remove,
};

static int __init homesw_init(void) {
	homesw_class = class_create(HOMESW_CLASS_NAME);
	if (IS_ERR(homesw_class)) {
		return PTR_ERR(homesw_class);
	}
	return platform_driver_register(&homesw_driver);
}

static void __exit homesw_exit(void) {
	platform_driver_unregister(&homesw_driver);
	class_destroy(homesw_class);
}

module_init(homesw_init);
module_exit(homesw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CDAC PG-Certificate Embedded Systems Design - Stage 2 Team");
MODULE_DESCRIPTION("Home/origin limit switch driver for linear actuator homing");
