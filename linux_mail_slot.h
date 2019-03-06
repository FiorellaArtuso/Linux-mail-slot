#ifndef LINUX_MAIL_SLOT_HEADER
#define LINUX_MAIL_SLOT_HEADER

#define DEVICE_NAME "FIFO_MAIL_SLOT"  /* Device file name in /dev/ - not mandatory  */
#define MODNAME "FIFO_MAIL_SLOT"

#define MAX_MINOR_NUMBER 256
#define MAX_DATA_UNIT_SIZE 128
#define MAX_STORAGE (1<<20)

#define CHANGE_WRITE_BLOCKING_MODE_CTL 3
#define CHANGE_READ_BLOCKING_MODE_CTL 4
#define CHANGE_MAX_DATA_UNIT_SIZE_CTL 5
#define GET_MAX_DATA_UNIT_SIZE_CTL 6
#define GET_FREESPACE_SIZE_CTL 7
#define GET_WRITE_BLOCKING_MODE_CTL 8
#define GET_READ_BLOCKING_MODE_CTL 9


struct fifomailslot_data {
	char *payload;
	int len;
	struct fifomailslot_data *next;
};

struct fifomailslot_dev {
	struct fifomailslot_data *head, *tail;
	struct mutex mutex;
	struct semaphore readsem;
	int minor;
	int blocking_write;
    int blocking_read;
	long max_storage;
    long max_data_unit_size;
	atomic_t no_msg;
	atomic_long_t storage_size;
	atomic_t no_sessions;
    wait_queue_head_t wq;
};

static int fifomailslot_open(struct inode *, struct file *);
static int fifomailslot_release(struct inode *, struct file *);
static ssize_t fifomailslot_write(struct file *, const char *, size_t, loff_t *);
static ssize_t fifomailslot_read(struct file * , char * , size_t , loff_t * );
static long fifomailslot_ioctl (struct file *filp, unsigned int param1, unsigned long param2);
void setup_fifomailslot(struct fifomailslot_dev *dev, int minor);
long get_freespace(struct fifomailslot_dev * dev);
static int fifomailslot_wait_event_interruptible(struct fifomailslot_dev *dev, int required_space, struct fifomailslot_data * mesg_data, char* payload);
#endif
