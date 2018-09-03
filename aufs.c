#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/cred.h>

#define AUFS_MAGIC 0x64668735

static struct vfsmount *aufs_mount;
static int aufs_mount_count;

static struct inode *aufs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
  struct inode *inode = new_inode(sb);

  if (inode)
  {
    const struct cred *cred = current_cred();
    inode->i_uid = cred->fsuid;
    inode->i_gid = cred->fsgid;

    inode->i_ino = get_next_ino();
    inode->i_mode = mode;
    inode->i_blkbits = PAGE_SIZE >> 1;
    inode->i_blocks = 0;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    switch (mode & S_IFMT)
    {
    default:
      init_special_inode(inode, mode, dev);
      break;
    case S_IFREG:
      printk("creat a file \n");
      break;
    case S_IFDIR:
      inode->i_op = &simple_dir_inode_operations;
      inode->i_fop = &simple_dir_operations;
      printk("creat a dir file \n");

      inc_nlink(inode);
      break;
    }
  }
  return inode;
}

/* SMP-safe  创建iNode的函数 */
static int aufs_mknod(struct inode *dir, struct dentry *dentry,
                      int mode, dev_t dev)
{
  struct inode *inode;
  int error = -EPERM;
  /* 判断inode是否存在 如果存在就返回退出函数 */
  if (dentry->d_inode)
    return -EEXIST;
  /* 如果inode不存在就调用aufs_get_inode函数创建inode */
  inode = aufs_get_inode(dir->i_sb, mode, dev);
  printk("aufs/aufs_mknod: "
         "dir->i_sb->s_root->d_name.name '%s', "
         "dev '%d', "
         "dentry->d_name.name '%s', "
         "mode '%d', "
         "inode->i_ino '%ld'\n",
         dir->i_sb->s_root->d_name.name,
         dev,
         dentry->d_name.name,
         mode,
         inode->i_ino);
  if (inode)
  {
    /*  */
    d_instantiate(dentry, inode);
    dget(dentry);
    error = 0;
  }
  return error;
}

static int aufs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
  int res;
  /* 参数S_IFDIR表示创建一个目录文件的inode */
  res = aufs_mknod(dir, dentry, mode | S_IFDIR, 0);
  if (!res)
    inc_nlink(dir);
  return res;
}

static int aufs_create(struct inode *dir, struct dentry *dentry, int mode)
{
  return aufs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int aufs_fill_super(struct super_block *sb, void *data, int silent)
{
  static struct tree_descr debug_files[] = {{""}};

  return simple_fill_super(sb, AUFS_MAGIC, debug_files);
}

static struct dentry *aufs_mount_single(struct file_system_type *fs_type,
                                       int flags, const char *dev_name,
                                       void *data)
{
  return mount_single(fs_type, flags, data, aufs_fill_super);
}

static struct file_system_type au_fs_type = {
    .owner = THIS_MODULE,
    .name = "aufs",
    .mount = aufs_mount_single,
    .kill_sb = kill_litter_super,
};

/* 创建文件的inode和dentry结构 */
static int aufs_create_by_name(const char *name, mode_t mode,
                               struct dentry *parent,
                               struct dentry **dentry)
{
  int error = 0;

  /* If the parent is not specified, we create it in the root.  
   * We need the root dentry to do this, which is in the super  
   * block. A pointer to that is in the struct vfsmount that we  
   * have around.  
   */
  /* 判断是否有父目录 没有就赋予文件系统的根dentry */
  if (!parent)
  {
    if (aufs_mount && aufs_mount->mnt_sb)
    {
      parent = aufs_mount->mnt_sb->s_root;
    }
  }
  if (!parent)
  {
    printk("Ah! can not find a parent!\n");
    return -EFAULT;
  }

  *dentry = NULL;
  /* 原子锁  */
  inode_lock(parent->d_inode);

  /* 调用lookup_one_len：首先在父目录下根据名字查找dentry结构 如果存在就返回指针 不存在就创建一个dentry */
  *dentry = lookup_one_len(name, parent, strlen(name));
  if (!IS_ERR(dentry))
  {
    if ((mode & S_IFMT) == S_IFDIR)
      /* 这里表示创建一个目录文件的inode */
      error = aufs_mkdir(parent->d_inode, *dentry, mode);
    else
      /* 创建一个文件的inode */
      error = aufs_create(parent->d_inode, *dentry, mode);
  }
  else
    error = PTR_ERR(dentry);

  inode_unlock(parent->d_inode);
  return error;
}

/* 创建一个文件 文件是用dentry和inode代表的 这里是创建dentry和inode */
struct dentry *aufs_create_file(const char *name, mode_t mode,
                                struct dentry *parent, void *data,
                                struct file_operations *fops)
{
  struct dentry *dentry = NULL;
  int error;

  printk("aufs: creating file '%s'\n", name);

  error = aufs_create_by_name(name, mode, parent, &dentry);
  if (error)
  {
    printk("aufs: creating file '%s' failure\n", name);
    dentry = NULL;
    goto exit;
  }
  if (dentry->d_inode)
  {
    if (data)
      dentry->d_inode->i_private = data;
    if (fops)
      dentry->d_inode->i_fop = fops;
  }
exit:
  return dentry;
}

/* 目录创建 linux 中目录也是文件 所以调用 aufs_create_file 创建文件 传入参数 S_IFDIR 指明创建的是一个目录 */
struct dentry *aufs_create_dir(const char *name, struct dentry *parent)
{
  return aufs_create_file(name,
                          S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
                          parent, NULL, NULL);
}

static int __init aufs_init(void)
{
  int retval;
  struct dentry *pslot;

  /* 将文件系统登记到系统 */
  retval = register_filesystem(&au_fs_type);

  if (!retval)
  {
    /* 创建super_block 根dentry 根inode */
    aufs_mount = kern_mount(&au_fs_type);
    /* kern_mount错误就卸载文件系统 */
    if (IS_ERR(aufs_mount))
    {
      printk(KERN_ERR "aufs: could not mount!\n");
      unregister_filesystem(&au_fs_type);
      return retval;
    }
  }
  /* 创建目录和目录下的几个文件 */
  pslot = aufs_create_dir("woman-star", NULL);
  aufs_create_file("lbb", S_IFREG | S_IRUGO, pslot, NULL, NULL);
  aufs_create_file("fbb", S_IFREG | S_IRUGO, pslot, NULL, NULL);
  aufs_create_file("ljl", S_IFREG | S_IRUGO, pslot, NULL, NULL);

  pslot = aufs_create_dir("man-star", NULL);
  aufs_create_file("ldh", S_IFREG | S_IRUGO, pslot, NULL, NULL);
  aufs_create_file("lcw", S_IFREG | S_IRUGO, pslot, NULL, NULL);
  aufs_create_file("jew", S_IFREG | S_IRUGO, pslot, NULL, NULL);

  return retval;
}

static void __exit aufs_exit(void)
{
  /* 退出函数中卸载super_block 根dentry 根inode */
  simple_release_fs(&aufs_mount, &aufs_mount_count);
  /* 卸载文件系统 */
  unregister_filesystem(&au_fs_type);
}

/* 模块入口出口函数声明 和模块声明 */
module_init(aufs_init);
module_exit(aufs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is a simple module");
MODULE_VERSION("Ver 0.1");