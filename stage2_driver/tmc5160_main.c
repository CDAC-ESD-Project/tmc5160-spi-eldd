
#include "tmc5160.h"

static struct class *tmc5160_class;

char* tmc5160_devnode(struct device *dev, umode_t *mode)
{
	if(mode)
	 *mode = 0666;
	 
        return NULL;
}

static int tmc5160_probe(struct spi_device *spi)
{
    struct tmc5160_dev *dev;
    int ret;

    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->spi = spi;
    spi_set_drvdata(spi, dev);

    mutex_init(&dev->lock);
    atomic_set(&dev->open_count, 0);
    atomic_set(&dev->stall_pending, 0);

    dev->current_usteps = TMC5160_DEFAULT_USTEPS;
    dev->current_irun   = TMC5160_DEFAULT_IRUN;
    dev->current_ihold  = TMC5160_DEFAULT_IHOLD;



    ret = tmc5160_hw_init(dev);
    if (ret) {
        dev_err(&spi->dev, "hardware init failed: %d\n", ret);
        return ret;
    }

    ret = tmc5160_verify_ioin(dev);
    if (ret) {
        dev_err(&spi->dev, "IOIN verification failed\n");
        goto err_hw;
    }

    ret = tmc5160_hw_configure(dev);
    if (ret) {
        dev_err(&spi->dev, "hardware configuration failed\n");
        goto err_hw;
    }



    ret = tmc5160_motion_init(dev);
    if (ret) {
        dev_err(&spi->dev, "motion init failed: %d\n", ret);
        goto err_hw;
    }



    ret = tmc5160_cdev_init(dev, tmc5160_class);
    if (ret) {
        dev_err(&spi->dev, "cdev init failed: %d\n", ret);
        goto err_motion;
    }

    dev_info(&spi->dev, "TMC5160 driver probed successfully\n");

    return 0;

err_motion:
    tmc5160_motion_cleanup(dev);

err_hw:
    tmc5160_hw_cleanup(dev);

    return ret;
}

/* Remove*/

static void tmc5160_remove(struct spi_device *spi)
{
    struct tmc5160_dev *dev = spi_get_drvdata(spi);

    tmc5160_cdev_cleanup(dev);
    tmc5160_motion_cleanup(dev);
    tmc5160_hw_cleanup(dev);

    dev_info(&spi->dev, "TMC5160 driver removed\n");
}

/* Device tree*/

static const struct of_device_id tmc5160_of_match[] = {
    { .compatible = "trinamic,tmc5160" },
    { }
};
MODULE_DEVICE_TABLE(of, tmc5160_of_match);

/* SPI driver*/

static struct spi_driver tmc5160_driver = {
    .driver = {
        .name           = "tmc5160",
        .of_match_table = tmc5160_of_match,
    },
    .probe  = tmc5160_probe,
    .remove = tmc5160_remove,
};

/* Module init / exit*/

static int __init tmc5160_init(void)
{
    int ret;

    tmc5160_class = class_create(THIS_MODULE,"tmc5160");
    if (IS_ERR(tmc5160_class))
        return PTR_ERR(tmc5160_class);

    ret = spi_register_driver(&tmc5160_driver);
    if (ret) {
        class_destroy(tmc5160_class);
        return ret;
    }

    pr_info("tmc5160: driver loaded\n");

    return 0;
}

static void __exit tmc5160_exit(void)
{
    spi_unregister_driver(&tmc5160_driver);

    class_destroy(tmc5160_class);

    pr_info("tmc5160: driver unloaded\n");
}

module_init(tmc5160_init);
module_exit(tmc5160_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Project Team");
MODULE_DESCRIPTION("TMC5160 SPI Stepper Motor Driver");
