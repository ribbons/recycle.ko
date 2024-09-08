/*
 * Copyright © 2024 Matt Robinson
 * Copyright © 2021-2022 Peter Zijlstra
 * Copyright © 2019 Andi Kleen
 * Copyright © 2018 Arnd Bergmann
 * Copyright © 2017 Josef Bacik
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");

#define STR_AND_LEN(s) s, sizeof(s) - 1

// Epoch time in ms is 13 digits long until 2286 (plus one for the dot)
#define SUFFIX_LEN 14

enum special_return_vals
{
    REACHED_ROOT = 1,
    IN_RECYCLE_DIR,
};

static char* paths[10];
static int pathcount = 0;

module_param_array(paths, charp, &pathcount, 0400);

struct recycler
{
    struct qstr path;
    struct path dir;
};

static struct recycler recycleconfs[ARRAY_SIZE(paths)];

#ifdef __x86_64__
    asmlinkage void just_return_func(void);

    asm(
        ".text\n"
        ".type just_return_func, @function\n"
        ".globl just_return_func\n"
        ASM_FUNC_ALIGN
        "just_return_func:\n"
            ANNOTATE_NOENDBR
            ASM_RET
        ".size just_return_func, .-just_return_func\n"
    );

    // cppcheck-suppress unusedFunction; referenced via a macro
    void override_function_with_return(struct pt_regs *regs)
    {
        regs->ip = (unsigned long)&just_return_func;
    }
#endif

static int buf_add_parent(const char* pathbuf, char **pos, struct qstr parent)
{
    if(*pos - pathbuf < parent.len + 1)
    {
        pr_err("File path too long to move to recycle dir\n");
        return -ENAMETOOLONG;
    }

    *pos -= parent.len;
    memcpy(*pos, parent.name, parent.len);

    *(--*pos) = '/';
    return 0;
}

static int collect_path_to_root(const char *pathbuf, char **bufpos,
    struct dentry *dir, const struct dentry *recycleroot,
    struct recycler *conf)
{
    struct dentry *walk = dir;
    int result = 0;

    while(walk != recycleroot)
    {
        if(walk == conf->dir.mnt->mnt_root || IS_ROOT(walk))
        {
            pr_debug("Reached root of mount without finding parent of %s\n",
                conf->path.name);

            result = REACHED_ROOT;
            goto cleanup;
        }

        if(walk == conf->dir.dentry)
        {
            pr_debug("File is already within recycle dir %s\n",
                conf->path.name);

            result = IN_RECYCLE_DIR;
            goto cleanup;
        }

        spin_lock(&walk->d_lock);
        result = buf_add_parent(pathbuf, bufpos, walk->d_name);
        spin_unlock(&walk->d_lock);

        if(result)
        {
            goto cleanup;
        }

        struct dentry *child = walk;
        walk = dget_parent(child);
        dput(child);
    }

cleanup:
    dput(walk);
    return result;
}

static struct dentry *vfs_path_create(struct filename *name, struct path *path,
    struct path *root)
{
    struct dentry *dentry;
    struct qstr last;
    int type;

    int error = vfs_path_parent_lookup(name, 0, path, &last, &type, root);

    if(error)
    {
        return ERR_PTR(error);
    }

    error = mnt_want_write(path->mnt);

    if(error)
    {
        path_put(path);
        return ERR_PTR(error);
    }

    inode_lock_nested(path->dentry->d_inode, I_MUTEX_PARENT);

    dentry = lookup_one_qstr_excl(&last, path->dentry,
        LOOKUP_CREATE | LOOKUP_EXCL);

    if(IS_ERR(dentry))
    {
        goto cleanup;
    }

    if(d_is_positive(dentry))
    {
        dput(dentry);
        dentry = ERR_PTR(-EEXIST);
        goto cleanup;
    }

    return dentry;

cleanup:
    inode_unlock(path->dentry->d_inode);
    mnt_drop_write(path->mnt);
    path_put(path);

    return dentry;
}

static struct dentry *kern_vfs_path_create(const char *relpath,
    struct path *path, struct path *root)
{
    struct filename *relfilename = getname_kernel(relpath);
    struct dentry *dentry = vfs_path_create(relfilename, path, root);

    putname(relfilename);
    return dentry;
}

static int create_dirs(const char *destpath, int pathlen, struct recycler *conf)
{
    char *pathcpy = kmalloc(pathlen, GFP_KERNEL);

    if(!pathcpy)
    {
        return -ENOMEM;
    }

    memcpy(pathcpy, destpath, pathlen);

    char* walk = pathcpy + pathlen - 1;

    bool desc = true;
    int depth = -1;
    int error = 0;

    while(desc || depth != 0)
    {
        if(desc)
        {
            while(*walk != '/')
            {
                walk--;
            }

            if(walk <= pathcpy)
            {
                pr_debug("Reached recycle dir %s\n", conf->path.name);
                goto cleanup;
            }

            *walk = '\0';
            depth++;
        }
        else
        {
            while(*walk != '\0')
            {
                walk++;
            }

            *walk = '/';
            depth--;
        }

        struct path path;
        struct dentry *dentry =
            kern_vfs_path_create(pathcpy, &path, &conf->dir);

        if(IS_ERR(dentry))
        {
            switch(PTR_ERR(dentry))
            {
                case -EEXIST:
                    // Found existing directory
                    desc = false;
                    continue;
                case -ENOENT:
                    // Parent dir does not exist, create that first
                    continue;
            }

            pr_err("Failed to create new directory path %s\n", pathcpy);
            error = PTR_ERR(dentry);
            goto cleanup;
        }

        struct mnt_idmap *idmap = mnt_idmap(path.mnt);

        error = vfs_mkdir(idmap, path.dentry->d_inode, dentry, 0777);
        done_path_create(&path, dentry);

        if(error)
        {
            goto cleanup;
        }

        desc = false;
    }

cleanup:
    kfree(pathcpy);
    return error;
}

static int touch(struct vfsmount *mnt, struct dentry *dentry)
{
    int error = mnt_want_write(mnt);

    if(error)
    {
        return error;
    }

    struct iattr attrs;
    attrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME | ATTR_TOUCH;

    error = notify_change(mnt_idmap(mnt), dentry, &attrs, NULL);

    mnt_drop_write(mnt);
    return error;
}

static int recycle(const struct inode *srcdir, struct dentry *dentry,
    struct recycler* conf)
{
    if(srcdir->i_sb != conf->dir.dentry->d_inode->i_sb)
    {
        pr_debug("File not on same fs as recycle dir %s\n", conf->path.name);
        return 1;
    }

    char *pathbuf = kmalloc(PATH_MAX + SUFFIX_LEN, GFP_KERNEL);

    if(!pathbuf)
    {
        return -ENOMEM;
    }

    char *destpath = pathbuf + PATH_MAX;
    *(--destpath) = '\0';
    char *pathsuffix = destpath;

    struct dentry *recycleroot = dget_parent(conf->dir.dentry);

    spin_lock(&dentry->d_lock);
    int retval = buf_add_parent(pathbuf, &destpath, dentry->d_name);
    spin_unlock(&dentry->d_lock);

    if(retval)
    {
        goto cleanup;
    }

    retval = collect_path_to_root(pathbuf, &destpath, dget_parent(dentry),
        recycleroot, conf);

    if(retval)
    {
        if(retval == IN_RECYCLE_DIR)
        {
            retval = 0;
        }

        goto cleanup;
    }

    destpath++;
    pr_debug("New path under recycle dir: %s\n", destpath);

    retval = create_dirs(destpath, PATH_MAX - (destpath - pathbuf), conf);

    if(retval)
    {
        goto cleanup;
    }

    bool suffixed = false;
    struct path destdir;
    struct dentry *new_dentry;

    while(true)
    {
        new_dentry = kern_vfs_path_create(destpath, &destdir, &conf->dir);

        if(IS_ERR(new_dentry))
        {
            retval = PTR_ERR(new_dentry);

            if(retval == -EEXIST && !suffixed)
            {
                snprintf(pathsuffix, SUFFIX_LEN + 1, ".%lld",
                    ktime_get_coarse_real_ns() / 1000000);

                suffixed = true;
                continue;
            }

            pr_err("Failed to create new file path %s\n", destpath);
            goto cleanup;
        }

        break;
    }

    struct mnt_idmap *idmap = mnt_idmap(conf->dir.mnt);

    // vfs_unlink calls security_inode_unlink after locking the inode but
    // vfs_link also locks it, causing a hang unless we unlock first.
    inode_unlock(dentry->d_inode);

    retval = vfs_link(dentry, idmap, destdir.dentry->d_inode, new_dentry, NULL);
    inode_lock(dentry->d_inode);

    done_path_create(&destdir, new_dentry);

    if(retval)
    {
        pr_err("Failed to create new link %s\n", destpath);
        goto cleanup;
    }

    retval = touch(conf->dir.mnt, dentry);

    // Redo check vfs_unlink made under the original lock so a flag change
    // cannot slip through in the two short periods of being unlocked
    if(IS_SWAPFILE(dentry->d_inode))
    {
        retval = -EPERM;
    }

cleanup:
    dput(recycleroot);
    kfree(pathbuf);

    return retval;
}

static void pre_security_inode_unlink(unsigned long ip, unsigned long parent_ip,
    struct ftrace_ops *op, struct ftrace_regs *regs)
{
    const struct inode *dir =
        (const struct inode*)ftrace_regs_get_argument(regs, 0);
    struct dentry *dentry = (struct dentry*)ftrace_regs_get_argument(regs, 1);

    int result = 0;

    for(int i = 0; i < pathcount; i++)
    {
        result = recycle(dir, dentry, &recycleconfs[i]);

        if(result <= 0)
        {
            break;
        }
    }

    if(result < 0)
    {
        ftrace_regs_set_return_value(regs, result);
        ftrace_override_function_with_return(regs);
    }
}

static struct ftrace_ops ft = {
    .func = pre_security_inode_unlink,
    .flags = FTRACE_OPS_FL_SAVE_REGS |
             FTRACE_OPS_FL_IPMODIFY |
             FTRACE_OPS_FL_PERMANENT,
};

static void free_conf(void)
{
    for(int i = 0; i < pathcount; i++)
    {
        path_put(&recycleconfs[i].dir);
    }
}

static __init int recycle_init(void)
{
    if(pathcount == 0)
    {
        pr_err("At least one recycle dir path needed via 'paths' parameter\n");
        return -EINVAL;
    }

    for(int i = 0; i < pathcount; i++)
    {
        int error = kern_path(paths[i], LOOKUP_DIRECTORY, &recycleconfs[i].dir);

        if(error)
        {
            for(int j = i - 1; j >= 0; j--)
            {
                path_put(&recycleconfs[i].dir);
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

        recycleconfs[i].path.name = paths[i];
        recycleconfs[i].path.len = strlen(paths[i]);
    }

    int error = ftrace_set_filter(&ft, STR_AND_LEN("security_inode_unlink"), 0);

    if(error)
    {
        pr_err("Failed to set ftrace filter\n");
        goto cleanup;
    }

    error = register_ftrace_function(&ft);

    if(error)
    {
        pr_err("Failed to register ftrace function");
        goto cleanup;
    }

    pr_info("Init complete\n");
    return 0;

cleanup:
    free_conf();
    return error;
}

static __exit void recycle_exit(void)
{
    unregister_ftrace_function(&ft);
    ftrace_free_filter(&ft);
    free_conf();

    pr_info("Exiting\n");
}

module_init(recycle_init);
module_exit(recycle_exit);
