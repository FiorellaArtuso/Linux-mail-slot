#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pid.h>		/* For pid types */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */

#include "linux_mail_slot.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fiorella Artuso");
MODULE_DESCRIPTION("this module implements a device file driver for linux fifo mail slot");


static int major;                           /* Major number assigned to mail slot device driver */
static spinlock_t open_release_lock;

static struct fifomailslot_dev* mailslot_devices[MAX_MINOR_NUMBER];


/* the actual driver */

static int fifomailslot_open(struct inode *inode, struct file *file){
    int minor;
    struct fifomailslot_dev *dev;
    struct fifomailslot_dev *tmp;

    minor = iminor(inode);

    if (minor < 0 || minor >= MAX_MINOR_NUMBER) {
        printk(KERN_ERR "%s: minor number %d is not supported. It should be in the range [0-255]\n", DEVICE_NAME, minor);
        return -ENODEV;
    }

    printk(KERN_INFO "%s: mail slot with minor number %d opened by the process %d\n", DEVICE_NAME, minor,current->pid);

    //preallocating memory before entering the critical section since kmalloc might go to sleep
    tmp = kmalloc(sizeof(struct fifomailslot_dev), GFP_KERNEL);
    memset(tmp, 0, sizeof(struct fifomailslot_dev));

    spin_lock(&open_release_lock);

	dev = mailslot_devices[minor];

    if(!dev){
        printk(KERN_INFO "%s: new device\n", DEVICE_NAME);
        setup_fifomailslot(tmp, minor);
        mailslot_devices[minor] = tmp;
        dev = mailslot_devices[minor];
        }
    else{
        kfree(tmp);
        printk(KERN_INFO "%s: device already present\n", DEVICE_NAME);
        }

	atomic_inc(&dev->no_sessions);

	spin_unlock(&open_release_lock);

    return 0;
}


static int fifomailslot_release(struct inode *inode, struct file *file){
    int minor;
    struct fifomailslot_dev *dev;

    minor = iminor(inode);

    spin_lock(&open_release_lock);
    dev = mailslot_devices[minor];
    atomic_dec(&dev->no_sessions);
    spin_unlock(&open_release_lock);

    printk(KERN_INFO "%s: mail slot with minor number %d closed by the process %d\n", DEVICE_NAME, minor,current->pid);

    return 0;
}


static ssize_t fifomailslot_write(struct file *filp, const char *buff, size_t len, loff_t *off){
    int blocking_write;
    int minor;
    int ret;
    int required_space;
    char tmp[len];
    struct fifomailslot_dev *dev;
    struct fifomailslot_data * mesg_data;
    char * payload;

    minor = iminor(filp->f_inode);
    dev = mailslot_devices[minor];

    blocking_write = dev->blocking_write;

    printk(KERN_INFO "%s: write called on mail slot with minor number %d by the process %d, blocking=%d, current available space=%ld \n", DEVICE_NAME, minor, current->pid, blocking_write, get_freespace(dev));

    if (len > dev->max_data_unit_size || len == 0){
        printk(KERN_ERR "%s: ERROR write of a message with too high size, the len was %zu but the maximum data unit size is %ld",DEVICE_NAME, len, dev->max_data_unit_size);
        return -EMSGSIZE;
    }

    //copy from user the message and store it into a temp buffer
    if (copy_from_user(tmp, buff, len)){
        printk(KERN_ERR "%s: ERROR in the copy_from_user()",DEVICE_NAME);
        return -1;
    }

    //i am preallocating memory here before acquiring the lock
    mesg_data = kmalloc(sizeof(struct fifomailslot_data), GFP_KERNEL);
	memset(mesg_data, 0, sizeof(struct fifomailslot_data));
    payload = kmalloc(sizeof(char)*len, GFP_KERNEL);
    memset(payload, 0, sizeof(char)*len);

    if (blocking_write){
        if (mutex_lock_interruptible(&dev->mutex)){
            printk(KERN_INFO "%s: process %d woken up by a signal\n", DEVICE_NAME, current->pid);
            return -ERESTARTSYS;
            }
        }
    else{
        if (!mutex_trylock(&dev->mutex)) {
            printk(KERN_ERR "%s: Resource not available\n", DEVICE_NAME);
            kfree(mesg_data);
            kfree(payload);
            return -EAGAIN;
            }
        }

    required_space = sizeof(char)*len;
    ret = fifomailslot_wait_event_interruptible(dev, required_space, mesg_data, payload);
    if (ret){
        return ret;
    }

    //now i am in critical section and there is enough space to write
    printk(KERN_INFO "%s: write, the process is in critical section and there is enough space to write \n", DEVICE_NAME);

    mesg_data->payload = payload;
    memcpy(mesg_data->payload, tmp, len);

    mesg_data->len = len;
    atomic_long_add(sizeof(char)*len, &dev->storage_size);
    printk(KERN_INFO "%s: new storage is: %ld \n", DEVICE_NAME, dev->storage_size.counter);

    if (atomic_read(&dev->no_msg) == 0){
        dev->tail = mesg_data;
        dev->head = dev->tail;
    }
    else{
        dev->tail->next = mesg_data;
        dev->tail = dev->tail->next;
    }

    dev->tail->next = NULL;

    atomic_inc(&dev->no_msg);
    printk(KERN_INFO "%s: new number of messagges: %d \n", DEVICE_NAME, dev->no_msg.counter);

    up(&dev->readsem);

    mutex_unlock(&dev->mutex);

    return len;
}


static ssize_t fifomailslot_read(struct file * filp, char * buff, size_t  len, loff_t * off){
    int minor;
    int blocking_read;
    int mesg_len;
    char aux[MAX_DATA_UNIT_SIZE];
    struct fifomailslot_dev *dev;
    struct fifomailslot_data * temp;

    minor = iminor(filp->f_inode);
    dev = mailslot_devices[minor];

    blocking_read = dev->blocking_read;

    printk(KERN_INFO "%s: read called on mail slot with minor number %d by the process %d, blocking=%d\n", DEVICE_NAME, minor, current->pid, blocking_read);

    if (blocking_read){
        if (down_interruptible(&dev->readsem)){
            printk(KERN_INFO "%s: process %d woken up by a signal read\n", DEVICE_NAME, current->pid);
            return -ERESTARTSYS;
            }
        if (mutex_lock_interruptible(&dev->mutex)){
            printk(KERN_INFO "%s: process %d woken up by a signal write\n", DEVICE_NAME, current->pid);
            return -ERESTARTSYS;
            }
        }
    else {
        if (down_trylock(&dev->readsem)){
            printk(KERN_ERR "%s: read, no message available right now\n",DEVICE_NAME);
            return -EAGAIN;
            }
        if (!mutex_trylock(&dev->mutex)){
            printk(KERN_ERR "%s: read, resource not available\n", DEVICE_NAME);
            return -EAGAIN;
            }
        }

    mesg_len = dev->head->len;

    if (len < mesg_len){
        printk(KERN_ERR "%s: read, the buffer is too small\n", DEVICE_NAME);
        mutex_unlock(&dev->mutex);
        up(&dev->readsem);
        return -1;
    }

    memcpy(aux, dev->head->payload, mesg_len);
    temp = dev->head;
    if (dev->head->next)
        dev->head = dev->head->next;
    else
        dev->head = NULL;

    printk(KERN_INFO "%s: read, old storage = %ld, space freed = %d\n", DEVICE_NAME, atomic_long_read(&dev->storage_size), mesg_len);
    atomic_long_sub(sizeof(char)*mesg_len, &dev->storage_size);
    kfree(temp->payload);
    kfree(temp);
    atomic_dec(&dev->no_msg);
    printk(KERN_INFO "%s: read, new storage = %ld\n", DEVICE_NAME, atomic_long_read(&dev->storage_size));
    printk(KERN_INFO "%s: read, remained number of messages %d\n", DEVICE_NAME, atomic_read(&dev->no_msg));

    mutex_unlock(&dev->mutex);
    wake_up_interruptible(&dev->wq);

    //this function might sleep but it is not in critical section
    if (copy_to_user(buff, aux, mesg_len)){
        printk(KERN_ERR "%s: ERROR in the copy_to_user()",DEVICE_NAME);
        return -1;
    }

    return mesg_len;
}


static long fifomailslot_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    int minor;
    struct fifomailslot_dev *dev;

    minor = iminor(filp->f_inode);
    dev = mailslot_devices[minor];

    printk("%s : IOCTL called on mailslot with with minor number %d - cmd= %d, arg= %ld\n", DEVICE_NAME, minor, cmd, arg);

	switch(cmd){

		case CHANGE_WRITE_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: chanching write blocking mode for mailslot with minor number %d\n", DEVICE_NAME, minor);

            if(arg != 0 && arg != 1){
                printk(KERN_ERR "%s: ERROR- invalid arguments for blocking mode (0 or 1)\n",DEVICE_NAME);
                return -EINVAL;
                }

            dev->blocking_write = arg;
			break;

        case CHANGE_READ_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: chanching read blocking mode for maislot with minor numer %d\n", DEVICE_NAME, minor);

            if(arg != 0 && arg!= 1){
                printk(KERN_ERR "%s: ERROR- invalid arguments for blocking mode (0 or 1)\n", DEVICE_NAME);
                return -EINVAL;
                }

            dev->blocking_read = arg;
			break;

		case CHANGE_MAX_DATA_UNIT_SIZE_CTL:
            printk(KERN_INFO "%s: chanching maximum segment size for mailslot with minor number %d\n", DEVICE_NAME, minor);

            if(arg < 1 || arg > MAX_DATA_UNIT_SIZE){
                printk(KERN_ERR "%s: ERROR- invalid arguments for maximum segment size\n", DEVICE_NAME);
                return -EINVAL;
                }

			dev->max_data_unit_size = arg;
			break;

		case GET_MAX_DATA_UNIT_SIZE_CTL:
            printk(KERN_INFO "%s: getting maximum segment size for mailslot with minor number %d\n",DEVICE_NAME, minor);
            return dev->max_data_unit_size;

		case GET_FREESPACE_SIZE_CTL:
            printk(KERN_INFO "%s: getting free space size for maislot with minor number %d\n", DEVICE_NAME, minor);
            return get_freespace(dev);

        case GET_WRITE_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: getting write blocking mode for mailslot with minor numer %d\n", DEVICE_NAME, minor);
            return dev->blocking_write;

        case GET_READ_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: getting read blocking mode for mailslot with minor numer %d\n", DEVICE_NAME, minor);
            return dev->blocking_read;

		default:
			printk(KERN_ERR "%s : ERROR- inappropriate ioctl for device\n",DEVICE_NAME);
			return -ENOTTY;
	}

	return 0;
}

static struct file_operations fops = {
  .owner = THIS_MODULE,
  .write = fifomailslot_write,
  .open =  fifomailslot_open,
  .release = fifomailslot_release,
  .read = fifomailslot_read,
  .unlocked_ioctl = fifomailslot_ioctl
};


void setup_fifomailslot(struct fifomailslot_dev *dev, int minor){
    mutex_init(&dev->mutex);
    sema_init(&dev->readsem, 0);
    dev->minor = minor;
    dev->blocking_write = 1;
    dev->blocking_read = 1;
    dev->max_storage = MAX_STORAGE;
    dev->max_data_unit_size = MAX_DATA_UNIT_SIZE;
    dev->no_msg.counter = 0;
    dev->no_sessions.counter = 0;
    dev->storage_size.counter = 0;
    init_waitqueue_head(&dev->wq);
}

long get_freespace(struct fifomailslot_dev * dev){
    return dev->max_storage - atomic_long_read(&dev->storage_size);
}


static int fifomailslot_wait_event_interruptible(struct fifomailslot_dev *dev, int required_space, struct fifomailslot_data * mesg_data, char* payload){

    DEFINE_WAIT(wait);

    printk(KERN_INFO "%s: write, current available space=%ld, required space=%d \n", DEVICE_NAME, get_freespace(dev), required_space);
    while (get_freespace(dev) < required_space) {
        printk(KERN_INFO "%s: write, there is no enough space to write \n", DEVICE_NAME);
        mutex_unlock(&dev->mutex);

        if (!dev->blocking_write) {
            printk(KERN_ERR "%s: Non-blocking write and not enough space at the moment\n", DEVICE_NAME);
            kfree(mesg_data);
            kfree(payload);
            return -EAGAIN;
        }

        prepare_to_wait(&dev->wq, &wait, TASK_INTERRUPTIBLE);

        if (get_freespace(dev) < required_space)
            schedule();

        finish_wait(&dev->wq, &wait);

        if (signal_pending(current))
            return -ERESTARTSYS;

        if (dev->blocking_write){
            if (mutex_lock_interruptible(&dev->mutex)){
                printk(KERN_INFO "%s: process %d woken up by a signal\n", DEVICE_NAME, current->pid);
                return -ERESTARTSYS;
                }
        }
        else{
            if (!mutex_trylock(&dev->mutex)) {
                printk(KERN_ERR "%s: Resource not available\n", DEVICE_NAME);
                kfree(mesg_data);
                kfree(payload);
                return -EAGAIN;
                }
        }
    }
    return 0;
}


int fifomailslot_init(void){
	major = register_chrdev(0, DEVICE_NAME, &fops);

	if (major < 0) {
	  printk(KERN_ERR "Registering FIFO mailslot device failed\n");
	  return major;
	}

	printk(KERN_INFO "FIFO mailslot device registered, it is assigned major number %d\n", major);

    memset(mailslot_devices, 0, sizeof(struct fifomailslot_dev*) * MAX_MINOR_NUMBER);

    spin_lock_init(&open_release_lock);

	return 0;
}

void fifomailslot_cleanup(void){
    int i;
    struct fifomailslot_dev* dev;
    struct fifomailslot_data* msg_to_delete;

    for(i = 0; i< MAX_MINOR_NUMBER; i++){
        dev = mailslot_devices[i];
        if (dev){
            msg_to_delete = dev->head;
            while(msg_to_delete) {
                dev->head = dev->head->next;
                kfree(msg_to_delete->payload);
                kfree(msg_to_delete);
                msg_to_delete = dev->head;
            }
            kfree(dev);
        }
    }

	unregister_chrdev(major, DEVICE_NAME);

	printk(KERN_INFO "FIFO mailslot device unregistered, it was assigned major number %d\n", major);
}

module_init(fifomailslot_init);
module_exit(fifomailslot_cleanup);
