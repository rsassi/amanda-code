/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 2008 Zmanda Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "amxfer.h"
#include "amanda.h"
#include "queueing.h"
#include "device-queueing.h"

/*
 * Class declaration
 *
 * This declaration is entirely private; nothing but xfer_dest_device() references
 * it directly.
 */

GType xfer_dest_device_get_type(void);
#define XFER_DEST_DEVICE_TYPE (xfer_dest_device_get_type())
#define XFER_DEST_DEVICE(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_dest_device_get_type(), XferDestDevice)
#define XFER_DEST_DEVICE_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_dest_device_get_type(), XferDestDevice const)
#define XFER_DEST_DEVICE_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), xfer_dest_device_get_type(), XferDestDeviceClass)
#define IS_XFER_DEST_DEVICE(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), xfer_dest_device_get_type ())
#define XFER_DEST_DEVICE_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), xfer_dest_device_get_type(), XferDestDeviceClass)

static GObjectClass *parent_class = NULL;

/*
 * Main object structure
 */

typedef struct XferDestDevice {
    XferElement __parent__;

    Device *device;
    size_t max_memory;

    GThread *thread;
} XferDestDevice;

/*
 * Class definition
 */

typedef struct {
    XferElementClass __parent__;
} XferDestDeviceClass;

/*
 * Implementation
 */

static producer_result_t
pull_buffer_producer(gpointer data,
    queue_buffer_t *buffer,
    size_t hint_size G_GNUC_UNUSED)
{
    XferDestDevice *self = (XferDestDevice *)data;
    gpointer buf;
    size_t size;

    buf = xfer_element_pull_buffer(XFER_ELEMENT(self)->upstream, &size);
    if (!buf) {
	return PRODUCER_FINISHED;
    }

    /* queueing recycles allocated buffers back to us, but we don't need them.. */
    amfree(buffer->data);
    buffer->data = buf;
    buffer->alloc_size = buffer->data_size = size;
    buffer->offset = 0;

    return PRODUCER_MORE;
}

static gpointer
queueing_thread(
    gpointer data)
{
    XferDestDevice *self = (XferDestDevice *)data;
    queue_result_flags result;
    GValue val;
    StreamingRequirement streaming_mode;
    size_t block_size;

    /* Get the device's parameters */
    bzero(&val, sizeof(val));
    if (!device_property_get(self->device, PROPERTY_STREAMING, &val)
	|| !G_VALUE_HOLDS(&val, STREAMING_REQUIREMENT_TYPE)) {
	g_warning("XferDestDevice Couldn't get streaming type for %s", self->device->device_name);
	streaming_mode = STREAMING_REQUIREMENT_REQUIRED;
    } else {
	streaming_mode = g_value_get_enum(&val);
    }

    block_size = device_write_max_size(self->device);

    /* this thread creates two other threads (consumer and producer) and
     * blocks waiting for them to finish.  TODO: when taper no longer uses
     * queueing, merge the queueing functionality here */
    result = do_consumer_producer_queue_full(
		pull_buffer_producer, data,
		device_write_consumer, self->device,
		block_size, self->max_memory,
		streaming_mode);

    /* TODO: handle this better */
    if (result != QUEUE_SUCCESS)
	error("Oh, noes!");

    xfer_queue_message(XFER_ELEMENT(self)->xfer, xmsg_new(XFER_ELEMENT(self), XMSG_DONE, 0));

    return NULL;
}

static gboolean
start_impl(
    XferElement *elt)
{
    XferDestDevice *self = (XferDestDevice *)elt;
    self->thread = g_thread_create(queueing_thread, (gpointer)self, FALSE, NULL);
    return TRUE;
}

static void
class_init(
    XferDestDeviceClass * selfc)
{
    XferElementClass *klass = XFER_ELEMENT_CLASS(selfc);
    static xfer_element_mech_pair_t mech_pairs[] = {
	{ XFER_MECH_PULL_BUFFER, XFER_MECH_NONE, 0, 0},
	/*{ XFER_MECH_READFD, XFER_MECH_NONE, 0, 0}, TODO */
	{ XFER_MECH_NONE, XFER_MECH_NONE, 0, 0},
    };

    klass->start = start_impl;
    klass->perl_class = "Amanda::Xfer::Dest::Device";
    klass->mech_pairs = mech_pairs;

    parent_class = g_type_class_peek_parent(selfc);
}

GType
xfer_dest_device_get_type (void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (XferDestDeviceClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (XferDestDevice),
            0 /* n_preallocs */,
            (GInstanceInitFunc) NULL,
            NULL
        };

        type = g_type_register_static (XFER_ELEMENT_TYPE, "XferDestDevice", &info, 0);
    }

    return type;
}

/* create an element of this class; prototype is in xfer-element.h */
XferElement *
xfer_dest_device(
    Device *device,
    size_t max_memory)
{
    XferDestDevice *self = (XferDestDevice *)g_object_new(XFER_DEST_DEVICE_TYPE, NULL);
    XferElement *elt = XFER_ELEMENT(self);

    g_assert(device != NULL);

    self->device = device;
    self->max_memory = max_memory;

    return elt;
}
