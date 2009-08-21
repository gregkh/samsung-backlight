/*
 * Samsung N130 and NC10 Laptop Backlight driver
 *
 * Copyright (C) 2009 Greg Kroah-Hartman (gregkh@suse.de)
 * Copyright (C) 2009 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/dmi.h>

#define MAX_BRIGHT	0x07
#define OFFSET		0xf4

/*
 * HAL/gnome-display-manager really wants us to only set 8 different levels for
 * the brightness control.  And since 256 different levels seems a bit
 * overkill, that's fine.  So let's map the 256 values to 8 different ones:
 *
 * userspace	 0    1    2    3    4    5    6    7
 * hardware	31   63   95  127  159  195  223  255
 *
 * or hardware = ((userspace + 1) * 32)-1
 *
 * Note, we keep value 0 at a positive value, otherwise the screen goes
 * blank because HAL likes to set the backlight to 0 at startup when there is
 * no power plugged in.
 */

struct sabi_header {
	u16 portNo;
	u8 ifaceFunc;
	u8 enMem;
	u8 reMem;
	u16 dataOffset;
	u16 dataSegment;
	u8 BIOSifver;
	u8 LauncherString;
} __attribute__((packed));

struct sabi_interface {
	u16 mainfunc;
	u16 subfunc;
	u8 complete;
	u8 retval[20];
} __attribute__((packed));

struct sabifuncio {
	u16 subfunc;
	u16 len_data;
	u8 data[20];
} __attribute__((packed));

static struct sabi_header __iomem *sabi;
static struct sabi_interface __iomem *iface;
static void __iomem *unmap1;
static int sabisupport = 0;

static int offset = OFFSET;
module_param(offset, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(offset, "The offset into the PCI device for the brightness control");

static struct pci_dev *pci_device;
static struct backlight_device *backlight_device;

static u8 read_brightness(void)
{
	u8 kernel_brightness;
	u8 user_brightness = 0;

	pci_read_config_byte(pci_device, offset, &kernel_brightness);
	user_brightness = ((kernel_brightness + 1) / 32) - 1;
	return user_brightness;
}

static void set_brightness(u8 user_brightness)
{
	u16 kernel_brightness = 0;

	kernel_brightness = ((user_brightness + 1) * 32) - 1;
	pci_write_config_byte(pci_device, offset, (u8)kernel_brightness);
}

static int get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int update_status(struct backlight_device *bd)
{
	set_brightness(bd->props.brightness);
	return 0;
}

static struct backlight_ops backlight_ops = {
	.get_brightness	= get_brightness,
	.update_status	= update_status,
};

static int __init dmi_check_cb(const struct dmi_system_id *id)
{
	printk(KERN_INFO KBUILD_MODNAME ": found laptop model '%s'\n",
		id->ident);
	return 0;
}

static struct dmi_system_id __initdata samsung_dmi_table[] = {
	{
		.ident = "N120",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N120"),
			DMI_MATCH(DMI_BOARD_NAME, "N120"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "N130",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N130"),
			DMI_MATCH(DMI_BOARD_NAME, "N130"),
		},
		.callback = dmi_check_cb,
	},
	{ },
};


#define SABI_GET_MODEL			0x04
#define SABI_GET_BRIGHTNESS		0x10
#define SABI_SET_BRIGHTNESS		0x11
#define SABI_GET_WIRELESS_BUTTON	0x12
#define SABI_SET_WIRELESS_BUTTON	0x13
#define SABI_GET_BACKLIGHT		0x2d
#define SABI_SET_BACKLIGHT		0x2e
#define SABI_GET_ETIQUETTE_MODE		0x31
#define SABI_SET_ETIQUETTE_MODE		0x32

struct sabi_retval {
	u8 retval[4];
};

static int sabi_get_command(u8 command, struct sabi_retval *sretval)
{
	/* enable memory to be able to write to it */
	outb(readb(&sabi->enMem), readw(&sabi->portNo));

	/* write out the command */
	writew(0x5843, &iface->mainfunc);
	writew(command, &iface->subfunc);
	writeb(0, &iface->complete);
	outb(readb(&sabi->ifaceFunc), readw(&sabi->portNo));

	/* sleep for a bit to let the command complete */
	msleep(100);

	/* write protect memory to make it safe */
	outb(readb(&sabi->reMem), readw(&sabi->portNo));

	/* see if the command actually succeeded */
	if (readb(&iface->complete) == 0xaa && readb(&iface->retval[0]) != 0xff) {
		/* it did! */
		/* save off the data into a structure */
		sretval->retval[0] = readb(&iface->retval[0]);
		sretval->retval[1] = readb(&iface->retval[1]);
		sretval->retval[2] = readb(&iface->retval[2]);
		sretval->retval[3] = readb(&iface->retval[3]);
		return 0;
	}

	/* Something bad happened, so report it and error out */
	printk(KERN_WARNING "SABI command 0x%02x failed with completion flag 0x%02x and output 0x%02x\n",
		command, readb(&iface->complete), readb(&iface->retval[0]));
	return -EINVAL;
}

static int sabi_set_command(u8 command, u8 data)
{
	/* enable memory to be able to write to it */
	outb(readb(&sabi->enMem), readw(&sabi->portNo));

	/* write out the command */
	writew(0x5843, &iface->mainfunc);
	writew(command, &iface->subfunc);
	writeb(0, &iface->complete);
	writeb(data, &iface->retval[0]);
	outb(readb(&sabi->ifaceFunc), readw(&sabi->portNo));

	/* sleep for a bit to let the command complete */
	msleep(100);

	/* write protect memory to make it safe */
	outb(readb(&sabi->reMem), readw(&sabi->portNo));

	/* see if the command actually succeeded */
	if (readb(&iface->complete) == 0xaa && readb(&iface->retval[0]) != 0xff) {
		/* it did! */
		return 0;
	}

	/* Something bad happened, so report it and error out */
	printk(KERN_WARNING "SABI command 0x%02x failed with completion flag 0x%02x and output 0x%02x\n",
		command, readb(&iface->complete), readb(&iface->retval[0]));
	return -EINVAL;
}

static int __init samsung_init(void)
{
	void __iomem *memcheck;
	char *testStr = "SwSmi@";
	unsigned int ifaceP;
	int pStr,loca,te;
	struct sabi_retval sretval;
	int retval;
	int i;


	if (!dmi_check_system(samsung_dmi_table))
		return -ENODEV;

	/*
	 * The Samsung N120, N130, and NC10 use pci device id 0x27ae, while the
	 * NP-Q45 uses 0x2a02.  Odds are we might need to add more to the
	 * list over time...
	 */
	pci_device = pci_get_device(PCI_VENDOR_ID_INTEL, 0x27ae, NULL);
	if (!pci_device) {
		pci_device = pci_get_device(PCI_VENDOR_ID_INTEL, 0x2a02, NULL);
		if (!pci_device)
			return -ENODEV;
	}

	/* create a backlight device to talk to this one */
	backlight_device = backlight_device_register("samsung",
						     &pci_device->dev,
						     NULL, &backlight_ops);
	if (IS_ERR(backlight_device)) {
		pci_dev_put(pci_device);
		return PTR_ERR(backlight_device);
	}

	backlight_device->props.max_brightness = MAX_BRIGHT;
	backlight_device->props.brightness = read_brightness();
	backlight_device->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(backlight_device);

//	return 0;


	pStr = 0;
	memcheck = ioremap(0xf0000, 0xffff);
	unmap1 = memcheck;
	for (loca = 0; loca < 0xffff; loca++) {
		char temp = readb(memcheck + loca);

//		if (*(testStr + pStr) == *(memcheck+loca)) {
		if (temp == testStr[pStr]) {
			printk("%c", temp);

			if (pStr == 5) {
				printk("\n");
				break;
			}
			pStr += 1;
		} else {
			pStr = 0;
		}
	}
	if (loca == 0xffff) {
		printk(KERN_INFO "This computer does not support SABI\n");
		sabisupport = 0;
		goto exit;
		}

	loca += 1; /*pointing SMI port Number*/
	sabisupport = 1;
	sabi = (struct sabi_header __iomem *)(loca + memcheck);
	if (!sabi) {
		printk(KERN_ERR "Can't remap %p\n", loca + memcheck);
		goto exit;
	}

	printk(KERN_INFO "This computer supports SABI==%x\n", loca + 0xf0000 - 6);
	printk(KERN_INFO "SABI header:\n");
	printk(KERN_INFO " SMI Port Number = 0x%04x\n", readw(&sabi->portNo));
	printk(KERN_INFO " SMI Interface Function = 0x%02x\n", readb(&sabi->ifaceFunc));
	printk(KERN_INFO " SMI enable memory buffer = 0x%02x\n", readb(&sabi->enMem));
	printk(KERN_INFO " SMI restore memory buffer = 0x%02x\n", readb(&sabi->reMem));
	printk(KERN_INFO " SABI data offset = 0x%04x\n", readw(&sabi->dataOffset));
	printk(KERN_INFO " SABI data segment = 0x%04x\n", readw(&sabi->dataSegment));
	printk(KERN_INFO " BIOS interface version = 0x%02x\n", readb(&sabi->BIOSifver));
	printk(KERN_INFO " KBD Launcher string = 0x%02x\n", readb(&sabi->LauncherString));

	ifaceP = (readw(&sabi->dataSegment) & 0x0ffff) << 4;
	ifaceP += readw(&sabi->dataOffset) & 0x0ffff;
	iface = (struct sabi_interface __iomem *)ioremap(ifaceP, 16);
	if (!iface) {
		printk(KERN_ERR "Can't remap %x\n", ifaceP);
		goto exit;
	}
	printk("SABI Interface = %p\n", iface);

	retval = sabi_get_command(SABI_GET_MODEL, &sretval);
	if (!retval) {
		printk("SABI Model %c%c%c%c\n",
			sretval.retval[0],
			sretval.retval[1],
			sretval.retval[2],
			sretval.retval[3]);
	}

	retval = sabi_get_command(SABI_GET_BACKLIGHT, &sretval);
	if (!retval)
		printk("backlight = 0x%02x\n", sretval.retval[0]);
	retval = sabi_set_command(SABI_GET_BACKLIGHT, 0x00);
	retval = sabi_get_command(SABI_GET_BACKLIGHT, &sretval);
	if (!retval)
		printk("backlight = 0x%02x\n", sretval.retval[0]);
	msleep(1000);
	retval = sabi_set_command(SABI_GET_BACKLIGHT, 0x01);
	retval = sabi_get_command(SABI_GET_BACKLIGHT, &sretval);
	if (!retval)
		printk("backlight = 0x%02x\n", sretval.retval[0]);

	for (i = 0; i < 9; i++) {
		sabi_set_command(SABI_SET_BRIGHTNESS, i);
		retval = sabi_get_command(SABI_GET_BRIGHTNESS, &sretval);
		if (!retval)
			printk("brightness = 0x%02x\n", sretval.retval[0]);
		msleep(1000);
	}
	sabi_set_command(SABI_SET_BRIGHTNESS, 8);

	retval = sabi_get_command(SABI_GET_WIRELESS_BUTTON, &sretval);
	if (!retval)
		printk("wireless button = 0x%02x\n", sretval.retval[0]);

	retval = sabi_get_command(SABI_GET_BRIGHTNESS, &sretval);
	if (!retval)
		printk("brightness = 0x%02x\n", sretval.retval[0]);

	retval = sabi_get_command(SABI_GET_ETIQUETTE_MODE, &sretval);
	if (!retval)
		printk("etiquette mode = 0x%02x\n", sretval.retval[0]);

	/* read model number */
	outb(readb(&sabi->enMem), readw(&sabi->portNo));
	writew(0x5843, &iface->mainfunc);
	writew(4, &iface->subfunc);
	writeb(0, &iface->complete);
	outb(readb(&sabi->ifaceFunc), readw(&sabi->portNo));
	for(te=0;te<10000;te++)
		;
	/* long enough? */
	msleep(100);
	outb(readb(&sabi->reMem), readw(&sabi->portNo));
	if (readb(&iface->complete) == 0xaa && readb(&iface->retval[0]) != 0xff) {
		printk("Model %c%c%c%c\n",
			readb(&iface->retval[0]),
			readb(&iface->retval[1]),
			readb(&iface->retval[2]),
			readb(&iface->retval[3]));
	}

	/* read backlight value, 0-8 */
	outb(readb(&sabi->enMem), readw(&sabi->portNo));
	writew(0x5843, &iface->mainfunc);
	writew(0x10, &iface->subfunc);
	writeb(0, &iface->complete);
	outb(readb(&sabi->ifaceFunc), readw(&sabi->portNo));
	msleep(100);

	outb(readb(&sabi->reMem), readw(&sabi->portNo));

	if (readb(&iface->complete) == 0xaa && readb(&iface->retval[0]) != 0xff) {
		printk(KERN_INFO "backlight=%d\n", readb(&iface->retval[0]));
	}



exit:
	return 0;
}

static void __exit samsung_exit(void)
{
	backlight_device_unregister(backlight_device);

	/* we are done with the PCI device, put it back */
	pci_dev_put(pci_device);
	iounmap(iface);
	iounmap(unmap1);
}

module_init(samsung_init);
module_exit(samsung_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@suse.de>");
MODULE_DESCRIPTION("Samsung Backlight driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dmi:*:svnSAMSUNGELECTRONICSCO.,LTD.:pnN120:*:rnN120:*");
MODULE_ALIAS("dmi:*:svnSAMSUNGELECTRONICSCO.,LTD.:pnN130:*:rnN130:*");
