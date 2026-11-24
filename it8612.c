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
 * Checked on that board: the chip ID and revision, the logical device and
 * base address of the environment controller, the three temperatures and
 * the tachometers of fan2 and fan3. Inferred from the IT87 family and not
 * checked: the meaning of bits 4..6 of 0x13 beyond the fans seen there, the
 * tachometer of fan1 (nothing is connected to it), and the alternative
 * configuration port 0x4e.
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
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define DRVNAME			"it8612"
#define IT8612E_DEVID		0x8612
#define ITE_DEVID_MASK		0xff00	/* ITE chips: 0x86xx and 0x87xx */

/* Super I/O configuration space */
#define SIO_REG_LDSEL		0x07	/* logical device select */
#define SIO_REG_DEVID		0x20	/* device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* device revision */
#define SIO_REG_ACT		0x30	/* logical device activation */
#define SIO_ACT_ENABLED		BIT(0)
#define SIO_REG_BASE		0x60	/* base address (2 bytes) */
#define SIO_BASE_MASK		0xfff8	/* 8-byte aligned */
#define SIO_DEVREV_MASK		GENMASK(3, 0)
#define SIO_LDN_EC		0x04	/* environment controller */

/* Environment controller: address port at base + 5, data port at base + 6 */
#define EC_ADDR_OFFSET		5
#define EC_EXTENT		2

#define EC_REG_FAN_MAIN_CTRL	0x13
#define EC_FAN_TACH_EN_MASK	GENMASK(6, 4)	/* bit 4 + n: fan n enabled */
#define EC_REG_FAN_LOW(n)	(0x0d + (n))
#define EC_REG_FAN_HIGH(n)	(0x18 + (n))
#define EC_REG_TEMP(n)		(0x29 + (n))
#define EC_TEMP_NO_SENSOR	-128	/* register reads 0x80 */

/* Tachometer: 16-bit count of a 1350000 / 2 Hz clock per revolution */
#define EC_FAN_COUNT_INVALID	0x0000	/* not a valid reading */
#define EC_FAN_COUNT_STOPPED	0xffff	/* stopped or not connected */
#define EC_FAN_CLOCK		1350000
#define EC_FAN_READ_TRIES	3

#define NUM_TEMP		3
#define NUM_FAN			3

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id,
		 "Treat another ITE chip ID as an IT8612E (testing only)");

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

/*
 * Read the 16-bit tachometer count. It is read low byte first, as in it87.
 * The chip may update the counter between the two byte reads; reading the
 * low byte again tells whether it did, and the pair is read again if so.
 */
static unsigned int it8612_read_fan_count(struct it8612_data *data,
					  int channel)
{
	unsigned int low, high;
	int i;

	for (i = 0; i < EC_FAN_READ_TRIES; i++) {
		low = env_read(data, EC_REG_FAN_LOW(channel));
		high = env_read(data, EC_REG_FAN_HIGH(channel));
		if (env_read(data, EC_REG_FAN_LOW(channel)) == low)
			break;
	}
	return high << 8 | low;
}

/* Convert a tachometer count to rpm, as in it87 */
static int it8612_fan_rpm(unsigned int count, long *rpm)
{
	if (count == EC_FAN_COUNT_INVALID)
		return -ENODATA;
	if (count == EC_FAN_COUNT_STOPPED)
		*rpm = 0;
	else
		*rpm = EC_FAN_CLOCK / (2 * count);
	return 0;
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
		if (attr != hwmon_temp_input || channel >= NUM_TEMP)
			return -EOPNOTSUPP;
		mutex_lock(&data->lock);
		temp = env_read(data, EC_REG_TEMP(channel));
		mutex_unlock(&data->lock);
		if (temp == EC_TEMP_NO_SENSOR)
			return -ENODATA;
		*val = temp * 1000L;
		return 0;
	case hwmon_fan:
		if (attr != hwmon_fan_input || channel >= NUM_FAN)
			return -EOPNOTSUPP;
		mutex_lock(&data->lock);
		count = it8612_read_fan_count(data, channel);
		mutex_unlock(&data->lock);
		return it8612_fan_rpm(count, val);
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
	/* not registered yet: nothing else uses the ports */
	data->fans = FIELD_GET(EC_FAN_TACH_EN_MASK,
			       env_read(data, EC_REG_FAN_MAIN_CTRL));
	dev_dbg(dev, "tachometers enabled: 0x%x\n", data->fans);

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
	u16 devid, chipid;
	int err;

	err = superio_enter(sioaddr);
	if (err)
		return err;

	err = -ENODEV;
	chipid = superio_inw(sioaddr, SIO_REG_DEVID);
	if (chipid == 0xffff || chipid == 0x0000)	/* nothing here */
		goto exit;
	devid = chipid;
	if (force_id && chipid != IT8612E_DEVID) {
		/*
		 * Only another ITE chip that answered the ITE enter key: the
		 * register map of the IT8612E is assumed, not checked.
		 */
		if ((chipid & ITE_DEVID_MASK) != 0x8600 &&
		    (chipid & ITE_DEVID_MASK) != 0x8700) {
			pr_info("chip 0x%04x at 0x%x is not an ITE chip, force_id ignored\n",
				chipid, sioaddr);
			goto exit;
		}
		pr_warn("treating chip 0x%04x at 0x%x as an IT8612E (force_id): register map not verified\n",
			chipid, sioaddr);
		devid = IT8612E_DEVID;
	}
	if (devid != IT8612E_DEVID)
		goto exit;

	superio_select(sioaddr, SIO_LDN_EC);
	if (!(superio_inb(sioaddr, SIO_REG_ACT) & SIO_ACT_ENABLED)) {
		pr_info("environment controller not activated, skipping\n");
		goto exit;
	}
	*base = superio_inw(sioaddr, SIO_REG_BASE) & SIO_BASE_MASK;
	if (!*base || *base == SIO_BASE_MASK) {
		pr_info("environment controller base address not set (0x%x), skipping\n",
			*base);
		*base = 0;
		goto exit;
	}
	pr_info("found chip 0x%04x rev %lu at 0x%x, environment controller at 0x%x\n",
		chipid,
		FIELD_GET(SIO_DEVREV_MASK, superio_inb(sioaddr, SIO_REG_DEVREV)),
		sioaddr, *base);
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
	if (err) {
		if (!ignore_resource_conflict)
			return err;
		pr_warn("ignoring the ACPI resource conflict on 0x%lx-0x%lx (ignore_resource_conflict)\n",
			(unsigned long)res.start, (unsigned long)res.end);
	}

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
MODULE_VERSION("0.3.0");
