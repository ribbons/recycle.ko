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

struct recycler
{
    struct qstr path;
    struct path dir;
};

static struct recycler recycleconfs[ARRAY_SIZE(paths)];

static int buf_add_parent(const char* pathbuf, char **pos, struct qstr parent)
{
    if(*pos - pathbuf < parent.len + 1)
    {
        return -ENAMETOOLONG;
    }

    *pos -= parent.len;
    memcpy(*pos, parent.name, parent.len);

    *(--*pos) = '/';
    return 0;
}

char* collect_path_to_root(const char *pathbuf, char *bufpos,
    const struct path *dir, const struct dentry *recycleroot,
    struct recycler *conf)
{
    struct dentry *walk = dget(dir->dentry);
    char *result = bufpos;

    while(walk != recycleroot)
    {
        if(walk == dir->mnt->mnt_root)
        {
            pr_debug("Reached root of mount without finding parent of %s\n",
                conf->path.name);

            result = NULL;
            goto cleanup;
        }

        if(walk == conf->dir.dentry)
        {
            pr_debug("File is already within recycle dir %s\n",
                conf->path.name);

            result = NULL;
            goto cleanup;
        }

        int error = buf_add_parent(pathbuf, &result, walk->d_name);

        if(error)
        {
            result = ERR_PTR(error);
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

int create_dirs(const char *destpath, int pathlen, struct recycler *conf)
{
    char *pathcpy = kmalloc(pathlen, GFP_KERNEL);
    memcpy(pathcpy, destpath, pathlen);

    char* walk = pathcpy + pathlen - 1;
    const char* stop = pathcpy + conf->path.len;

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

            if(walk <= stop)
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
            kern_path_create(AT_FDCWD, pathcpy, &path, LOOKUP_DIRECTORY);

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

            error = PTR_ERR(dentry);
            goto cleanup;
        }

        error = vfs_mkdir(mnt_user_ns(path.mnt), path.dentry->d_inode, dentry,
            0777);

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

bool recycle(const struct path *srcdir, struct dentry *dentry,
    struct recycler* conf)
{
    if(srcdir->mnt != conf->dir.mnt)
    {
        pr_debug("File not on same mount as recycle dir %s\n", conf->path.name);
        return false;
    }

    struct dentry *recycleroot = dget_parent(conf->dir.dentry);
    char *pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
    char *destpath = pathbuf + PATH_MAX;

    *(--destpath) = '\0';
    bool result = true;

    int error = buf_add_parent(pathbuf, &destpath, dentry->d_name);

    if(error)
    {
        result = false;
        goto cleanup;
    }

    destpath = collect_path_to_root(pathbuf, destpath, srcdir, recycleroot,
        conf);

    if(IS_ERR(destpath) || destpath == NULL)
    {
        result = false;
        goto cleanup;
    }

    error = buf_add_parent(pathbuf, &destpath, conf->path);

    if(error)
    {
        result = false;
        goto cleanup;
    }

    destpath++;
    pr_debug("New path under recycle dir: %s\n", destpath);

    error = create_dirs(destpath, PATH_MAX - (destpath - pathbuf), conf);

    if(error)
    {
        result = false;
        goto cleanup;
    }

    struct path destdir;
    struct dentry *new_dentry =
        kern_path_create(AT_FDCWD, destpath, &destdir, 0);

    if(IS_ERR(new_dentry))
    {
        pr_err("kern_path_create failed with: %ld\n", PTR_ERR(new_dentry));
        result = false;
        goto cleanup;
    }

    struct user_namespace *mnt_usern = mnt_user_ns(conf->dir.mnt);

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
    kfree(pathbuf);

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
        if(recycle(dir, dentry, &recycleconfs[i]))
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
        path_put(&recycleconfs[i].dir);
    }

    pr_info("Exiting\n");
}

module_init(recycle_init);
module_exit(recycle_exit);
