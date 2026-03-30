/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Ryan Lynch");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;
    PDEBUG("ioctl cmd %u", cmd);
    /**
     * TODO: handle ioctl
     */
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;
    return retval;
}

loff_t aesd_circular_buffer_size(struct aesd_circular_buffer *buffer)
{
    size_t size = 0;
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index)
    {
        size += entry->size;
    }
    return size;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    PDEBUG("llseek offset %lld whence %d", offset, whence);
    loff_t newpos;
    struct aesd_dev *dev = filp->private_data;
    if (mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }
    newpos = fixed_size_llseek(filp, offset, whence, aesd_circular_buffer_size(&dev->buffer));
    PDEBUG("newpos %lld", newpos);
    mutex_unlock(&dev->lock);
    return newpos;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */
    struct aesd_dev *dev = filp->private_data;
    if (mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }
    size_t entry_offset_byte;
    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte);
    if (!entry)
    {
        retval = 0;
        goto read_cleanup;
    }
    if (entry_offset_byte >= entry->size)
    {
        retval = 0;
        goto read_cleanup;
    }
    size_t bytes_to_copy = min(count, entry->size - entry_offset_byte);
    if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_copy))
    {
        retval = -EFAULT;
        goto read_cleanup;
    }
    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;
read_cleanup:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *dev = filp->private_data;
    if (mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }
    if (dev->current_write_entry.buffptr == NULL)
    {
        dev->current_write_entry.buffptr = kzalloc(count, GFP_KERNEL);
        if (!dev->current_write_entry.buffptr)
        {
            PDEBUG("kzalloc failed");
            retval = -ENOMEM;
            goto write_cleanup;
        }
        if (copy_from_user((char *)dev->current_write_entry.buffptr, buf, count))
        {
            PDEBUG("copy_from_user failed, freeing current_write_entry.buffptr");
            kfree(dev->current_write_entry.buffptr);
            dev->current_write_entry.buffptr = NULL;
            dev->current_write_entry.size = 0;
            retval = -EFAULT;
            goto write_cleanup;
        }
        dev->current_write_entry.size = count;
    }
    else
    {
        char *new_buffptr = krealloc((char *)dev->current_write_entry.buffptr, dev->current_write_entry.size + count, GFP_KERNEL);
        if (!new_buffptr)
        {
            // kfree(dev->current_write_entry.buffptr);
            PDEBUG("krealloc failed, keeping current_write_entry.buffptr and size");
            retval = -ENOMEM;
            goto write_cleanup;
        }
        dev->current_write_entry.buffptr = new_buffptr;
        if (copy_from_user((char *)dev->current_write_entry.buffptr + dev->current_write_entry.size, buf, count))
        {
            PDEBUG("copy_from_user failed, freeing current_write_entry.buffptr");
            kfree(dev->current_write_entry.buffptr);
            dev->current_write_entry.buffptr = NULL;
            dev->current_write_entry.size = 0;
            retval = -EFAULT;
            goto write_cleanup;
        }
        dev->current_write_entry.size += count;
    }

    char *newline = memchr(dev->current_write_entry.buffptr, '\n', dev->current_write_entry.size);
    if (newline)
    {
        PDEBUG("newline found in current_write_entry.buffptr at offset %zd", newline - dev->current_write_entry.buffptr);
        struct aesd_buffer_entry entry;
        entry.buffptr = dev->current_write_entry.buffptr;
        entry.size = dev->current_write_entry.size;
        retval = entry.size;
        PDEBUG("adding entry with buffptr %p and size %zu to circular buffer", entry.buffptr, entry.size);
        const char *removed = aesd_circular_buffer_add_entry(&dev->buffer, &entry);
        if (removed)
        {
            PDEBUG("freeing entry buffptr %p and size %zu", removed, entry.size);
            kfree((char *)removed);
        }
        dev->current_write_entry.buffptr = NULL;
        dev->current_write_entry.size = 0;
    }
    else
    {
        retval = count;
    }
write_cleanup:
    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    PDEBUG("init module");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    PDEBUG("cleanup module");
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    mutex_destroy(&aesd_device.lock);
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index)
    {
        if (entry->buffptr)
        {
            kfree(entry->buffptr);
        }
    }
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
