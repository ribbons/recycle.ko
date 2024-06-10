/*
 * Copyright © 2024 Matt Robinson
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kprobes.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

int pre_security_path_unlink(struct kprobe *p, struct pt_regs *regs)
{
    pr_info("Before security_path_unlink\n");
    return 0;
}

static struct kprobe kp = {
    .symbol_name = "security_path_unlink",
    .pre_handler = pre_security_path_unlink,
};

static __init int recycle_init(void)
{
    int error = register_kprobe(&kp);

    if(error)
    {
        pr_err("Failed to register kprobe for %s\n", kp.symbol_name);
        return error;
    }

    pr_info("Init complete\n");
    return 0;
}

static __exit void recycle_exit(void)
{
    unregister_kprobe(&kp);
    pr_info("Exiting\n");
}

module_init(recycle_init);
module_exit(recycle_exit);
