/*
 * Copyright © 2024 Matt Robinson
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/namei.h>

MODULE_LICENSE("GPL");

static char* paths[5];
static int pathcount = 0;

module_param_array(paths, charp, &pathcount, 0400);

static struct path recycledirs[ARRAY_SIZE(paths)];

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
    if(pathcount == 0)
    {
        pr_err("At least one recycle dir path needed via 'paths' parameter\n");
        return -EINVAL;
    }

    for(int i = 0; i < pathcount; i++)
    {
        int error = kern_path(paths[i], LOOKUP_DIRECTORY, &recycledirs[i]);

        if(error)
        {
            for(int j = i - 1; j >= 0; j--)
            {
                path_put(&recycledirs[i]);
            }

            switch(error)
            {
                case -ENOENT:
                    pr_err("'%s' is not found\n", paths[i]);
                    return -EINVAL;
                case -ENOTDIR:
                    pr_err("'%s' is not a directory\n", paths[i]);
                    return error;
                default:
                    pr_err("kern_path failed with %d\n", error);
                    return error;
            }
        }
    }

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

    for(int i = 0; i < pathcount; i++)
    {
        path_put(&recycledirs[i]);
    }

    pr_info("Exiting\n");
}

module_init(recycle_init);
module_exit(recycle_exit);
