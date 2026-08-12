// direct hardware access to tmc5160
// to be called for motion and configuration

#include "tmc5160.h"

static irqreturn_t diag0_irq_handler(int irq, void *data);
static irqreturn_t diag1_irq_handler(int irq, void *data);

static void diag0_work_handler(struct work_struct *work);
static void diag1_work_handler(struct work_struct *work);

int tmc5160_hw_init(struct tmc5160_dev *dev) {
	int ret;
	dev->step_gpio = devm_gpiod_get(&dev->spi->dev, "step", GPIOD_OUT_LOW);
	if (IS_ERR(dev->step_gpio)) {
		pr_err("tmc5160: failed to acquire step gpio\n");
		return PTR_ERR(dev->step_gpio);
	}
	dev->dir_gpio = devm_gpiod_get(&dev->spi->dev, "dir", GPIOD_OUT_HIGH);
	if (IS_ERR(dev->dir_gpio)) {
		pr_err("tmc5160: failed to acquire dir gpio\n");
		return PTR_ERR(dev->dir_gpio);
	}
	dev->enable_gpio = devm_gpiod_get(&dev->spi->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(dev->enable_gpio)) {
		pr_err("tmc5160: failed to acquire enable gpio\n");
		return PTR_ERR(dev->enable_gpio);
	}
	dev->diag0_gpio = devm_gpiod_get(&dev->spi->dev, "diag0", GPIOD_IN);
	if (IS_ERR(dev->diag0_gpio)) {
		pr_err("tmc5160: failed to acquire diag0 gpio\n");
		return PTR_ERR(dev->diag0_gpio);
	}
	dev->diag1_gpio = devm_gpiod_get(&dev->spi->dev, "diag1", GPIOD_IN);
	if (IS_ERR(dev->diag1_gpio)) {
		pr_err("tmc5160: failed to acquire diag1 gpio\n");
		return PTR_ERR(dev->diag1_gpio);
	}
	dev->diag0_irq = gpiod_to_irq(dev->diag0_gpio);
	if (dev->diag0_irq < 0) {
		pr_err("tmc5160: failed to get diag0 irq number\n");
		return dev->diag0_irq;
	}
	dev->diag1_irq = gpiod_to_irq(dev->diag1_gpio);
	if (dev->diag1_irq < 0) {
		pr_err("tmc5160: failed to get diag1 irq number\n");
		return dev->diag1_irq;
	}
	INIT_WORK(&dev->diag0_work, diag0_work_handler);
	INIT_WORK(&dev->diag1_work, diag1_work_handler);
	ret = request_irq(dev->diag0_irq, diag0_irq_handler, IRQF_TRIGGER_RISING, "tmc5160-diag0", dev);
    if (ret) {
		pr_err("tmc5160: failed to request irq for diag0\n");
		return ret;
	}
    ret = request_irq(dev->diag1_irq, diag1_irq_handler, IRQF_TRIGGER_RISING, "tmc5160-diag1", dev);
    if (ret) {
		pr_err("tmc5160: failed to request irq for diag1\n");
		return ret;
	}
	pr_info("tmc5160: gpio init complete. diag0_irq: %d diag1_irq: %d\n", dev->diag0_irq, dev->diag1_irq);
	return 0;
}

void tmc5160_hw_cleanup(struct tmc5160_dev *dev) {
	if (!IS_ERR_OR_NULL(dev->enable_gpio)) {
		gpiod_set_value(dev->enable_gpio, 1);
	}
	if (dev->diag0_irq >= 0) {
		free_irq(dev->diag0_irq, dev);
		pr_info("tmc5160: diag0 irq freed\n");
	}
	if (dev->diag1_irq >= 0) {
		free_irq(dev->diag1_irq, dev);
		pr_info("tmc5160: diag1 irq freed\n");
	}
	cancel_work_sync(&dev->diag0_work);
	cancel_work_sync(&dev->diag1_work);
	pr_info("tmc5160: hardware cleanup complete\n");
}

int tmc5160_hw_configure(struct tmc5160_dev *dev) {
    u32 gconf_read;
	u32 gconf_value;
	u32 chopconf_value;
	u32 ihold_irun_value;
	u32 mres;
	int ret;
	ret = tmc5160_write_reg(dev, REG_GSTAT, 0x7);
	if (ret) {
		pr_err("tmc5160: failed to clear gstat flags\n");
		return ret;
	}
	gconf_value = TMC5160_GCONF_EN_PWM_MODE 
				| TMC5160_GCONF_SPREAD_EN 
				| TMC5160_GCONF_DIAG0_ERROR 
				| TMC5160_GCONF_DIAG1_STALL 
				| TMC5160_GCONF_DIAG1_PUSHPULL;
	ret = tmc5160_write_reg(dev, REG_GCONF, gconf_value);
	if (ret) {
		pr_err("tmc5160: failed to write gconf\n");
		return ret;
	}
	mres = 8 - ilog2(TMC5160_DEFAULT_USTEPS);
	chopconf_value = (TMC5160_CHOPCONF_TOFF  & 0xF) 
				| ((TMC5160_CHOPCONF_HSTRT & 0x7) << 4) 
				| ((TMC5160_CHOPCONF_HEND  & 0xF) << 7) 
				| ((TMC5160_CHOPCONF_TBL   & 0x3) << 15) 
				| (TMC5160_CHOPCONF_INTPOL ? BIT(11) : 0) 
				| ((mres & 0xF) << 24);
	ret = tmc5160_write_reg(dev, REG_CHOPCONF, chopconf_value);
	if (ret) {
		pr_err("tmc5160: failed to write chopconf\n");
		return ret;
	}
	ret = tmc5160_write_reg(dev, REG_GLOBAL_SCALER, TMC5160_DEFAULT_GLOBAL_SCALER);
	if (ret) {
		pr_err("tmc5160: failed to write global_scaler\n");
		return ret;
	}
	ihold_irun_value = (TMC5160_DEFAULT_IHOLD & 0x1F) 
					| ((TMC5160_DEFAULT_IRUN & 0x1F) << 8) 
					| ((TMC5160_DEFAULT_IHOLDDELAY & 0xF) << 16);
	ret = tmc5160_write_reg(dev, REG_IHOLD_IRUN, ihold_irun_value);
	if (ret) {
		pr_err("tmc5160: failed to write ihold_irun\n");
		return ret;
	}
	ret = tmc5160_write_reg(dev, REG_TPOWERDOWN, TMC5160_DEFAULT_TPOWERDOWN);
	if (ret) {
		pr_err("tmc5160: failed to write tpowerdown\n");
		return ret;
	}
	ret = tmc5160_write_reg(dev, REG_TCOOLTHRS, TMC5160_DEFAULT_TCOOLTHRS);
	if (ret) {
		pr_err("tmc5160: failed to write tcoolthrs\n");
		return ret;
	}
	ret = tmc5160_read_reg(dev, REG_GCONF, &gconf_read);
	if (ret) {
		pr_err("tmc5160: failed to read gconf for verification\n");
		return ret;
	}
	if (gconf_read != gconf_value) {
		pr_err("tmc5160: gconf verification failed (read 0x%08x, expected 0x%08x)\n", gconf_read, gconf_value);
		return -EIO;
	}
	pr_info("tmc5160: configuration complete and verified\n");
	return 0;
}

int tmc5160_write_reg(struct tmc5160_dev *dev, u8 addr, u32 data) {
	int ret;
	u8 tx[5];
	u8 rx[5];
	struct spi_transfer t;
	struct spi_message m;
	tx[0] = addr | 0x80;
	tx[1] = (data >> 24) & 0xFF;
	tx[2] = (data >> 16) & 0xFF;
	tx[3] = (data >> 8) & 0xFF;
	tx[4] = data & 0xFF;
	memset(&t, 0, sizeof(t));
	t.tx_buf = tx;
	t.rx_buf = rx;
	t.len = 5;
	t.bits_per_word = 8;
	t.cs_change = 0;
	ret = spi_setup(dev->spi);
	if (ret) {
		pr_err("tmc5160: spi_setup failed\n");
		return ret;
	}
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(dev->spi, &m);
	if (ret) {
		pr_err("tmc5160: spi_sync failed (%d)\n", ret);
		return ret;
	}
	return 0;
}

int tmc5160_read_reg(struct tmc5160_dev *dev, u8 addr, u32 *out) {
	int ret;
	u8 tx[5];
	u8 rx[5];
	struct spi_transfer t;
	struct spi_message m;
	ret = spi_setup(dev->spi);
	if (ret) {
		pr_err("tmc5160: spi_setup failed\n");
		return ret;
	}
	tx[0] = addr;
	tx[1] = 0x00;
	tx[2] = 0x00;
	tx[3] = 0x00;
	tx[4] = 0x00;
	memset(&t, 0, sizeof(t));
	t.tx_buf = tx;
	t.rx_buf = rx;
	t.len = 5;
	t.bits_per_word = 8;
	t.cs_change = 0;
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(dev->spi, &m);
	if (ret) {
		pr_err("tmc5160: spi_sync failed on first read transaction (%d)\n", ret);
		return ret;
	}
	memset(&t, 0, sizeof(t));
	t.tx_buf = tx;
	t.rx_buf = rx;
	t.len = 5;
	t.bits_per_word = 8;
	t.cs_change = 0;
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(dev->spi, &m);
	if (ret) {
		pr_err("tmc5160: spi_sync failed on second read transaction (%d)\n", ret);
		return ret;
	}
	*out = ((u32)rx[1] << 24) | ((u32)rx[2] << 16) | ((u32)rx[3] << 8) | (u32)rx[4];
	return 0;
}

void tmc5160_set_dir(struct tmc5160_dev *dev, int forward) {
	gpiod_set_value(dev->dir_gpio, forward);
}

void tmc5160_set_enable(struct tmc5160_dev *dev, int enable) {
	gpiod_set_value(dev->enable_gpio, !enable);
}

void tmc5160_step_pulse(struct tmc5160_dev *dev) {
	gpiod_set_value(dev->step_gpio, 1);
	ndelay(TMC5160_STEP_HIGH_NS);
	gpiod_set_value(dev->step_gpio, 0);
}

int tmc5160_read_faults(struct tmc5160_dev *dev, u32 *drv, u32 *gstat) {
	int ret;
	ret = tmc5160_read_reg(dev, REG_DRV_STATUS, drv);
	if (ret != 0) {
		pr_info("tmc5160: failed to read drv_status faults\n");
		return ret;
	}
	ret = tmc5160_read_reg(dev, REG_GSTAT, gstat);
	if (ret != 0) {
		pr_info("tmc5160: failed to read gstat faults\n");
		return ret;
	}
	ret = tmc5160_write_reg(dev, REG_GSTAT, 0x7);
	if(ret != 0) {
		pr_info("tmc5160: error clearing gstat flags\n");
		return ret;
	}
	return 0;
}

int tmc5160_set_current(struct tmc5160_dev *dev, u8 irun, u8 ihold) {
	u32 value;
	int ret;
	if (irun > 31 || ihold > 31) {
		return -EINVAL;
	}
	ret = tmc5160_read_reg(dev, REG_IHOLD_IRUN, &value);
	if (ret != 0) {
		pr_info("tmc5160: failed to read ihold/irun values\n");
		return ret;
	}
	value &= ~(0x1F | (0x1F << 8));
	value |= (ihold & 0x1F);
	value |= ((u32)irun & 0x1F) << 8;
	ret = tmc5160_write_reg(dev, REG_IHOLD_IRUN, value);
	if (ret != 0) {
		pr_info("tmc5160: failed to update ihold/irun values\n");
	}
	return ret;
}

int tmc5160_set_microstep(struct tmc5160_dev *dev, u32 usteps) {
	u32 value;
	u32 mres;
	int ret;
	if (usteps == 0 || usteps > 256 || (usteps & (usteps-1)) != 0) {
		return -EINVAL;
	}
	mres = 8 - ilog2(usteps);
	ret = tmc5160_read_reg(dev, REG_CHOPCONF, &value);
	if (ret != 0) {
		pr_info("tmc5160: failed to get chopconf values\n");
		return ret;
	}
	value &= ~(0xFU << 24);
	value |= (mres & 0xFU) << 24;
	ret = tmc5160_write_reg(dev, REG_CHOPCONF, value);
    if (ret != 0) {
        pr_info("tmc5160: failed to update chopconf values\n");
		return ret;
    }
	dev->current_usteps = usteps;
    return 0;
}

int tmc5160_verify_ioin(struct tmc5160_dev *dev) {
	u32 value;
	int ret;
	ret = tmc5160_read_reg(dev, REG_IOIN, &value);
	if (ret != 0) {
		pr_info("tmc5160: failed to get ioin values\n");
		return ret;
	}
	if (((value >> 24) & 0xFF) == TMC5160_VERSION_EXPECTED) {
		pr_info("tmc5160: version found to be 0x30 as expected\n");
		return 0;
	}
	else {
		pr_info("tmc5160: version mismatch, cannot continue further\n");
		return -ENODEV;
	}
}

static irqreturn_t diag0_irq_handler(int irq, void *data) {
    struct tmc5160_dev *dev = data;
	dev->fault_flags = TMC5160_FAULT_PENDING;
    schedule_work(&dev->diag0_work);
    return IRQ_HANDLED;
}

static irqreturn_t diag1_irq_handler(int irq, void *data) {
    struct tmc5160_dev *dev = data;
    atomic_set(&dev->stall_pending, 1);
    schedule_work(&dev->diag1_work);
    return IRQ_HANDLED;
}

static void diag0_work_handler(struct work_struct *work) {
    struct tmc5160_dev *dev = container_of(work, struct tmc5160_dev, diag0_work);
    u32 drv_status, gstat;
    int ret;
    ret = tmc5160_read_faults(dev, &drv_status, &gstat);
    if (ret) {
        pr_err("tmc5160: diag0 bottom-half fault read failed (%d)\n", ret);
        return;
    }
    dev->fault_flags = drv_status;
    pr_info("tmc5160: diag0 fault. drv_status: 0x%08x gstat: 0x%08x\n", drv_status, gstat);
}

static void diag1_work_handler(struct work_struct *work) {
    struct tmc5160_dev *dev = container_of(work, struct tmc5160_dev, diag1_work);
    u32 drv_status;
    int ret;
    if (!atomic_xchg(&dev->stall_pending, 0))
        return;
    ret = tmc5160_read_reg(dev, REG_DRV_STATUS, &drv_status);
    if (ret) {
        pr_err("tmc5160: diag1 bottom-half drv_status read failed (%d)\n", ret);
        return;
    }
    if (drv_status & (1UL << 24)) {   /* SG_STATUS / stall bit */
        pr_warn("tmc5160: diag1 stall detected. stopping move\n");
        tmc5160_stop(dev);
    }
}
