/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 2009, 2010 Zmanda, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contact information: Zmanda Inc., 465 S. Mathilda Ave., Suite 300
 * Sunnyvale, CA 94085, USA, or: http://www.zmanda.com
 */

#ifndef XFER_DEVICE_H
#define XFER_DEVICE_H

#include "amxfer.h"
#include "device.h"

/* A transfer source that reads from a Device. The device must be positioned
 * at the start of a file before the transfer is started.  The transfer will
 * continue until the end of the file.
 *
 * Implemented in xfer-source-device.c
 *
 * @param device: Device object to read from
 * @return: new element
 */
XferElement *xfer_source_device(
    Device *device);

/* A transfer destination that writes bytes to a Device.  The device should have a
 * file started, ready for a device_write_block call.  On completion of the transfer,
 * the file will be finished.
 *
 * Implemented in xfer-dest-device.c
 *
 * @param device: the Device to write to, with a file started
 * @param max_memory: total amount of memory to use for buffers, or zero
 *                    for a reasonable default.
 * @return: new element
 */
XferElement *xfer_dest_device(
    Device *device,
    size_t max_memory);

/*
 * class declaration for XferDestTaper (an abstract base class)
 */

GType xfer_dest_taper_get_type(void);
#define XFER_DEST_TAPER_TYPE (xfer_dest_taper_get_type())
#define XFER_DEST_TAPER(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_dest_taper_get_type(), XferDestTaper)
#define XFER_DEST_TAPER_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_dest_taper_get_type(), XferDestTaper const)
#define XFER_DEST_TAPER_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), xfer_dest_taper_get_type(), XferDestTaperClass)
#define IS_XFER_DEST_TAPER(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), xfer_dest_taper_get_type ())
#define XFER_DEST_TAPER_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), xfer_dest_taper_get_type(), XferDestTaperClass)

typedef struct XferDestTaper_ {
    XferElement __parent__;
} XferDestTaper;

typedef struct {
    XferElementClass __parent__;

    /* see xfer-device.h for details of these methods */
    void (*start_part)(XferDestTaper *self, gboolean retry_part, dumpfile_t *header);
    void (*use_device)(XferDestTaper *self, Device *device);
    void (*cache_inform)(XferDestTaper *self, const char *filename, off_t offset,
			 off_t length);
    guint64 (*get_part_bytes_written)(XferDestTaper *self);
} XferDestTaperClass;

/* Start writing the next part to the given device.  The part will be written
 * to the device given to use_device, which should be open and properly positioned.
 * It should not have a file open yet.
 *
 * @param self: the XferDestTaper object
 * @param retry_part: retry the previous (incomplete) part if true
 * @param header: part header
 */
void xfer_dest_taper_start_part(
    XferElement *self,
    gboolean retry_part,
    dumpfile_t *header);

/* Prepare to write subsequent parts to the given device.  The device must
 * not be started yet.  It is not necessary to call this method for the first
 * device used in a transfer.
 *
 * @param self: the XferDestTaper object
 * @param device: the device
 */
void xfer_dest_taper_use_device(
    XferElement *self,
    Device *device);

/* Add a slice of data to the cache for the element.  This is used by the taper
 * when reading from holding disk, to tell the element which holding disk files
 * contain the data that might be needed when rewinding, but can be used in any
 * situation where the part data is already on-disk.  The order of calls to this
 * function dictates the order in whch the files will be read, and no gaps or
 * overlaps are supported.  Note, too, that this must be called *before* any of
 * the data in the new file is sent into the transfer.
 *
 * @param self: the XferDestTaper object
 * @param filename: the fully qualified filename of the cache file
 * @param offset: offset into the file at which data begins
 * @param length: length of data in file
 */
void xfer_dest_taper_cache_inform(
    XferElement *self,
    const char *filename,
    off_t offset,
    off_t length);

/* Return the number of bytes written for the current part.
 *
 * @param self: the XferDestTaper object
 */
guint64 xfer_dest_taper_get_part_bytes_written(
    XferElement *self);

/* Constructor for XferDestTaperSplitter, which writes data to devices block by
 * block and handles caching and splitting parts.
 *
 * @param first_device: the first device that will be used with this xfer, used
 *                      to calculate some internal parameters
 * @param max_memory: total amount of memory to use for buffers, or zero
 *                    for a reasonable default.
 * @param part_size: the desired size of each part
 * @param use_mem_cache: if true, use the memory cache
 * @param disk_cache_dirname: if not NULL, this is the directory in which the disk
 *		      cache should be created
 * @return: new element
 */
XferElement *
xfer_dest_taper_splitter(
    Device *first_device,
    size_t max_memory,
    guint64 part_size,
    gboolean use_mem_cache,
    const char *disk_cache_dirname);

/* Constructor for XferDestTaperDirectTCP, which uses DirectTCP to transfer data
 * to devices (which must support the feature).
 *
 * @param first_device: the first device that will be used with this xfer, used
 *                      to calculate some internal parameters
 * @param part_size: the desired size of each part
 * @return: new element
 */
XferElement *
xfer_dest_taper_directtcp(
    Device *first_device,
    guint64 part_size);

/*
 * XferSourceRecovery
 */

/* Create a new XferSourceRecovery object.  Like XferDestTaper instances, this object
 * will not start transferring data until xfer_source_recovery_start_part is called to
 * give the device from which the data should flow.
 *
 * @param first device: teh first device that will be used with this xfer
 * @returns: new element
 */
XferElement *
xfer_source_recovery(
    Device *first_device);

/* Start an XferSourceRecovery reading from a particular, pre-positioned device
 *
 * @param self: XferSourceRecovery object
 * @param device: device to read from
 */
void
xfer_source_recovery_start_part(
    XferElement *elt,
    Device *device);

/* Prepare to read subsequent parts from the given device.  The device must
 * not be started yet.  It is not necessary to call this method for the first
 * device used in a transfer.
 *
 * @param self: the XferSourceRecovery object
 * @param device: the device
 */
void xfer_source_recovery_use_device(
    XferElement *self,
    Device *device);

#endif
