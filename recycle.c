/*
 * Copyright © 2024 Matt Robinson
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

MODULE_LICENSE("GPL");

static __init int recycle_init(void)
{
    pr_info("Init complete\n");
    return 0;
}

static __exit void recycle_exit(void)
{
    pr_info("Exiting\n");
}

module_init(recycle_init);
module_exit(recycle_exit);
