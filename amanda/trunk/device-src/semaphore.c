/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 2005 Zmanda Inc.
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

/* GLib does not provide semaphores, which are useful in queue.c.
   So, we implement it here. */

#include "semaphore.h"
#include "amanda.h"

semaphore_t* semaphore_new_with_value(int value) {
    semaphore_t *rval;

    if (!g_thread_supported())
        return NULL;

    rval = malloc(sizeof(*rval));
    rval->value = value;
    rval->mutex = g_mutex_new();
    rval->decrement_cond = g_cond_new();
    rval->zero_cond = g_cond_new();
    
    if (rval->mutex == NULL || rval->decrement_cond == NULL ||
        rval->zero_cond == NULL) {
        semaphore_free(rval);
        return NULL;
    } else {
        return rval;
    }
}

void semaphore_free(semaphore_t* o) {
    g_mutex_free(o->mutex);
    g_cond_free(o->decrement_cond);
    g_cond_free(o->zero_cond);
    free(o);
}

/* This function checks if the semaphore would be zero or negative
   after adjusting by the given amount. If so, the zero_cond is
   signalled. We assume that the mutex is locked. */
static void check_empty(semaphore_t * o, int adjust) {
    if (o->value - adjust <= 0) {
        g_cond_broadcast(o->zero_cond);
    }
}

void semaphore_increment(semaphore_t* o, unsigned int inc) {
    g_return_if_fail(o != NULL);
    g_return_if_fail(inc != 0);

    semaphore_force_adjust(o, inc);
}

void semaphore_decrement(semaphore_t* o, unsigned int dec) {
    int sdec;
    g_return_if_fail(o != NULL);
    sdec = (int) dec;
    g_return_if_fail(sdec >= 0);

    g_mutex_lock(o->mutex);
    check_empty(o, sdec);
    while (o->value < sdec) {
        g_cond_wait(o->decrement_cond, o->mutex);
    }
    o->value -= sdec;
    g_mutex_unlock(o->mutex);
}

void semaphore_force_adjust(semaphore_t* o, int inc) {
    g_return_if_fail(o != NULL);

    g_mutex_lock(o->mutex);
    o->value += inc;
    check_empty(o, 0);
    g_cond_broadcast(o->decrement_cond);
    g_mutex_unlock(o->mutex);

}

void semaphore_force_set(semaphore_t* o, int value) {
    g_return_if_fail(o != NULL);
    
    g_mutex_lock(o->mutex);
    o->value = value;
    check_empty(o, 0);
    g_cond_broadcast(o->decrement_cond);
    g_mutex_unlock(o->mutex);
    
}

void semaphore_wait_empty(semaphore_t * o) {
    g_return_if_fail(o != NULL);
    
    g_mutex_lock(o->mutex);
    while (o->value > 0) {
        g_cond_wait(o->zero_cond, o->mutex);
    }
    g_mutex_unlock(o->mutex);
}
