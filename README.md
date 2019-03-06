# Linux-mail-slot
This is the implementation of a special device file that is accessible according to FIFO style semantic (via open/close/read/write services), but offering an execution semantic of read/write services such that any segment that is posted to the stream associated with the file is seen as an independent data unit (a message), thus being posted and delivered atomically (all or nothing) and in data separation (with respect to other segments) to the reading threads.
The device file is multi-instance (by having the possibility to manage at least 256 different instances) so that mutiple FIFO style streams (characterized by the above semantic) can be concurrently accessed by active processes/threads.

The device file also supports ioctl commands in order to define the run time behavior of any I/O session targeting it (such as whether read and/or write operations on a session need to be performed according to blocking or non-blocking rules).
