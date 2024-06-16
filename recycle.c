/*
 * Copyright © 2024 Matt Robinson
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static char* paths[5];
static int pathcount = 0;

module_param_array(paths, charp, &pathcount, 0400);

static struct path recycledirs[ARRAY_SIZE(paths)];

bool recycle(const struct path *srcdir, struct dentry *dentry,
    const struct path* recycledir, const char* recyclepath)
{
    if(srcdir->mnt != recycledir->mnt)
    {
        pr_debug("File not on same mount as recycle dir %s\n", recyclepath);
        return false;
    }

    struct dentry *recycleroot = dget_parent(recycledir->dentry);
    struct dentry *walk = dget(srcdir->dentry);

    while(walk != recycleroot)
    {
        struct dentry *child = walk;
        walk = dget_parent(child);
        dput(child);
    }

    bool result = true;
    char *destpath = NULL;
    dput(walk);

    size_t destlen = strlen(recyclepath) + dentry->d_name.len + 2;
    destpath = kmalloc(destlen, GFP_KERNEL);
    snprintf(destpath, destlen, "%s/%s", recyclepath, dentry->d_name.name);

    pr_debug("New path under recycle dir: %s\n", destpath);

    struct path destdir;
    struct dentry *new_dentry =
        kern_path_create(AT_FDCWD, destpath, &destdir, 0);

    if(IS_ERR(new_dentry))
    {
        pr_err("kern_path_create failed with: %ld\n", PTR_ERR(new_dentry));
        result = false;
        goto cleanup;
    }

    struct user_namespace *mnt_usern = mnt_user_ns(recycledir->mnt);

    int retval =
        vfs_link(dentry, mnt_usern, destdir.dentry->d_inode, new_dentry, NULL);

    if(retval)
    {
        pr_err("vfs_link failed with: %d\n", retval);
        result = false;
    }

    done_path_create(&destdir, new_dentry);

cleanup:
    dput(recycleroot);
    kfree(destpath);

    return result;
}

int pre_security_path_unlink(struct kprobe *p, struct pt_regs *regs)
{
    #ifdef __x86_64__
        const struct path *dir = (const struct path*)regs->di;
        struct dentry *dentry = (struct dentry*)regs->si;
    #else
        #error "Argument retrieval not implemented for current platform"
    #endif

    for(int i = 0; i < pathcount; i++)
    {
        if(recycle(dir, dentry, &recycledirs[i], paths[i]))
        {
            break;
        }
    }

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
