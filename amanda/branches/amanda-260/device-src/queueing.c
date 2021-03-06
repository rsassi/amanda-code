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

#include "queueing.h"
#include "device.h"
#include "semaphore.h"
#include "amanda.h"

/* Queueing framework here. */
typedef struct {
    guint block_size;
    ProducerFunctor producer;
    gpointer producer_user_data;
    ConsumerFunctor consumer;
    gpointer consumer_user_data;
    GAsyncQueue *data_queue, *free_queue;
    semaphore_t *free_memory;
    StreamingRequirement streaming_mode;
} queue_data_t;

static queue_buffer_t *invent_buffer(void) {
    queue_buffer_t *rval;
    rval = malloc(sizeof(*rval));

    rval->data = NULL;
    rval->alloc_size = 0;
    rval->data_size = 0;
    rval->offset = 0;

    return rval;
}

void free_buffer(queue_buffer_t *buf) {
    if (buf != NULL)
        amfree(buf->data);
    amfree(buf);
}

static queue_buffer_t * merge_buffers(queue_buffer_t *buf1,
                                      queue_buffer_t *buf2) {
    if (buf1 == NULL)
        return buf2;
    else if (buf2 == NULL)
        return buf1;

    if (buf2->offset >= buf1->data_size) {
        /* We can fit buf1 at the beginning of buf2. */
        memcpy(buf2->data + buf2->offset - buf1->data_size,
               buf1->data + buf1->offset,
               buf1->data_size);
        buf2->offset -= buf1->data_size;
        buf2->data_size += buf1->data_size;
        free_buffer(buf1);
        return buf2;
    } else if (buf1->alloc_size - buf1->offset - buf1->data_size
               >= buf2->data_size) {
        /* We can fit buf2 at the end of buf1. */
        memcpy(buf1->data + buf1->offset + buf1->data_size,
               buf2->data + buf2->offset, buf2->data_size);
        buf1->data_size += buf2->data_size;
        free_buffer(buf2);
        return buf1;
    } else {
        /* We can grow buf1 and put everything there. */
        if (buf1->offset != 0) {
            /* But first we have to fix up buf1. */
            memmove(buf1->data, buf1->data + buf1->offset, buf1->data_size);
            buf1->offset = 0;
        }
        buf1->alloc_size = buf1->data_size + buf2->data_size;
        buf1->data = realloc(buf1->data, buf1->alloc_size);
        memcpy(buf1->data + buf1->data_size, buf2->data + buf2->offset,
               buf2->data_size);
        buf1->data_size = buf1->alloc_size;
        free_buffer(buf2);
        return buf1;
    }
}

/* Invalidate the first "bytes" bytes of the buffer, by adjusting the
   offset and data size. */
static void consume_buffer(queue_buffer_t* buf, int bytes) {
    buf->offset += bytes;
    buf->data_size -= bytes;
}

/* Looks at the buffer to see how much free space it has. If it has more than
 * twice the data size of unused space at the end, or more than four times
 * the data size of unused space at the beginning, then that space is
 * reclaimed. */
static void heatshrink_buffer(queue_buffer_t *buf) {
    if (buf == NULL)
        return;

    if (G_UNLIKELY(buf->data_size * 4 > buf->offset)) {
        /* Consolodate with memmove. We will reclaim the space in the next
         * step. */
        memmove(buf->data, buf->data + buf->offset, buf->data_size);
        buf->offset = 0;
    } 

    if (buf->alloc_size > buf->data_size*2 + buf->offset) {
        buf->alloc_size = buf->data_size + buf->offset;
        buf->data = realloc(buf->data, buf->alloc_size);
    }
}

static gpointer do_producer_thread(gpointer datap) {
    queue_data_t* data = datap;

    for (;;) {
        queue_buffer_t *buf;
        gboolean result;

        semaphore_decrement(data->free_memory, 0);
        buf = g_async_queue_try_pop(data->free_queue);
        if (buf != NULL && buf->data == NULL) {
            /* Consumer is finished, then so are we. */
            amfree(buf);
            return GINT_TO_POINTER(TRUE);
        }

        if (buf == NULL) {
            buf = invent_buffer();
        }
        buf->offset = 0;
        buf->data_size = 0;

        result = data->producer(data->producer_user_data, buf,
                                data->block_size);

        // Producers can allocate way too much memory.
        heatshrink_buffer(buf);

        if (buf->data_size > 0) {
            semaphore_force_adjust(data->free_memory, -buf->alloc_size);
            
            g_async_queue_push(data->data_queue, buf);
            buf = NULL;
        } else {
            g_assert(result != PRODUCER_MORE);
            free_buffer(buf);
            buf = NULL;
        }


        if (result == PRODUCER_MORE) {
            continue;
        } else {
            /* We are finished (and the first to do so). */
            g_async_queue_push(data->data_queue, invent_buffer());
            semaphore_force_set(data->free_memory, INT_MIN);

            return GINT_TO_POINTER(result == PRODUCER_FINISHED);
        }
    }
}

static gpointer do_consumer_thread(gpointer datap) {
    queue_data_t* data = datap;
    gboolean finished = FALSE;
    queue_buffer_t *buf = NULL;

    if (data->streaming_mode != STREAMING_REQUIREMENT_NONE) {
        semaphore_wait_empty(data->free_memory);
    }

    for (;;) {
        gboolean result;

        if (finished) {
            return GINT_TO_POINTER(TRUE);
        }

        while (buf == NULL || buf->data_size < data->block_size) {
            queue_buffer_t *next_buf;
            if (data->streaming_mode == STREAMING_REQUIREMENT_DESIRED) {
                do {
                    next_buf = g_async_queue_try_pop(data->data_queue);
                    if (next_buf == NULL) {
                        semaphore_wait_empty(data->free_memory);
                    }
                } while (next_buf == NULL);
            } else {
                next_buf = g_async_queue_pop(data->data_queue);
                g_assert(next_buf != NULL);
            }

            if (next_buf->data == NULL) {
                /* Producer is finished, then so are we.*/
                free_buffer(next_buf);
                if (buf != NULL) {
                    /* But we can't quit yet, we have a buffer to flush.*/
                    finished = TRUE;
                    break;
                } else {
                    /* We are so outta here. */
                    return GINT_TO_POINTER(TRUE);
                }            
            }

            semaphore_increment(data->free_memory, next_buf->alloc_size);
            
            buf = merge_buffers(buf, next_buf);
        }

        result = data->consumer(data->consumer_user_data, buf);

        if (result > 0) {
            consume_buffer(buf, result);
            if (buf->data_size == 0) {
                g_async_queue_push(data->free_queue, buf);
                buf = NULL;
            }
            continue;
        } else {
            free_buffer(buf);
            return GINT_TO_POINTER(FALSE);
        }
    }
}

/* Empties a buffer queue and frees all the buffers associated with it.
 *
 * If full_cleanup is TRUE, then we delete the queue itself.
 * If full_cleanup is FALSE, then we leave the queue around, with a
 *         signal element in it. */
static void cleanup_buffer_queue(GAsyncQueue *Q, gboolean full_cleanup) {
    g_async_queue_lock(Q);
    for (;;) {
        queue_buffer_t *buftmp;
        buftmp = g_async_queue_try_pop_unlocked(Q);
        if (buftmp == NULL)
            break;

        free_buffer(buftmp);
    }
    if (!full_cleanup)
        g_async_queue_push_unlocked(Q, invent_buffer());

    g_async_queue_unlock(Q);
    
    if (full_cleanup)
        g_async_queue_unref(Q);
}

/* This function sacrifices performance, but will still work just
   fine, on systems where threads are not supported. */
static queue_result_flags
do_unthreaded_consumer_producer_queue(guint block_size,
                                      ProducerFunctor producer,
                                      gpointer producer_user_data,
                                      ConsumerFunctor consumer,
                                      gpointer consumer_user_data) {
    queue_buffer_t *buf = NULL, *next_buf = NULL;
    gboolean finished = FALSE;
    queue_result_flags rval = 0;

    /* The basic theory of operation here is to read until we have
       enough data to write, then write until we don't.. */
    while (!finished) {
        int result;
        
        while ((buf == NULL || buf->data_size < block_size) && !finished) {
            if (next_buf == NULL)
                next_buf = invent_buffer();

            result = producer(producer_user_data, next_buf, block_size);

            if (result != PRODUCER_MORE) {
                finished = TRUE;
                if (result != PRODUCER_FINISHED) {
                    rval |= QUEUE_PRODUCER_ERROR;
                }
            }

            buf = merge_buffers(buf, next_buf);
            next_buf = NULL;
        }

        while (buf != NULL && buf->data_size > 0 &&
               (buf->data_size >= block_size || finished)) {
            result = consumer(consumer_user_data, buf);
            
            if (result > 0) {
                consume_buffer(buf, result);
                if (buf->data_size == 0) {
                    next_buf = buf;
                    buf = NULL;
                }
            } else {
                finished = TRUE;
                rval |= QUEUE_CONSUMER_ERROR;
                break;
            }
        }
    }

    free_buffer(buf);
    free_buffer(next_buf);
    return rval;
}

gboolean do_consumer_producer_queue(ProducerFunctor producer,
                                    gpointer producer_user_data,
                                    ConsumerFunctor consumer,
                                    gpointer consumer_user_data) {
    return QUEUE_SUCCESS ==
        do_consumer_producer_queue_full(producer, producer_user_data,
                                        consumer, consumer_user_data,
                                        0, DEFAULT_MAX_BUFFER_MEMORY,
                                        STREAMING_REQUIREMENT_NONE);
}

queue_result_flags
do_consumer_producer_queue_full(ProducerFunctor producer,
                                gpointer producer_user_data,
                                ConsumerFunctor consumer,
                                gpointer consumer_user_data,
                                int block_size,
                                size_t max_memory,
                                StreamingRequirement streaming_mode) {
    GThread     * producer_thread;
    GThread     * consumer_thread;
    queue_data_t  queue_data;
    gpointer      producer_result;
    gpointer      consumer_result;
    queue_result_flags rval;

    if (block_size <= 0) {
        block_size = DISK_BLOCK_BYTES;
    }

    g_return_val_if_fail(producer != NULL, FALSE);
    g_return_val_if_fail(consumer != NULL, FALSE);

    if (!g_thread_supported()) {
        return do_unthreaded_consumer_producer_queue(block_size, producer,
                                                     producer_user_data,
                                                     consumer,
                                                     consumer_user_data);
    }

    queue_data.block_size = block_size;
    queue_data.producer = producer;
    queue_data.producer_user_data = producer_user_data;
    queue_data.consumer = consumer;
    queue_data.consumer_user_data = consumer_user_data;
    queue_data.streaming_mode = streaming_mode;

    queue_data.data_queue = g_async_queue_new();
    queue_data.free_queue = g_async_queue_new();

    max_memory = MAX(1,MIN(max_memory, INT_MAX / 2));
    queue_data.free_memory = semaphore_new_with_value(max_memory);

    producer_thread = g_thread_create(do_producer_thread, &queue_data,
                                      TRUE,
                                      NULL /* FIXME: Should handle
                                              errors. */);
    consumer_thread = g_thread_create(do_consumer_thread, &queue_data,
                                      TRUE,
                                      NULL /* FIXME: Should handle
                                              errors. */);
    
    /* The order of cleanup here is very important, to avoid deadlock. */
    /* 1) Reap the consumer. */
    consumer_result = g_thread_join(consumer_thread);
    /* 2) Stop the producer. */
    semaphore_force_set(queue_data.free_memory, -1);
    /* 3) Cleanup the free queue; add a signal flag. */
    cleanup_buffer_queue(queue_data.free_queue, FALSE);
    /* 4) Restart the producer (so it can exit). */
    semaphore_force_set(queue_data.free_memory, INT_MAX);
    /* 5) Reap the producer. */
    producer_result = g_thread_join(producer_thread);

    cleanup_buffer_queue(queue_data.free_queue, TRUE);
    cleanup_buffer_queue(queue_data.data_queue, TRUE);

    semaphore_free(queue_data.free_memory);
    
    rval = 0;
    if (!GPOINTER_TO_INT(producer_result)) {
        rval |= QUEUE_PRODUCER_ERROR;
    }
    if (!GPOINTER_TO_INT(consumer_result)) {
        rval |= QUEUE_CONSUMER_ERROR;
    }
    return rval;
}

/* Commonly-useful producers and consumers below. */

producer_result_t device_read_producer(gpointer devicep,
                                       queue_buffer_t *buffer,
                                       int hint_size G_GNUC_UNUSED) {
    Device* device;

    device = (Device*) devicep;
    g_assert(IS_DEVICE(device));

    buffer->offset = 0;
    for (;;) {
        int result, read_size;
        read_size = buffer->alloc_size;
        result = device_read_block(device, buffer->data, &read_size);
        if (result > 0) {
            buffer->data_size = read_size;
            return PRODUCER_MORE;
        } else if (result == 0) {
            buffer->data = realloc(buffer->data, read_size);
            buffer->alloc_size = read_size;
        } else if (device->is_eof) {
            return PRODUCER_FINISHED;
        } else {
            buffer->data_size = 0;
            return PRODUCER_ERROR;
        }
    }
}

int device_write_consumer(gpointer devicep, queue_buffer_t *buffer) {
    Device* device;
    unsigned int write_size;
    static gboolean wrote_blocksize = FALSE;

    device = (Device*) devicep;
    g_assert(IS_DEVICE(device));
    write_size = MIN(buffer->data_size,
                     device_write_max_size(device));

    if (!wrote_blocksize) {
	wrote_blocksize = TRUE;
	dbprintf("USING BLOCKSIZE %d bytes\n", write_size);
    }

    if (device_write_block(device, write_size,
                           buffer->data + buffer->offset,
                           buffer->data_size <
                               device_write_min_size(device))) {
        /* Success! */
        return write_size;
    } else {
        /* Nope, really an error. */
        return -1;
    }
}

producer_result_t fd_read_producer(gpointer fdp, queue_buffer_t *buffer,
                                   int hint_size) {
    int fd;

    fd = GPOINTER_TO_INT(fdp);
    g_assert(fd >= 0);
    g_assert(buffer->data_size == 0);

    buffer->offset = 0;

    if (buffer->data == NULL) {
        /* Set up the buffer. */
        buffer->data = malloc(hint_size);
        buffer->alloc_size = hint_size;
    }

    for (;;) {
        int result;
        result = read(fd, buffer->data, buffer->alloc_size);

        if (result > 0) {
            buffer->data_size = result;
            return PRODUCER_MORE;
        } else if (result == 0) {
            /* End of file. */
            return PRODUCER_FINISHED;
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
            return PRODUCER_ERROR;
        }
    }
}

int fd_write_consumer(gpointer fdp, queue_buffer_t *buffer) {
    int fd;

    fd = GPOINTER_TO_INT(fdp);
    g_assert(fd >= 0);

    g_return_val_if_fail(buffer->data_size > 0, 0);

    for (;;) {
        int write_size;
        write_size = write(fd, buffer->data + buffer->offset,
                           buffer->data_size);
        
        if (write_size > 0) {
            return write_size;
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
            g_fprintf(stderr, "Error writing fd %d: %s\n", fd, strerror(errno));
            return -1;
        }        
    }
}
