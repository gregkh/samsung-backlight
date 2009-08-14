/*
 * Samsung N130 Laptop Backlight driver
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

static struct pci_dev *device;

static struct pci_device_id samsung_ids[] = {
	{ PCI_DEVICE(0x8086, 0x27ae) },
	{ },
};
MODULE_DEVICE_TABLE(pci, samsung_ids);

static int probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	return -ENODEV;
}

static void remove(struct pci_dev *pci_dev)
{
}

static struct pci_driver samsung_driver = {
	.name		= "samsung-backlight",
	.id_table	= samsung_ids,
	.probe		= probe,
	.remove		= remove,
};


static int find_video_card(void)
{

	return 0;
}

static void remove_video_card(void)
{
	if (!device)
		return;
}

static int __init samsung_init(void)
{
	int retval;
	retval = pci_register_driver(&samsung_driver);
	if (retval)
		return retval;

	return find_video_card();
}

static void __exit samsung_exit(void)
{
	pci_unregister_driver(&samsung_driver);
	remove_video_card();
}

module_init(samsung_init);
module_exit(samsung_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@suse.de>");
MODULE_DESCRIPTION("Samsung N130 Backlight driver");
MODULE_LICENSE("GPL");
