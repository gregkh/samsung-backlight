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

static struct sabi_header *sabi;
static struct sabi_interface *iface;
static unsigned int ifaceP=0;
static char *unmap1;
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

static int __init samsung_init(void)
{
	char *memcheck;
	char *testStr = "SwSmi@";
	int pStr,loca,te;

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


	pStr=0;
	memcheck = ioremap(0xf0000, 0xffff);
	unmap1 = memcheck;
	for (loca=0; loca < 0xffff; loca++) {
		if (*(testStr + pStr) == *(memcheck+loca)) {
			printk("%c",*(memcheck+loca));

			if (pStr == 5) {
				printk("\n");
				break;
			}
			pStr += 1;
		} else {
			pStr = 0;
		}
	}
	if (loca == 0xffff){
		printk(KERN_INFO "This computer does not support SABI\n");
		sabisupport = 0;
	} else {
		loca += 1; /*pointing SMI port Number*/
		sabisupport = 1;
		sabi = (struct sabi_header *)(loca+memcheck);
		ifaceP += ((sabi->dataSegment) & 0x0ffff) << 4;
		ifaceP += (sabi->dataOffset) & 0x0ffff;
		outb(sabi->enMem, sabi->portNo);
		iface = (struct sabi_interface *)ioremap(ifaceP, 16);
		printk("%x\n", (unsigned int)iface);
		if (iface != 0) {
			iface->mainfunc=0x5843;
			iface->subfunc=4;
			iface->complete=0;
			outb(sabi->ifaceFunc, sabi->portNo);
			for(te=0;te<10000;te++)
				;
		}
		//sleep needed
		outb(sabi->reMem, sabi->portNo);
		if (iface !=0 && iface->complete == 0xaa && iface->retval[0] != 0xff) {
			printk("%c%c%c%c\n",
				(iface->retval)[0],
				(iface->retval)[1],
				(iface->retval)[2],
				(iface->retval)[3]);
			iounmap(iface);
		}
//		#ifdef TIKADEBUG
		printk("This computer supports SABI==%x\n",loca+0xf0000-6);
		printk("address segment %x,offset %x\n",sabi->dataSegment,sabi->dataOffset);
		printk("%x,%x --> iface address\n",(unsigned int)ifaceP,(unsigned int)iface);
		printk("%x,%x",0x01&0x02,0x01&&0x02);
		printk("function port %x,next 3 value is %x,%x,%x\n",sabi->portNo,sabi->ifaceFunc,sabi->enMem,sabi->reMem);
//		#endif
	}

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
