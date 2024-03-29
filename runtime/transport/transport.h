#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "relay_compat.h"
#include "transport_msgs.h"

/* The size of print buffers. This limits the maximum */
/* amount of data a print can send. */
#define STP_BUFFER_SIZE 8192

/* STP_CTL_BUFFER_SIZE is the maximum size of a message */
/* exchanged on the control channel. */
#define STP_CTL_BUFFER_SIZE 384

static unsigned _stp_nsubbufs;
static unsigned _stp_subbuf_size;
static pid_t _stp_target;
static pid_t _stp_orig_target;

// flags to indicate choice of host filesystem for the relayfs
// pseudofiles; chosen within _stp_transport_fs_init
static unsigned procfs_p = 0;
static unsigned debugfs_p = 0;

static int _stp_transport_init(void);
static void _stp_transport_close(void);

static int _stp_lock_transport_dir(void);
static void _stp_unlock_transport_dir(void);

static struct dentry *_stp_debugfs_get_root_dir(void);
static void _stp_debugfs_remove_root_dir(void);

static struct dentry *_stp_get_module_dir(void);
static struct dentry *_stp_procfs_get_module_dir(void);
static struct dentry *_stp_debugfs_get_module_dir(void);

static int _stp_transport_fs_init(const char *module_name);
static void _stp_transport_fs_close(void);
static int _stp_debugfs_transport_fs_init(const char *module_name);
static void _stp_debugfs_transport_fs_close(void);
static int _stp_procfs_transport_fs_init(const char *module_name);
static void _stp_procfs_transport_fs_close(void);

static int __stp_debugfs_relay_remove_buf_file_callback(struct dentry *dentry);
static int __stp_procfs_relay_remove_buf_file_callback(struct dentry *dentry);
struct rchan_buf;
static struct dentry * __stp_debugfs_relay_create_buf_file_callback(const char *filename,
                                                                    struct dentry *parent,
#ifdef STAPCONF_RELAY_UMODE_T
                                                                    umode_t mode,
#else
                                                                    int mode,
#endif
                                                                    struct rchan_buf *buf,
                                                                    int *is_global);
static struct dentry * __stp_procfs_relay_create_buf_file_callback(const char *filename,
                                                                   struct dentry *parent,
#ifdef STAPCONF_RELAY_UMODE_T
                                                                   umode_t mode,
#else
                                                                   int mode,
#endif
                                                                   struct rchan_buf *buf,
                                                                   int *is_global);
        

static void _stp_attach(void);
static void _stp_detach(void);
static void _stp_handle_start(struct _stp_msg_start *st);
static int _stp_handle_kallsyms_lookups(void);

static uid_t _stp_uid;
static gid_t _stp_gid;

static atomic_t _stp_ctl_attached;

static int _stp_bufsize;


enum _stp_transport_state {
	STP_TRANSPORT_STOPPED,
	STP_TRANSPORT_INITIALIZED,
	STP_TRANSPORT_RUNNING,
};

/*
 * All transports must provide the following functions.
 */

/*
 * _stp_transport_get_state
 *
 * This function returns the current transport state.
 */
static enum _stp_transport_state _stp_transport_get_state(void);

/*
 * _stp_transport_data_fs_init
 *
 * This function allocates any buffers needed, creates files,
 * etc. needed for this transport.
 */
static int _stp_transport_data_fs_init(void);

/* 
 * _stp_transport_data_fs_start
 *
 * This function actually starts the transport.
 */
static void _stp_transport_data_fs_start(void);

/* 
 * _stp_transport_data_fs_start
 *
 * This function stops the transport without doing any cleanup.
 */
static void _stp_transport_data_fs_stop(void);

/* 
 * _stp_transport_data_fs_close
 *
 * This function cleans up items created by
 * _stp_transport_data_fs_init().
 */
static void _stp_transport_data_fs_close(void);

/*
 * _stp_transport_data_fs_overwrite - set data overwrite mode
 * overwrite:		boolean
 *
 * When in overwrite mode and the transport buffers are full, older
 * data gets overwritten.
 */
static void _stp_transport_data_fs_overwrite(int overwrite);

/*
 * _stp_data_write_reserve - reserve bytes
 * size_request:	number of bytes to reserve
 * entry:		allocated buffer is returned here
 *
 * This function attempts to reserve size_request number of bytes,
 * returning the number of bytes actually reserved.  The allocated
 * buffer is returned in entry.  Note that the number of bytes
 * allocated may be less than the number of bytes requested.
 */
static size_t _stp_data_write_reserve(size_t size_request, void **entry);


/*
 * _stp_data_entry_data - return data pointer from entry
 * entry:		entry
 *
 * This function returns the data pointer from entry.
 */
static unsigned char *_stp_data_entry_data(void *entry);

/*
 * _stp_data_write_commit - 
 * entry:		pointer returned by _stp-data_write_reserve()
 *
 * This function notifies the transport that the bytes in entry are
 * ready to be written.  
 */
static int _stp_data_write_commit(void *entry);

#endif /* _TRANSPORT_TRANSPORT_H_ */
