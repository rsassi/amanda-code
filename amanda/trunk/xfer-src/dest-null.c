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

/*
 * Class declaration
 *
 * This declaration is entirely private; nothing but xfer_dest_null() references
 * it directly.
 */

GType xfer_dest_null_get_type(void);
#define XFER_DEST_NULL_TYPE (xfer_dest_null_get_type())
#define XFER_DEST_NULL(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_dest_null_get_type(), XferDestNull)
#define XFER_DEST_NULL_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_dest_null_get_type(), XferDestNull const)
#define XFER_DEST_NULL_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), xfer_dest_null_get_type(), XferDestNullClass)
#define IS_XFER_DEST_NULL(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), xfer_dest_null_get_type ())
#define XFER_DEST_NULL_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), xfer_dest_null_get_type(), XferDestNullClass)

static GObjectClass *parent_class = NULL;

/*
 * Main object structure
 */

typedef struct XferDestNull {
    XferElement __parent__;

    gboolean debug_print;
    gboolean sent_info;
} XferDestNull;

/*
 * Class definition
 */

typedef struct {
    XferElementClass __parent__;
} XferDestNullClass;

/*
 * Implementation
 */

static void
push_buffer_impl(
    XferElement *elt,
    gpointer bufp,
    size_t len)
{
    XferDestNull *self = (XferDestNull *)elt;
    char *buf = bufp;

    if (!buf) {
	if (self->debug_print)
	    g_printf("dest-null: EOF\n");
	return;
    }

    if (self->debug_print) {
	size_t i;
	g_printf("dest-null: ");
	for (i = 0; i < len; i++) {
	    if (i && (i % 70 == 0))
		g_printf("\n         : ");
	    if (isprint((int)buf[i]))
		g_printf("%c", buf[i]);
	    else
		g_printf("\\x%02x", (int)buf[i]);
	}
	g_printf("\n");
    }

    if (!self->sent_info) {
	/* send a superfluous message (this is a testing XferElement,
	 * after all) */
	XMsg *msg = xmsg_new((XferElement *)self, XMSG_INFO, 0);
	msg->message = stralloc("Is this thing on?");
	xfer_queue_message(XFER_ELEMENT(self)->xfer, msg);
	self->sent_info = TRUE;
    }

    amfree(buf);
}

static void
class_init(
    XferDestNullClass * selfc)
{
    XferElementClass *klass = XFER_ELEMENT_CLASS(selfc);
    static xfer_element_mech_pair_t mech_pairs[] = {
	{ XFER_MECH_PUSH_BUFFER, XFER_MECH_NONE, 0, 0},
	{ XFER_MECH_NONE, XFER_MECH_NONE, 0, 0},
    };

    klass->push_buffer = push_buffer_impl;

    klass->perl_class = "Amanda::Xfer::Dest::Null";
    klass->mech_pairs = mech_pairs;

    parent_class = g_type_class_peek_parent(selfc);
}

GType
xfer_dest_null_get_type (void)
{
    static GType type = 0;

    if G_UNLIKELY(type == 0) {
        static const GTypeInfo info = {
            sizeof (XferDestNullClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) class_init,
            (GClassFinalizeFunc) NULL,
            NULL /* class_data */,
            sizeof (XferDestNull),
            0 /* n_preallocs */,
            (GInstanceInitFunc) NULL,
            NULL
        };

        type = g_type_register_static (XFER_ELEMENT_TYPE, "XferDestNull", &info, 0);
    }

    return type;
}

/* create an element of this class; prototype is in xfer-element.h */
XferElement *
xfer_dest_null(
    gboolean debug_print)
{
    XferDestNull *self = (XferDestNull *)g_object_new(XFER_DEST_NULL_TYPE, NULL);
    XferElement *elt = XFER_ELEMENT(self);

    self->debug_print = debug_print;

    return elt;
}
