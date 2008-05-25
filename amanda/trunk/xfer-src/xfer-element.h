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

/* Base classes and interfaces for transfer elements.  There are two interfaces
 * defined here: IXferProducer and IXferConsumer.  The former is for elements
 * which produce data, and the latter is for those which consume it.  There is
 * a top-level XferElement base class, which all implementations subclass.
 *
 * Unless you're well-acquainted with GType and GObject, this file will be a
 * difficult read.  It is really only of use to those implementing new subclasses.
 */

#ifndef XFER_ELEMENT_H
#define XFER_ELEMENT_H

#include <glib.h>
#include <glib-object.h>
#include "xfer.h"

typedef enum {
    /* sources have no input mechanisms and destinations have no output
     * mechansisms. */
    XFER_MECH_NONE,

    /* downstream element will read() from elt->upstream->output_fd; EOF
     * is indicated by the usual OS mechanism resulting in a zero-length
     * read, in response to which the downstream element must close
     * the fd. */
    XFER_MECH_READFD,

    /* upstream element will write() to elt->downstream->input_fd.  EOF
     * is indicated by closing the file descriptor. */
    XFER_MECH_WRITEFD,

    /* downstream element will call elt->upstream->pull_buffer() to
     * pull a buffer.  EOF is indicated by returning a NULL buffer */
    XFER_MECH_PULL_BUFFER,

    /* upstream element will call elt->downstream->push_buffer(buf) to push
     * a buffer.  EOF is indicated by passing a NULL buffer. */
    XFER_MECH_PUSH_BUFFER,
} xfer_mech;

/* Description of a pair (input, output) of xfer mechanisms that an
 * element can support, along with the associated costs.  An array of these
 * pairs is stored in the class-level variable 'mech_pairs', describing
 * all of the mechanisms that an element supports.
 */
typedef struct {
    xfer_mech input_mech;
    xfer_mech output_mech;
    guint8 ops_per_byte;	/* number of byte copies or other operations */
    guint8 nthreads;		/* number of additional threads created */
} xfer_element_mech_pair_t;

/***********************
 * XferElement
 *
 * The virtual base class for all transfer elements
 */

GType xfer_element_get_type(void);
#define XFER_ELEMENT_TYPE (xfer_element_get_type())
#define XFER_ELEMENT(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_element_get_type(), XferElement)
#define XFER_ELEMENT_CONST(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), xfer_element_get_type(), XferElement const)
#define XFER_ELEMENT_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), xfer_element_get_type(), XferElementClass)
#define IS_XFER_ELEMENT(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), xfer_element_get_type ())
#define XFER_ELEMENT_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), xfer_element_get_type(), XferElementClass)

/*
 * Main object structure
 */

typedef struct XferElement {
    GObject __parent__;

    /* The transfer to which this element is attached */
    Xfer *xfer; /* set by xfer_new */

    /* assigned input and output mechanisms */
    xfer_mech input_mech;
    xfer_mech output_mech;

    /* neighboring xfer elements */
    struct XferElement *upstream;
    struct XferElement *downstream;

    /* file descriptors for XFER_MECH_READFD and XFER_MECH_WRITEFD.  These should be set
     * during setup(), and can be accessed by neighboring elements during start(). It is
     * up to subclasses to handle closing these file descriptors, if required. */
    gint input_fd;
    gint output_fd;

    /* cache for repr() */
    char *repr;
} XferElement;

/*
 * Class definition
 */

typedef struct {
    GObjectClass __parent__;

    /* Get a string representation of this element.  The returned string will be freed
     * when the element is finalized, and is static until that time.  This method is
     * implemented by XferElement, but can be overridden by classes that can provide
     * additional useful information about themselves.  Overriding methods can use
     * the 'repr' instance variable as a cache -- it will be freed on finalize().
     *
     * @param elt: the XferElement
     * @return: statically allocated string
     */
    char *(*repr)(XferElement *elt);

    /* Set up this element.  This function is called for all elements in a transfer
     * before start() is called for any elements.  For mechanisms where this element
     * supplies a file descriptor, it should set its input_fd and/or output_fd
     * appropriately; neighboring elements will use that value in start().
     *
     * elt->input_mech and elt->output_mech are already set when this function
     * is called, but upstream and downstream are not.
     *
     * @param elt: the XferElement
     */
    void (*setup)(XferElement *elt);

    /* Start transferring data.  The element downstream of this one will already be
     * started, while the upstream element will not, so data will not begin flowing
     * immediately.
     *
     * @param elt: the XferElement
     * @return: TRUE if this element will send XMSG_DONE
     */
    gboolean (*start)(XferElement *elt);

    /* Stop transferring data.  The upstream element's abort method will already have
     * been called, but due to OS buffering, data may yet arrive.  The element may
     * discard any such data, but must not fail.  This function is only called for
     * abnormal terminations; elements should normally stop processing on receiving
     * an EOF indication from upstream.  This method should cause an EOF indication
     * to be sent downstream.
     *
     * @param elt: the XferElement
     */
    void (*abort)(XferElement *elt);

    /* Get a buffer full of data from this element.  This function is called by
     * the downstream element under XFER_MECH_PULL_CALL.  It can block indefinitely,
     * and must only return NULL on EOF.  Responsibility to free the buffer transfers
     * to the caller.
     *
     * @param elt: the XferElement
     * @param size (output): size of resulting buffer
     */
    gpointer (*pull_buffer)(XferElement *elt, size_t *size);

    /* A buffer full of data is being sent to this element for processing; this
     * function is called by the upstream element under XFER_MECH_PUSH_CALL.
     * It can block indefinitely if the data cannot be processed immediately.
     * An EOF condition is signaled by call with a NULL buffer.  Responsibility to
     * free the buffer transfers to the callee.
     *
     * @param elt: the XferElement
     * @param buf: buffer
     * @param size: size of buffer
     */
    void (*push_buffer)(XferElement *elt, gpointer buf, size_t size);

    /* class variables */

    /* This is used by the perl bindings -- it is a class variable giving the
     * appropriate perl class to wrap this XferElement.  It should be set by
     * each class's class_init.
     */
    const char *perl_class;

    /* Statically allocated array of input/output mechanisms supported by this
     * class (terminated by <XFER_MECH_NONE,XFER_MECH_NONE>) */
    xfer_element_mech_pair_t *mech_pairs;
} XferElementClass;

/*
 * Method stubs
 */

void xfer_element_unref(XferElement *elt);
gboolean xfer_element_link_to(XferElement *elt, XferElement *successor);
char *xfer_element_repr(XferElement *elt);
void xfer_element_setup(XferElement *elt);
gboolean xfer_element_start(XferElement *elt);
void xfer_element_push_buffer(XferElement *elt, gpointer buf, size_t size);
gpointer xfer_element_pull_buffer(XferElement *elt, size_t *size);
void xfer_element_abort(XferElement *elt);

/*
 * Methods
 */

/***********************
 * XferElement subclasses
 *
 * These simple subclasses do not introduce any additional public members or
 * methods, so they do not have their own header file.  The functions here
 * provide their only public interface.  The implementation of these elements
 * can also provide a good prototype for new elements.
 */

/* A transfer source that produces LENGTH bytes of random data, for testing
 * purposes.
 *
 * Implemented in source-random.c
 *
 * @param length: bytes to produce
 * @param prng_seed: initial value for random number generator
 * @return: new element
 */
XferElement *xfer_source_random(
    size_t length,
    guint32 prng_seed);

/* A transfer source that provides bytes read from a file descriptor.
 * Reading continues until EOF, but the file descriptor is not closed.
 *
 * Implemented in source-fd.c
 *
 * @param fd: the file descriptor from which to read
 * @return: new element
 */
XferElement * xfer_source_fd(
    int fd);

/* A transfer filter that just applies a bytewise XOR transformation to the data
 * that passes through it.
 *
 * Implemented in filter-xor.c
 *
 * @param xor_key: key for xor operations
 * @return: new element
 */
XferElement *xfer_filter_xor(
    unsigned char xor_key);

/* A transfer destination that consumes all bytes it is given, optionally
 * validating that they match those produced by source_random
 *
 * Implemented in dest-null.c
 *
 * @param prng_seed: if nonzero, validate that the datastream matches
 *	that produced by a random source with this random seed.  If zero,
 *	no validation is performed.
 * @return: new element
 */
XferElement *xfer_dest_null(
    guint32 prng_seed);

/* A transfer destination that writes bytes to a file descriptor.  The file
 * descriptor is not closed when the transfer is complete.
 *
 * Implemented in dest-fd.c
 *
 * @param fd: file descriptor to which to write
 * @return: new element
 */
XferElement *xfer_dest_fd(
    int fd);

#endif
