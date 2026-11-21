// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * it8612 - read-only hwmon driver for the ITE IT8612E Super I/O (ID 0x8612).
 *
 * The IT8612E is not supported by the in-kernel it87 driver. Its environment
 * controller uses the register map of the IT87 family (IT8603E/IT8613E), which
 * was checked on an HP Pavilion 500-526ns (board 2B35) by dumping the registers
 * at rest, under load and while cooling down:
 *
 *   0x29, 0x2a, 0x2b  temp1..temp3, signed 8-bit, degrees C
 *                     (on that board: CPU thermistor, board diode, and the AMD
 *                     Tctl that the chip reads from the APU)
 *   0x0d..0x0f        fan1..fan3 tachometer, low byte
 *   0x18..0x1a        fan1..fan3 tachometer, high byte (16-bit counters)
 *   0x13 bits 4..6    fan1..fan3 tachometer enabled
 *
 * The driver only reads. It never writes to the environment controller, so the
 * fans stay under the chip's own automatic control as set up by the BIOS. The
 * only writes are the standard Super I/O configuration-space sequence (enter
 * key, logical device select, exit) used to find the controller at load time.
 *
 * Super I/O access follows drivers/hwmon/it87.c (Jean Delvare, Christophe
 * Gauthron and others).
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define DRVNAME			"it8612"
#define IT8612E_DEVID		0x8612

/* Super I/O configuration space */
#define SIO_REG_LDSEL		0x07	/* logical device select */
#define SIO_REG_DEVID		0x20	/* device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* device revision */
#define SIO_REG_ACT		0x30	/* logical device activation */
#define SIO_REG_BASE		0x60	/* base address (2 bytes) */
#define SIO_LDN_EC		0x04	/* environment controller */

/* Environment controller: address port at base + 5, data port at base + 6 */
#define EC_ADDR_OFFSET		5
#define EC_EXTENT		2

#define EC_REG_FAN_MAIN_CTRL	0x13
#define EC_REG_FAN_LOW(n)	(0x0d + (n))
#define EC_REG_FAN_HIGH(n)	(0x18 + (n))
#define EC_REG_TEMP(n)		(0x29 + (n))

#define NUM_TEMP		3
#define NUM_FAN			3

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID (testing only)");

static bool ignore_resource_conflict;
module_param(ignore_resource_conflict, bool, 0);
MODULE_PARM_DESC(ignore_resource_conflict, "Ignore an ACPI resource conflict");

static struct platform_device *it8612_pdev;

struct it8612_data {
	unsigned short addr;	/* address port of the environment controller */
	u8 fans;		/* bit n set: tachometer n enabled */
	struct mutex lock;	/* address/data port pair */
};

/* Super I/O configuration space */

static int superio_enter(int ioreg)
{
	if (!request_muxed_region(ioreg, 2, DRVNAME))
		return -EBUSY;
	outb(0x87, ioreg);
	outb(0x01, ioreg);
	outb(0x55, ioreg);
	outb(ioreg == 0x4e ? 0xaa : 0x55, ioreg);
	return 0;
}

static void superio_exit(int ioreg)
{
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
	release_region(ioreg, 2);
}

static int superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static int superio_inw(int ioreg, int reg)
{
	return superio_inb(ioreg, reg) << 8 | superio_inb(ioreg, reg + 1);
}

static void superio_select(int ioreg, int ldn)
{
	outb(SIO_REG_LDSEL, ioreg);
	outb(ldn, ioreg + 1);
}

/* Environment controller (caller holds data->lock) */

static u8 env_read(struct it8612_data *data, u8 reg)
{
	outb_p(reg, data->addr);
	return inb_p(data->addr + 1);
}

/* hwmon interface */

static umode_t it8612_is_visible(const void *drvdata,
				 enum hwmon_sensor_types type, u32 attr,
				 int channel)
{
	const struct it8612_data *data = drvdata;

	switch (type) {
	case hwmon_temp:
		return attr == hwmon_temp_input ? 0444 : 0;
	case hwmon_fan:
		if (attr == hwmon_fan_input && (data->fans & BIT(channel)))
			return 0444;
		return 0;
	default:
		return 0;
	}
}

static int it8612_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct it8612_data *data = dev_get_drvdata(dev);
	unsigned int count;
	s8 temp;

	switch (type) {
	case hwmon_temp:
		mutex_lock(&data->lock);
		temp = env_read(data, EC_REG_TEMP(channel));
		mutex_unlock(&data->lock);
		if (temp == -128)	/* 0x80: no sensor */
			return -ENODATA;
		*val = temp * 1000L;
		return 0;
	case hwmon_fan:
		mutex_lock(&data->lock);
		count = env_read(data, EC_REG_FAN_LOW(channel));
		count |= env_read(data, EC_REG_FAN_HIGH(channel)) << 8;
		mutex_unlock(&data->lock);
		/*
		 * As it87: a count of 0 is not a valid reading (no data);
		 * 0xffff means the fan is stopped or not connected (0 rpm).
		 */
		if (count == 0)
			return -ENODATA;
		*val = count == 0xffff ? 0 : 1350000 / (count * 2);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops it8612_hwmon_ops = {
	.is_visible = it8612_is_visible,
	.read = it8612_read,
};

static const struct hwmon_channel_info * const it8612_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT, HWMON_T_INPUT, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT, HWMON_F_INPUT, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_chip_info it8612_chip_info = {
	.ops = &it8612_hwmon_ops,
	.info = it8612_info,
};

/* Platform driver */

static int it8612_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct it8612_data *data;
	struct device *hwmon;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENODEV;
	if (!devm_request_region(dev, res->start, EC_EXTENT, DRVNAME)) {
		dev_err(dev, "ports 0x%lx-0x%lx busy (is it87 loaded?)\n",
			(unsigned long)res->start, (unsigned long)res->end);
		return -EBUSY;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->addr = res->start;
	mutex_init(&data->lock);
	data->fans = (env_read(data, EC_REG_FAN_MAIN_CTRL) >> 4) & 0x07;

	hwmon = devm_hwmon_device_register_with_info(dev, DRVNAME, data,
						     &it8612_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon);
}

static struct platform_driver it8612_driver = {
	.driver = {
		.name = DRVNAME,
	},
	.probe = it8612_probe,
};

/* Module */

static int __init it8612_find(int sioaddr, unsigned short *base)
{
	u16 devid;
	int err;

	err = superio_enter(sioaddr);
	if (err)
		return err;

	err = -ENODEV;
	devid = superio_inw(sioaddr, SIO_REG_DEVID);
	if (devid == 0xffff)	/* nothing at this address */
		goto exit;
	if (force_id)
		devid = force_id;
	if (devid != IT8612E_DEVID)
		goto exit;

	superio_select(sioaddr, SIO_LDN_EC);
	if (!(superio_inb(sioaddr, SIO_REG_ACT) & 0x01)) {
		pr_info("environment controller not activated, skipping\n");
		goto exit;
	}
	*base = superio_inw(sioaddr, SIO_REG_BASE) & ~0x07;
	if (!*base) {
		pr_info("environment controller base address not set, skipping\n");
		goto exit;
	}
	pr_info("found IT8612E rev %d at 0x%x, environment controller at 0x%x\n",
		superio_inb(sioaddr, SIO_REG_DEVREV) & 0x0f, sioaddr, *base);
	err = 0;
exit:
	superio_exit(sioaddr);
	return err;
}

static int __init it8612_init(void)
{
	static const int sioaddr[] = { 0x2e, 0x4e };
	struct resource res = {
		.name = DRVNAME,
		.flags = IORESOURCE_IO,
	};
	unsigned short base = 0;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(sioaddr); i++)
		if (!it8612_find(sioaddr[i], &base))
			break;
	if (!base)
		return -ENODEV;

	res.start = base + EC_ADDR_OFFSET;
	res.end = res.start + EC_EXTENT - 1;
	err = acpi_check_resource_conflict(&res);
	if (err && !ignore_resource_conflict)
		return err;

	err = platform_driver_register(&it8612_driver);
	if (err)
		return err;

	it8612_pdev = platform_device_register_simple(DRVNAME, base, &res, 1);
	if (IS_ERR(it8612_pdev)) {
		platform_driver_unregister(&it8612_driver);
		return PTR_ERR(it8612_pdev);
	}
	return 0;
}

static void __exit it8612_exit(void)
{
	platform_device_unregister(it8612_pdev);
	platform_driver_unregister(&it8612_driver);
}

module_init(it8612_init);
module_exit(it8612_exit);

MODULE_DESCRIPTION("Read-only hwmon driver for the ITE IT8612E Super I/O");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
