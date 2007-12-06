/*
 * Copyright (c) 2005 Zmanda, Inc.  All Rights Reserved.
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1 as 
 * published by the Free Software Foundation.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 * 
 * Contact information: Zmanda Inc., 505 N Mathlida Ave, Suite 120
 * Sunnyvale, CA 94085, USA, or: http://www.zmanda.com
 */

#include <string.h> /* memset() */

#include "vfs-device.h"
#include "fsusage.h"
#include "amanda.h"
#include <regex.h>

/* This regex will match all VfsDevice files in a directory. We use it
   for cleanup and verification. Note that this regex does NOT match
   the volume label. */
#define VFS_DEVICE_FILE_REGEX "^[0-9]+[\\.-]"

/* The name of the volume lockfile. Should be the same as that
   generated by lockfile_name(0). */
#define VOLUME_LOCKFILE_NAME "00000-lock"

/* Possible (abstracted) results from a system I/O operation. */
typedef enum {
    RESULT_SUCCESS,
    RESULT_ERROR,        /* Undefined error. */
    RESULT_NO_DATA,      /* End of File, while reading */
    RESULT_NO_SPACE,     /* Out of space. Sometimes we don't know if
                            it was this or I/O error, but this is the
                            preferred explanation. */
    RESULT_MAX
} IoResult;

/* here are local prototypes */
static void vfs_device_init (VfsDevice * o);
static void vfs_device_class_init (VfsDeviceClass * c);
static void vfs_device_finalize (GObject * o);

static gboolean vfs_device_start(Device * pself, DeviceAccessMode mode,
                                 char * label, char * timestamp);
static gboolean vfs_device_open_device (Device * pself,
                                        char * device_name);
static gboolean vfs_device_start_file (Device * pself, const dumpfile_t * ji);
static gboolean vfs_device_finish_file (Device * pself);
static dumpfile_t * vfs_device_seek_file (Device * self, guint file);
static gboolean vfs_device_seek_block (Device * self, guint64 block);
static gboolean vfs_device_property_get (Device * pself, DevicePropertyId ID,
                                         GValue * val);
static gboolean vfs_device_property_set (Device * pself, DevicePropertyId ID,
                                         GValue * val);
static gboolean vfs_device_recycle_file (Device * pself, guint filenum);
static Device * vfs_device_factory(char * device_type,
                                   char * device_name);
static ReadLabelStatusFlags vfs_device_read_label(Device * dself);
static gboolean vfs_device_write_block(Device * self, guint size,
                                       gpointer data, gboolean last_block);
static int vfs_device_read_block(Device * self, gpointer data, int * size_req);
static IoResult vfs_device_robust_write(VfsDevice * self,  void *buf,
                                              int count);
static IoResult vfs_device_robust_read(VfsDevice * self, void *buf,
                                             int *count);

/* Various helper functions. */
static void release_file(VfsDevice * self);
static gboolean check_is_dir(const char * name, gboolean printmsg);
static char* file_number_to_file_name(VfsDevice * self, guint file);
static gboolean file_number_to_file_name_functor(const char * filename,
                                                 gpointer datap);
static char* lockfile_name(VfsDevice * self, guint file);
static gboolean open_lock(VfsDevice * self, int file, gboolean exclusive);
static void promote_volume_lock(VfsDevice * self);
static void demote_volume_lock(VfsDevice * self);
static gboolean delete_vfs_files(VfsDevice * self);
static gboolean delete_vfs_files_functor(const char * filename,
                                         gpointer self);
static gboolean check_dir_empty_functor(const char * filename,
                                        gpointer self);
static gboolean clear_and_prepare_label(VfsDevice * self, char * label,
                                        char * timestamp);
static gint get_last_file_number(VfsDevice * self);
static gboolean get_last_file_number_functor(const char * filename,
                                             gpointer datap);
static char * make_new_file_name(VfsDevice * self, const dumpfile_t * ji);
static gboolean try_unlink(const char * file);

/* pointer to the classes of our parents */
static DeviceClass *parent_class = NULL;

void vfs_device_register(void) {
    static const char * device_prefix_list[] = { "file", NULL };
    register_device(vfs_device_factory, device_prefix_list);
}

GType
vfs_device_get_type (void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (VfsDeviceClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) vfs_device_class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (VfsDevice),
            0 /* n_preallocs */,
            (GInstanceInitFunc) vfs_device_init,
            NULL
        };

        type = g_type_register_static (TYPE_DEVICE, "VfsDevice",
                                       &info, (GTypeFlags)0);
    }
    
    return type;
}

static void 
vfs_device_init (VfsDevice * self) {
    Device * o;
    DeviceProperty prop;
    GValue response;

    self->dir_handle = NULL;
    self->dir_name = self->file_name = NULL;
    self->file_lock_name = self->volume_lock_name = NULL;
    self->file_lock_fd = self->volume_lock_fd = self->open_file_fd = -1;
    self->block_size = VFS_DEVICE_DEFAULT_BLOCK_SIZE;
    self->volume_bytes = 0; 
    self->volume_limit = 0;

    /* Register Properties */
    o = DEVICE(self);
    bzero(&response, sizeof(response));
    prop.base = &device_property_concurrency;
    prop.access = PROPERTY_ACCESS_GET_MASK;
    g_value_init(&response, CONCURRENCY_PARADIGM_TYPE);
    g_value_set_enum(&response, CONCURRENCY_PARADIGM_RANDOM_ACCESS);
    device_add_property(o, &prop, &response);
    g_value_unset(&response);

    prop.base = &device_property_streaming;
    g_value_init(&response, STREAMING_REQUIREMENT_TYPE);
    g_value_set_enum(&response, STREAMING_REQUIREMENT_NONE);
    device_add_property(o, &prop, &response);
    g_value_unset(&response);

    prop.base = &device_property_min_block_size;
    g_value_init(&response, G_TYPE_UINT);
    g_value_set_uint(&response, VFS_DEVICE_MIN_BLOCK_SIZE);
    device_add_property(o, &prop, &response);

    prop.base = &device_property_max_block_size;
    g_value_set_uint(&response, VFS_DEVICE_MAX_BLOCK_SIZE);
    device_add_property(o, &prop, &response);
    g_value_unset(&response);

    prop.base = &device_property_appendable;
    g_value_init(&response, G_TYPE_BOOLEAN);
    g_value_set_boolean(&response, TRUE);
    device_add_property(o, &prop, &response);

    prop.base = &device_property_partial_deletion;
    device_add_property(o, &prop, &response);
    g_value_unset(&response);

    /* This one is handled by Device's get_property handler. */
    prop.base = &device_property_canonical_name;
    device_add_property(o, &prop, NULL);

    prop.base = &device_property_medium_access_type;
    g_value_init(&response, MEDIA_ACCESS_MODE_TYPE);
    g_value_set_enum(&response, MEDIA_ACCESS_MODE_READ_WRITE);
    device_add_property(o, &prop, &response);
    g_value_unset(&response);

    /* These are dynamic, handled in vfs_device_property_xxx */
    prop.base = &device_property_block_size;
    prop.access = PROPERTY_ACCESS_GET_MASK | PROPERTY_ACCESS_SET_BEFORE_START;
    device_add_property(o, &prop, NULL);

    prop.base = &device_property_max_volume_usage;
    prop.access =
        (PROPERTY_ACCESS_GET_MASK | PROPERTY_ACCESS_SET_MASK) &
        (~ PROPERTY_ACCESS_SET_INSIDE_FILE_WRITE);
    device_add_property(o, &prop, NULL);
}

static void 
vfs_device_class_init (VfsDeviceClass * c G_GNUC_UNUSED)
{
    GObjectClass *g_object_class = (GObjectClass*) c;
    DeviceClass *device_class = (DeviceClass *)c;

    parent_class = g_type_class_ref(TYPE_DEVICE);

    device_class->open_device = vfs_device_open_device;
    device_class->start = vfs_device_start;
    device_class->start_file = vfs_device_start_file;
    device_class->read_label = vfs_device_read_label;
    device_class->write_block = vfs_device_write_block;
    device_class->read_block = vfs_device_read_block;
    device_class->finish_file = vfs_device_finish_file;
    device_class->seek_file = vfs_device_seek_file;
    device_class->seek_block = vfs_device_seek_block;
    device_class->property_get = vfs_device_property_get;
    device_class->property_set = vfs_device_property_set;
    device_class->recycle_file = vfs_device_recycle_file;
    g_object_class->finalize = vfs_device_finalize;
}

/* Drops everything associated with the volume file: Its name and fd,
   its lock, and its lock's name and fd. */
static void release_file(VfsDevice * self) {
    /* Doesn't hurt. */
    robust_close(self->open_file_fd);
    amfree(self->file_name);

    if (self->file_lock_fd > 0) {
        amfunlock(self->file_lock_fd, self->file_lock_name);
        close(self->file_lock_fd);
        amfree(self->file_lock_name);
    }
    self->file_lock_fd = self->open_file_fd = -1;
}

static void vfs_device_finalize(GObject * obj_self) {
    VfsDevice *self = VFS_DEVICE (obj_self);
    Device * d_self = (Device*)self;

    if (d_self->access_mode != ACCESS_NULL) {
        device_finish(d_self);
    }

    if(G_OBJECT_CLASS(parent_class)->finalize)
        (* G_OBJECT_CLASS(parent_class)->finalize)(obj_self);

    amfree(self->dir_name);

    if(self->dir_handle) {
        closedir (self->dir_handle);
        self->dir_handle = NULL;
    }

    release_file(self);

    if (self->volume_lock_fd >= 0) {
        amfunlock(self->volume_lock_fd, self->volume_lock_name);
        close(self->volume_lock_fd);
    }

    amfree(self->volume_lock_name);
}

static Device * vfs_device_factory(char * device_type,
                                   char * device_name) {
    Device * rval;
    g_assert(0 == strcmp(device_type, "file"));
    rval = DEVICE(g_object_new(TYPE_VFS_DEVICE, NULL));
    if (!device_open_device(rval, device_name)) {
        g_object_unref(rval);
        return NULL;
    } else {
        return rval;
    }
}

static gboolean check_is_dir(const char * name, gboolean printmsg) {
    struct stat dir_status;
    
    if (stat(name, &dir_status) < 0) {
#ifdef EINTR
        if (errno == EINTR) {
            return check_is_dir(name, printmsg);
        }
#endif /* EINTR */
        if (printmsg) {
            g_fprintf(stderr, "Error checking directory %s: %s\n",
                    name, strerror(errno));
        }
        return FALSE;
    } else if (!S_ISDIR(dir_status.st_mode)) {
        if (printmsg) {
            g_fprintf(stderr, "VFS Device path %s is not a directory.\n",
                    name);
        }
        return FALSE;
    } else {
        return TRUE;
    }
}

typedef struct {
    VfsDevice * self;
    int count;
    char * result;
} fnfn_data;

/* A SearchDirectoryFunctor. */
static gboolean file_number_to_file_name_functor(const char * filename,
                                                 gpointer datap) {
    char * result_tmp;
    struct stat file_status;
    fnfn_data *data = (fnfn_data*)datap;
    
    result_tmp = vstralloc(data->self->dir_name, "/", filename, NULL);    
    
    /* Just to be thorough, let's check that it's a real
       file. */
    if (0 != stat(result_tmp, &file_status)) {
        g_fprintf(stderr, "Cannot stat file %s (%s), ignoring it.\n", 
                result_tmp, strerror(errno));
    } else if (!S_ISREG(file_status.st_mode)) {
        g_fprintf(stderr, "%s is not a regular file, ignoring it.\n",
                result_tmp);
    } else {
        data->count ++;
        if (data->result == NULL) {
            data->result = result_tmp;
            result_tmp = NULL;
        }
    }
    amfree(result_tmp);
    return TRUE;
}

/* This function finds the filename for a given file number. We search
 * for a filesystem file matching the regex /^0*$device_file\./; if
 * there is more than one such file we make a warning and take an
 * arbitrary one. */
static char * file_number_to_file_name(VfsDevice * self, guint device_file) {
    char * regex;
    fnfn_data data;

    g_return_val_if_fail(self != NULL, NULL);
    data.self = self;
    data.count = 0;
    data.result = NULL;

    regex = g_strdup_printf("^0*%u\\.", device_file);

    search_directory(self->dir_handle, regex,
                     file_number_to_file_name_functor, &data);

    amfree(regex);

    if (data.count == 0) {
        g_assert(data.result == NULL);
        return NULL;
    } else if (data.count > 1) {
        g_fprintf(stderr,
                "Found multiple names for file number %d, choosing file %s.\n",
                device_file, data.result);
        return data.result;
    } else {
        g_assert(data.result != NULL);
        return data.result;
    }
    g_assert_not_reached();
    return NULL;
}

/* This function returns the dynamically-allocated lockfile name for a
   given file number. */
static char * lockfile_name(VfsDevice * self, guint number) {
    return g_strdup_printf("%s/%05d-lock", self->dir_name, number);
}

/* Does what you expect. If the lock already exists, it is released
 * and regained, in case the mode is changing.
 * The file field has several options:
 * - file > 0: Open a lock on a real volume file.
 * - file = 0: Open the volume lock as a volume file (for setup).
 * - file < 0: Open the volume lock as a volume lock (persistantly).
 */
static gboolean open_lock(VfsDevice * self, int file, gboolean exclusive) {
    int fd;
    char * name;
    
    /* At the moment, file locking is horribly broken. */
    return TRUE;

    if (file < 0) {
        if (self->volume_lock_name == NULL) {
            self->volume_lock_name = lockfile_name(self, 0);
        } else if (self->volume_lock_fd >= 0) {
            amfunlock(self->volume_lock_fd, self->volume_lock_name);
            close(self->volume_lock_fd);
        }
        name = self->volume_lock_name;
    } else {
        if (self->file_lock_fd >= 0 && self->file_lock_name != NULL) {
            amfunlock(self->file_lock_fd, self->file_lock_name);
        }
        amfree(self->file_lock_name);
        close(self->file_lock_fd);
        name = self->file_lock_name = lockfile_name(self, file);
    }
        

    fd = robust_open(name, O_CREAT | O_WRONLY, VFS_DEVICE_CREAT_MODE);

    if (fd < 0) {
        g_fprintf(stderr, "Can't open lock file %s: %s\n",
                name, strerror(errno));
        return FALSE;
    }

    if (exclusive) {
        amflock(fd, name);
    } else {
        amroflock(fd, name);
    }

    if (file < 0) {
        self->volume_lock_fd = fd;
    } else {
        self->file_lock_fd = fd;
    }
    return TRUE;
}

/* For now, does it the bad way. */
static void promote_volume_lock(VfsDevice * self) {
    amfunlock(self->volume_lock_fd, self->volume_lock_name);
    amflock(self->volume_lock_fd, self->volume_lock_name);
}

static void demote_volume_lock(VfsDevice * self) {
    amfunlock(self->volume_lock_fd, self->volume_lock_name);
    amroflock(self->volume_lock_fd, self->volume_lock_name);
}

/* A SearchDirectoryFunctor */
static gboolean update_volume_size_functor(const char * filename,
                                           gpointer user_data) {
    char * full_filename;
    struct stat stat_buf;
    VfsDevice * self = user_data;
    g_return_val_if_fail(IS_VFS_DEVICE(self), FALSE);
    
    full_filename = vstralloc(self->dir_name, "/", filename, NULL);

    if (stat(full_filename, &stat_buf) < 0) {
        /* Log it and keep going. */
        g_fprintf(stderr, "Couldn't stat file %s: %s\n",
                full_filename, strerror(errno));
        amfree(full_filename);
        return TRUE;
    }

    amfree(full_filename);
    self->volume_bytes += stat_buf.st_size;

    return TRUE;
}

static void update_volume_size(VfsDevice * self) {
    self->volume_bytes = 0;
    search_directory(self->dir_handle, "^[0-9]+\\.",
                     update_volume_size_functor, self);

}

static gboolean 
vfs_device_open_device (Device * pself, char * device_name) {
    VfsDevice * self;
    dumpfile_t * rval;
    
    self = VFS_DEVICE(pself);
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (device_name != NULL, FALSE);

    /* We don't have to free this ourselves; it will be freed by
     * vfs_device_finalize whether we succeed here or not. */
    self->dir_name = g_strconcat(device_name, "/data/", NULL);
    if (!check_is_dir(self->dir_name, TRUE)) {
        return FALSE;
    }

    /* Next open the directory itself. */
    self->dir_handle = opendir(self->dir_name);
    if (self->dir_handle == NULL) {
        g_fprintf(stderr, "Couldn't open directory %s for reading: %s\n",
                device_name, strerror(errno));
        return FALSE;
    }

    if (!open_lock(self, -1, FALSE))
        return FALSE;

    /* Not an error if this fails. Note that we ignore the class hierarchy.
     */
    rval = vfs_device_seek_file(pself, 0);
    amfree(rval);

    if (parent_class->open_device) {
        /* Will call vfs_device_read_label. */
        return (parent_class->open_device)(pself, device_name);
    } else {
        return TRUE;
    }
}

/* A SearchDirectoryFunctor */
static gboolean delete_vfs_files_functor(const char * filename,
                                         gpointer user_data) {
    VfsDevice * self;
    char * path_name;

    self = VFS_DEVICE(user_data);
    g_return_val_if_fail(self != NULL, FALSE);

    /* Skip the volume lock. */
    if (strcmp(filename, VOLUME_LOCKFILE_NAME) == 0)
        return TRUE;

    path_name = vstralloc(self->dir_name, "/", filename, NULL);
    if (unlink(path_name) != 0) {
        g_fprintf(stderr, "Error unlinking %s: %s\n", path_name,
                strerror(errno));
    }
    amfree(path_name);
    return TRUE;
}

/* delete_vfs_files deletes all VfsDevice files in the directory except the
   volume lockfile. */
static gboolean delete_vfs_files(VfsDevice * self) {
    g_assert(self != NULL);
    g_assert(self->dir_handle != NULL);

    /* This function assumes that the volume is locked! */
    search_directory(self->dir_handle, VFS_DEVICE_FILE_REGEX,
                     delete_vfs_files_functor, self);
    return TRUE;
}

/* This is a functor suitable for search_directory. It simply prints a
   warning. It also dodges the volume lockfile. */
static gboolean check_dir_empty_functor(const char * filename,
                                        gpointer user_data) {
    VfsDevice * self;
    char * path_name;

    self = VFS_DEVICE(user_data);
    g_return_val_if_fail(self != NULL, FALSE);

    if (strcmp(filename, VOLUME_LOCKFILE_NAME) == 0)
        return TRUE;

    path_name = vstralloc(self->dir_name, "/", filename, NULL);

    g_fprintf(stderr, "Found spurious storage file %s\n", path_name);

    amfree(path_name);
    return TRUE;
}

/* This function is used to write volume and dump headers. */
static gboolean write_amanda_header(VfsDevice * self,
                                    const dumpfile_t * header) {
    char * label_buffer;
    IoResult result;
    
    g_return_val_if_fail(header != NULL, FALSE);
    g_return_val_if_fail(self != NULL, FALSE);
    label_buffer = build_header(header, VFS_DEVICE_LABEL_SIZE);
    if (strlen(label_buffer)+1 > VFS_DEVICE_LABEL_SIZE) {
        amfree(label_buffer);
        g_fprintf(stderr, "Amanda header header won't fit on VFS device!\n");
        return FALSE;
    }

    result = vfs_device_robust_write(self, label_buffer, VFS_DEVICE_LABEL_SIZE);
    amfree(label_buffer);
    return (result == RESULT_SUCCESS);
}

/* clear_and_label will erase the contents of the directory, and write
 * this label in its place. This function assumes we already have a volume
 * label write lock in place (e.g., promote_lock() has been called.) */
static gboolean clear_and_prepare_label(VfsDevice * self, char * label,
                                        char * timestamp) {
    dumpfile_t * label_header;

    release_file(self);

    /* Delete any extant data, except our volume lock. */
    if (!delete_vfs_files(self)) {
        return FALSE;
    }

    /* Print warnings about any remaining files. */
    search_directory(self->dir_handle, VFS_DEVICE_FILE_REGEX,
                     check_dir_empty_functor, self);

    self->file_name = g_strdup_printf("%s/00000.%s", self->dir_name, label);

    self->open_file_fd = robust_open(self->file_name,
                                     O_CREAT | O_EXCL | O_WRONLY,
                                     VFS_DEVICE_CREAT_MODE);
    if (self->open_file_fd < 0) {
        g_fprintf(stderr, "Can't open file %s: %s\n", self->file_name,
                strerror(errno));
        return FALSE;
    }

    label_header = make_tapestart_header(DEVICE(self), label, timestamp);
    if (write_amanda_header(self, label_header)) {
        amfree(label_header);
        self->volume_bytes = VFS_DEVICE_LABEL_SIZE;
        return TRUE;
    } else {
        amfree(label_header);
        return FALSE;
    }
}

static ReadLabelStatusFlags vfs_device_read_label(Device * dself) {
    dumpfile_t * amanda_header;
    VfsDevice * self;

    self = VFS_DEVICE(dself);
    g_return_val_if_fail(self != NULL, ~READ_LABEL_STATUS_SUCCESS);

    amanda_header = vfs_device_seek_file(dself, 0);
    if (amanda_header == NULL) {
        /* This means an error occured getting locks or opening the header
         * file. */
        return (READ_LABEL_STATUS_DEVICE_ERROR |
                READ_LABEL_STATUS_VOLUME_ERROR |
                READ_LABEL_STATUS_VOLUME_UNLABELED);
    }

    if (amanda_header->type != F_TAPESTART) {
        /* This is an error, and should not happen. */
        g_fprintf(stderr, "Got a bad volume label\n");
        amfree(amanda_header);
        return READ_LABEL_STATUS_VOLUME_ERROR;
    }

    dself->volume_label = g_strdup(amanda_header->name);
    dself->volume_time = g_strdup(amanda_header->datestamp);
    amfree(amanda_header);

    update_volume_size(self);

    if (parent_class->read_label) {
        return (parent_class->read_label)(dself);
    } else {
        return READ_LABEL_STATUS_SUCCESS;
    }
}

static gboolean vfs_device_write_block(Device * pself, guint size,
                                       gpointer data, gboolean last_block) {
    VfsDevice * self = VFS_DEVICE(pself);
    IoResult result;
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(last_block || size >= (guint)self->block_size, FALSE);
    g_return_val_if_fail(pself->in_file, FALSE);
    g_assert(self->open_file_fd >= 0);

    if (self->volume_limit > 0 &&
        self->volume_bytes + size > self->volume_limit) {
        /* Simulate EOF. */
        pself->is_eof = TRUE;
        return FALSE;
    }

    result = vfs_device_robust_write(self, data, size);
    if (result == RESULT_SUCCESS) {
        self->volume_bytes += size;
        if (parent_class->write_block) {
            (parent_class->write_block)(pself, size, data, last_block);
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

static int
vfs_device_read_block(Device * pself, gpointer data, int * size_req) {
    VfsDevice * self;
    int size;
    IoResult result;
    
    self = VFS_DEVICE(pself);
    g_return_val_if_fail (self != NULL, -1);

    if (data == NULL || *size_req < self->block_size) {
        /* Just a size query. */
        *size_req = self->block_size;
        return 0;
    }

    size = self->block_size;
    result = vfs_device_robust_read(self, data, &size);
    switch (result) {
    case RESULT_SUCCESS:
        *size_req = size;
        return size;
    case RESULT_NO_DATA:
        pself->is_eof = TRUE;
        /* FALLTHROUGH */
    default:
        return -1;
    }

    g_assert_not_reached();
}

static gboolean	vfs_device_start(Device * pself,
                                 DeviceAccessMode mode, char * label,
                                 char * timestamp) {
    VfsDevice * self;
    self = VFS_DEVICE(pself);
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(parent_class->start != NULL, FALSE);
    
    if (mode == ACCESS_WRITE) {
        promote_volume_lock(self);
        if (!clear_and_prepare_label(self, label, timestamp)) {
            demote_volume_lock(self);
            return FALSE;
        }
        demote_volume_lock(self);
    }

    release_file(self);
 
    if (parent_class->start) {
        return parent_class->start(pself, mode, label, timestamp);
    } else {
        return TRUE;
    }
}

typedef struct {
    VfsDevice * self;
    int rval;
} glfn_data;

/* A SearchDirectoryFunctor. */
static gboolean get_last_file_number_functor(const char * filename,
                                             gpointer datap) {
    guint64 file;
    glfn_data * data = (glfn_data*)datap;
    g_return_val_if_fail(IS_VFS_DEVICE(data->self), FALSE);
    file = g_ascii_strtoull(filename, NULL, 10); /* Guaranteed to work. */
    if (file > G_MAXINT) {
        g_fprintf(stderr, "Super-large device file %s found, ignoring.\n",
               filename);
        return TRUE;
    }
    /* This condition is needlessly complex due to sign issues. */
    if (data->rval < 0 || ((guint)data->rval) < file) {
        data->rval = file;
    }
    return TRUE;
}

static gint get_last_file_number(VfsDevice * self) {
    glfn_data data;
    int count;
    data.self = self;
    data.rval = -1;
    
    count = search_directory(self->dir_handle, "^[0-9]+\\.",
                             get_last_file_number_functor, &data);

    if (count <= 0) {
        /* Somebody deleted something important while we weren't looking. */
        g_fprintf(stderr, "Error identifying VFS device contents!\n");
        return -1;
    } else {
        g_assert(data.rval >= 0);
    }
    
    return data.rval;
}

typedef struct {
    VfsDevice * self;
    guint request;
    int best_found;
} gnfn_data;

/* A SearchDirectoryFunctor. */
static gboolean get_next_file_number_functor(const char * filename,
                                             gpointer datap) {
    guint file;
    gnfn_data * data = (gnfn_data*)datap;
    g_return_val_if_fail(IS_VFS_DEVICE(data->self), FALSE);
    file = g_ascii_strtoull(filename, NULL, 10); /* Guaranteed to work. */
    if (file > G_MAXINT) {
        g_fprintf(stderr, "Super-large device file %s found, ignoring.\n",
               filename);
        return TRUE;
    }
    /* This condition is needlessly complex due to sign issues. */
    if (file >= data->request &&
        (data->best_found < 0 || file < (guint)data->best_found)) {
        data->best_found = file;
    }
    return TRUE;
}

/* Returns the file number equal to or greater than the given requested
 * file number. */
static gint get_next_file_number(VfsDevice * self, guint request) {
    gnfn_data data;
    int count;
    data.self = self;
    data.request = request;
    data.best_found = -1;
    
    count = search_directory(self->dir_handle, "^[0-9]+\\.",
                             get_next_file_number_functor, &data);

    if (count <= 0) {
        /* Somebody deleted something important while we weren't looking. */
        g_fprintf(stderr, "Error identifying VFS device contents!\n");
        return -1;
    }
    
    /* Could be -1. */
    return data.best_found;
}

/* Finds the file number, acquires a lock, and returns the new file name. */
static
char * make_new_file_name(VfsDevice * self, const dumpfile_t * ji) {
    char * rval;
    char *base, *sanitary_base;
    int fileno;

    for (;;) {
        fileno = 1 + get_last_file_number(self);
        if (fileno <= 0)
            return NULL;
    
        if (open_lock(self, fileno, TRUE)) {
            break;
        } else {
            continue;
        }
    }

    base = g_strdup_printf("%05d.%s.%s.%d", fileno, ji->name, ji->disk,
                           ji->dumplevel);
    sanitary_base = sanitise_filename(base);
    amfree(base);
    rval = g_strdup_printf("%s/%s", self->dir_name, sanitary_base);
    amfree(sanitary_base);
    return rval;
}

static gboolean 
vfs_device_start_file (Device * pself, const dumpfile_t * ji) {
    VfsDevice * self;

    self = VFS_DEVICE(pself);
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (ji != NULL, FALSE);

    if (self->volume_limit > 0 &&
        self->volume_bytes + VFS_DEVICE_LABEL_SIZE > self->volume_limit) {
        /* No more room. */
        return FALSE;
    }

    /* The basic idea here is thus:
       1) Try to get a lock on the next filenumber.
       2) If that fails, update our idea of "next filenumber" and try again.
       3) Then open the file itself.
       4) Write the label.
       5) Chain up. */

    self->file_name = make_new_file_name(self, ji);
    if (self->file_name == NULL)
        return FALSE;

    self->open_file_fd = robust_open(self->file_name,
                                     O_CREAT | O_EXCL | O_RDWR,
                                     VFS_DEVICE_CREAT_MODE);
    if (self->open_file_fd < 0) {
        g_fprintf(stderr, "Can't create file %s: %s\n", self->file_name,
                strerror(errno));
        release_file(self);
        return FALSE;
    }

    
    if (!write_amanda_header(self, ji)) {
        release_file(self);
        return FALSE;
    }

    self->volume_bytes += VFS_DEVICE_LABEL_SIZE;

    if (parent_class->start_file) {
        parent_class->start_file(pself, ji);
    }
    return TRUE;
}

static gboolean 
vfs_device_finish_file (Device * pself) {
    VfsDevice * self;
    self = VFS_DEVICE(pself);
    g_return_val_if_fail(self != NULL, FALSE);

    release_file(self);
    
    if (parent_class->finish_file) {
        return parent_class->finish_file(pself);
    } else {
        return TRUE;
    }
    g_assert_not_reached();
}

/* This function is used for two purposes, rather than one. In
 * addition to its documented behavior, we also use it to open the
 * volume label for reading at startup. In that second case, we avoid
 * FdDevice-related side effects. */
static dumpfile_t * 
vfs_device_seek_file (Device * pself, guint requested_file) {
    VfsDevice * self;
    int file;
    dumpfile_t * rval;
    char header_buffer[VFS_DEVICE_LABEL_SIZE];
    int header_buffer_size = sizeof(header_buffer);
    IoResult result;

    self = VFS_DEVICE(pself);
    g_return_val_if_fail (self != NULL, NULL);

    pself->in_file = FALSE;
    
    release_file(self);

    if (requested_file > 0) {
        file = get_next_file_number(self, requested_file);
    } else {
        file = requested_file;
    }

    if (file < 0) {
        /* Did they request one past the end? */
        char * tmp_file_name;
        tmp_file_name = file_number_to_file_name(self, requested_file - 1);
        if (tmp_file_name != NULL) {
            free(tmp_file_name);
            return make_tapeend_header();
        } else {
            return NULL;
        }
    }

    if (!open_lock(self, file, FALSE)) {
        return NULL;
    }

    self->file_name = file_number_to_file_name(self, file);
    if (self->file_name == NULL) {
        release_file(self);
        return NULL;
    }

    self->open_file_fd = robust_open(self->file_name, O_RDONLY, 0);
    if (self->open_file_fd <= 0) {
        g_fprintf(stderr, "Couldn't open file %s: %s\n", self->file_name,
                strerror(errno));
        amfree(self->file_name);
        release_file(self);
        return NULL;
    }

    result = vfs_device_robust_read(self, header_buffer,
                                    &header_buffer_size);
    if (result != RESULT_SUCCESS) {
        g_fprintf(stderr, "Problem reading Amanda header.\n");
        release_file(self);
        return NULL;
    }

    rval = malloc(sizeof(*rval));
    parse_file_header(header_buffer, rval, header_buffer_size);
    if (file > 0) {
        switch (rval->type) {
        case F_DUMPFILE:
        case F_CONT_DUMPFILE:
        case F_SPLIT_DUMPFILE:
            /* Chain up. */
            if (parent_class->seek_file) {
                parent_class->seek_file(pself, file);
            }
            return rval;
        default:
            amfree(rval);
            release_file(self);
            return NULL;
        }
    } else if (file == 0) {
        return rval;
    } else {
        amfree(rval);
        return NULL;
    }
}

static gboolean 
vfs_device_seek_block (Device * pself, guint64 block) {
    VfsDevice * self;
    off_t result;

    self = VFS_DEVICE(pself);
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (self->open_file_fd >= 0, FALSE);
    g_assert(sizeof(off_t) >= sizeof(guint64));

    /* Pretty simple. We figure out the blocksize and use that. */
    result = lseek(self->open_file_fd,
                   (block) * self->block_size + VFS_DEVICE_LABEL_SIZE,
                   SEEK_SET);
    return (result != (off_t)(-1));
}

static gboolean
vfs_device_property_get (Device * pself, DevicePropertyId ID, GValue * val) {
    VfsDevice * self;
    self = VFS_DEVICE(pself);
    g_return_val_if_fail(self != NULL, FALSE);
    if (ID == PROPERTY_BLOCK_SIZE) {
        g_value_unset_init(val, G_TYPE_INT);
        g_value_set_int(val, self->block_size);
        return TRUE;
    } else if (ID == PROPERTY_MAX_VOLUME_USAGE) {
        g_value_unset_init(val, G_TYPE_UINT64);
        g_value_set_uint64(val, self->volume_limit);
        return TRUE;
    } else if (ID == PROPERTY_FREE_SPACE) {
	QualifiedSize qsize;
	struct fs_usage fsusage;
	guint64 bytes_avail;

	if (get_fs_usage(self->dir_name, NULL, &fsusage) == 0) {
	    if (fsusage.fsu_bavail_top_bit_set)
		bytes_avail = 0;
	    else
		bytes_avail = fsusage.fsu_bavail * fsusage.fsu_blocksize;
	    if (self->volume_limit && (guint64)self->volume_limit < bytes_avail / 1024)
		bytes_avail = (guint64)self->volume_limit * 1024;

	    qsize.accuracy = SIZE_ACCURACY_REAL;
	    qsize.bytes = bytes_avail;
	} else {
	    g_warning(_("get_fs_usage('%s') failed: %s"), self->dir_name, strerror(errno));
	    qsize.accuracy = SIZE_ACCURACY_UNKNOWN;
	    qsize.bytes = 0;
	}
	g_value_unset_init(val, QUALIFIED_SIZE_TYPE);
	g_value_set_boxed(val, &qsize);
	return TRUE;
    } else {
        if (parent_class->property_get) {
            return parent_class->property_get(pself, ID, val);
        } else {
            return FALSE;
        }
    }
    g_assert_not_reached();
}

static gboolean 
vfs_device_property_set (Device * pself, DevicePropertyId ID, GValue * val) {
    VfsDevice * self;
    self = VFS_DEVICE(pself);
    g_return_val_if_fail(self != NULL, FALSE);
    if (ID == PROPERTY_BLOCK_SIZE) {
        int block_size = g_value_get_int(val);
        g_return_val_if_fail(block_size > 0, FALSE);
        self->block_size = block_size;
        return TRUE;
    } else if (ID == PROPERTY_MAX_VOLUME_USAGE) {
        self->volume_limit = g_value_get_uint64(val);
        return TRUE;
    } else {
        if (parent_class->property_get) {
            return parent_class->property_get(pself, ID, val);
        } else {
            return FALSE;
        }
    }
    g_assert_not_reached();
}

static gboolean try_unlink(const char * file) {
    if (unlink(file) < 0) {
        g_fprintf(stderr, "Can't unlink file %s: %s\n", file, strerror(errno));
        return FALSE;
    } else {
        return TRUE;
    }
}

static gboolean 
vfs_device_recycle_file (Device * pself, guint filenum) {
    VfsDevice * self;
    struct stat file_status;
    off_t file_size;

    self = VFS_DEVICE(pself);
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(!(pself->in_file), FALSE);

    /* Game Plan:
     * 1) Get a write lock on the file in question.
     * 2) Unlink the file in question.
     * 3) Unlink the lock.
     * 4) Release the lock.
     * FIXME: Is it OK to unlink the lockfile?
     */

    self->file_name = file_number_to_file_name(self, filenum);

    if (self->file_name == NULL)
        return FALSE;

    if (!open_lock(self, filenum, TRUE))
        return FALSE;

    if (0 != stat(self->file_name, &file_status)) {
        fprintf(stderr, "Cannot stat file %s (%s), so not removing.\n",
                self->file_name, strerror(errno));
        return FALSE;
    }
    file_size = file_status.st_size;
    
    if (!try_unlink(self->file_name) ||
        !try_unlink(self->file_lock_name)) {
        release_file(self);
        return FALSE;
    }

    self->volume_bytes -= file_size;
    release_file(self);
    return TRUE;
}

static IoResult vfs_device_robust_read(VfsDevice * self, void *buf,
                                             int *count) {
    int fd = self->open_file_fd;
    int want = *count, got = 0;

    while (got < want) {
        int result;
        result = read(fd, buf + got, want - got);
        if (result > 0) {
            got += result;
        } else if (result == 0) {
            /* end of file */
            if (got == 0) {
                return RESULT_NO_DATA;
            } else {
                *count = got;
                return RESULT_SUCCESS;
            }
        } else if (0
#ifdef EAGAIN
                || errno == EAGAIN
#endif
#ifdef EWOULDBLOCK
                || errno == EWOULDBLOCK
#endif
#ifdef EINTR
                || errno == EINTR
#endif
                   ) {
            /* Try again. */
            continue;
        } else {
            /* Error occured. */
            g_fprintf(stderr, "Error reading fd %d: %s\n", fd, strerror(errno));
            *count = got;
            return -1;
        }
    }

    *count = got;
    return RESULT_SUCCESS;
}

static IoResult
vfs_device_robust_write(VfsDevice * self,  void *buf, int count) {
    int fd = self->open_file_fd;
    int rval = 0;

    while (rval < count) {
        int result;
        result = write(fd, buf + rval, count - rval);
        if (result > 0) {
            rval += result;
            continue;
        } else if (0
#ifdef EAGAIN
                || errno == EAGAIN
#endif
#ifdef EWOULDBLOCK
                || errno == EWOULDBLOCK
#endif
#ifdef EINTR
                || errno == EINTR
#endif
                   ) {
            /* Try again. */
            continue;
        } else if (0
#ifdef EFBIG
                   || errno == EFBIG
#endif
#ifdef ENOSPC
                   || errno == ENOSPC
#endif
                   ) {
            /* We are definitely out of space. */
            return RESULT_NO_SPACE;
        } else {
            /* Error occured. Note that here we handle EIO as an error. */
            g_fprintf(stderr, "Error writing device fd %d: %s\n",
                    fd, strerror(errno));
            
            return RESULT_ERROR;
        }
    }
    return RESULT_SUCCESS;
}
