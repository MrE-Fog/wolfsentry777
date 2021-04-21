/*
 * wolfsentry_internal.h
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfSentry.
 *
 * wolfSentry is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSentry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef WOLFSENTRY_INTERNAL_H
#define WOLFSENTRY_INTERNAL_H

#include <wolfsentry/wolfsentry.h>

#ifndef offsetof
#define offsetof(structure, element) ((uintptr_t)&(((structure *)0)->element))
#endif

#ifdef WOLFSENTRY_REFCOUNT_TYPE
typedef WOLFSENTRY_REFCOUNT_TYPE wolfsentry_refcount_t;
#else
typedef uint32_t wolfsentry_refcount_t;
#endif

#ifdef WOLFSENTRY_THREADSAFE

#ifdef WOLFSENTRY_HAVE_POSIX_SEMAPHORES

struct wolfsentry_rwlock {
    sem_t sem;
    sem_t sem_read_waiters;
    sem_t sem_write_waiters;
    sem_t sem_read2write_waiters;
    volatile int shared_count;
    volatile int read_waiter_count;
    volatile int write_waiter_count;
    volatile int read2write_waiter_count;
    volatile enum {
        WOLFSENTRY_LOCK_UNLOCKED = 0,
        WOLFSENTRY_LOCK_SHARED,
        WOLFSENTRY_LOCK_EXCLUSIVE
    } state;
};

#endif

#ifdef WOLFSENTRY_HAVE_GNU_ATOMICS

#define WOLFSENTRY_ATOMIC_INCREMENT(i, x) __atomic_add_fetch(&(i),x,__ATOMIC_SEQ_CST)
#define WOLFSENTRY_ATOMIC_INCREMENT_BY_ONE(i) WOLFSENTRY_ATOMIC_INCREMENT(i, 1)
#define WOLFSENTRY_ATOMIC_DECREMENT(i, x) __atomic_sub_fetch(&(i),x,__ATOMIC_SEQ_CST)
#define WOLFSENTRY_ATOMIC_DECREMENT_BY_ONE(i) WOLFSENTRY_ATOMIC_DECREMENT(i, 1)
#define WOLFSENTRY_ATOMIC_POSTINCREMENT(i, x) __atomic_fetch_add(&(i),x,__ATOMIC_SEQ_CST)
#define WOLFSENTRY_ATOMIC_POSTDECREMENT(i, x) __atomic_fetch_sub(&(i),x,__ATOMIC_SEQ_CST)

#define WOLFSENTRY_ATOMIC_UPDATE(i, set_i, clear_i, pre_i, post_i)      \
do {                                                                    \
    *(pre_i) = (i);                                                     \
    for (;;) {                                                          \
        *(post_i) = (*(pre_i) | (set_i)) & ~(clear_i);                  \
        if (*(post_i) == *(pre_i))                                      \
            break;                                                      \
        if (__atomic_compare_exchange_n(                                \
                &(i),                                                   \
                (pre_i),                                                \
                *(post_i),                                              \
                0 /* weak */,                                           \
                __ATOMIC_SEQ_CST /* success_memmodel */,                \
                __ATOMIC_SEQ_CST /* failure_memmodel */))               \
            break;                                                      \
    }                                                                   \
} while (0)

#endif /* WOLFSENTRY_HAVE_GNU_ATOMICS */

#else /* !WOLFSENTRY_THREADSAFE */

#define WOLFSENTRY_ATOMIC_INCREMENT(i, x) ((i) += (x))
#define WOLFSENTRY_ATOMIC_INCREMENT_BY_ONE(i) (++(i))
#define WOLFSENTRY_ATOMIC_DECREMENT(i, x) ((i) -= (x))
#define WOLFSENTRY_ATOMIC_DECREMENT_BY_ONE(i) (--(i))

#define WOLFSENTRY_ATOMIC_UPDATE(i, set_i, clear_i, pre_i, post_i)      \
do {                                                                    \
    *(pre_i) = (i);                                                     \
    *(post_i) = (*(pre_i) | (set_i)) & ~(clear_i);                      \
    if (*(post_i) != *(pre_i))                                          \
        (i) = *(post_i);                                                \
} while (0)

#endif /* WOLFSENTRY_THREADSAFE */

#define WOLFSENTRY_REFCOUNT_INCREMENT(x) WOLFSENTRY_ATOMIC_INCREMENT_BY_ONE(x)
#define WOLFSENTRY_REFCOUNT_DECREMENT(x) WOLFSENTRY_ATOMIC_DECREMENT_BY_ONE(x)

struct wolfsentry_table_header;

struct wolfsentry_table_ent_header {
    struct wolfsentry_table_header *parent_table;
    struct wolfsentry_table_ent_header *prev, *next; /* these will be replaced by red-black table elements later. */
    struct wolfsentry_table_ent_header *prev_by_id, *next_by_id; /* these will be replaced by red-black table elements later. */
    wolfsentry_hitcount_t hitcount;
    wolfsentry_ent_id_t id;
    uint32_t padding1;
    wolfsentry_refcount_t refcount;
};

struct wolfsentry_list_ent_header {
    struct wolfsentry_list_ent_header *prev, *next;
};

struct wolfsentry_list_header {
    struct wolfsentry_list_ent_header *head, *tail;
    /* note no cmp_fn slot */
};

static inline void wolfsentry_list_ent_prepend(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header *ent) {
    ent->prev = NULL;
    if (list->head) {
        list->head->prev = ent;
        ent->next = list->head;
        list->head = ent;
    } else {
        ent->next = NULL;
        list->head = list->tail = ent;
    }
}

static inline void wolfsentry_list_ent_append(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header *ent) {
    ent->next = NULL;
    if (list->tail) {
        list->tail->next = ent;
        ent->prev = list->tail;
        list->tail = ent;
    } else {
        ent->prev = NULL;
        list->head = list->tail = ent;
    }
}

static inline void wolfsentry_list_ent_insert_after(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header *point_ent, struct wolfsentry_list_ent_header *new_ent) {
    if (point_ent == NULL)
        return wolfsentry_list_ent_prepend(list, new_ent);
    new_ent->prev = point_ent;
    if (point_ent->next)
        new_ent->next = point_ent->next;
    else
        list->tail = new_ent;
    point_ent->next = new_ent;
}

static inline void wolfsentry_list_ent_delete(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header *ent) {
    if (ent == list->head) {
        list->head = ent->next;
        if (list->head)
            ent->next->prev = NULL;
        else
            list->tail = NULL;
    } else if (ent == list->tail) {
        list->tail = ent->prev;
        list->tail->next = NULL;
    } else {
        ent->next->prev = ent->prev;
        ent->prev->next = ent->next;
    }
}

static inline void wolfsentry_list_ent_get_first(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header **ent) {
    *ent = list->head;
}

static inline void wolfsentry_list_ent_get_last(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header **ent) {
    *ent = list->tail;
}

static inline void wolfsentry_list_ent_get_next(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header **ent) {
    (void)list;
    *ent = (*ent)->next;
}

static inline void wolfsentry_list_ent_get_prev(struct wolfsentry_list_header *list, struct wolfsentry_list_ent_header **ent) {
    (void)list;
    *ent = (*ent)->prev;
}

struct wolfsentry_context;

typedef int (*wolfsentry_ent_cmp_fn_t)(struct wolfsentry_table_ent_header *left, struct wolfsentry_table_ent_header *right);
typedef wolfsentry_errcode_t (*wolfsentry_ent_free_fn_t)(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header *ent, wolfsentry_action_res_t *action_results);

typedef wolfsentry_errcode_t (*wolfsentry_filter_function_t)(void *context, struct wolfsentry_table_ent_header *object, wolfsentry_action_res_t *action_results);
typedef wolfsentry_errcode_t (*wolfsentry_dropper_function_t)(void *context, struct wolfsentry_table_ent_header *object, wolfsentry_action_res_t *action_results);

struct wolfsentry_table_header {
    struct wolfsentry_table_ent_header *head, *tail; /* these will be replaced by red-black table elements later. */
    wolfsentry_ent_cmp_fn_t cmp_fn;
    wolfsentry_ent_free_fn_t free_fn;
    wolfsentry_hitcount_t n_ents;
    wolfsentry_object_type_t ent_type;
};

struct wolfsentry_cursor {
    struct wolfsentry_table_ent_header *point;
};

struct wolfsentry_action {
    struct wolfsentry_table_ent_header header;
    wolfsentry_action_callback_t handler;
    void *handler_arg;
    byte label_len;
    char label[0];
};

struct wolfsentry_action_table {
    struct wolfsentry_table_header header;
};

struct wolfsentry_action_list_ent {
    struct wolfsentry_list_ent_header header;
    struct wolfsentry_action *action;
};

struct wolfsentry_action_list {
    struct wolfsentry_list_header header;
};

struct wolfsentry_eventconfig_internal {
    struct wolfsentry_eventconfig config; /* note route_private_data_size modified to include padding needed for route_private_data_alignment. */
    size_t route_private_data_padding; /* with top of struct wolfsentry_route aligned to private_data_alignment, this is the padding needed in addr_buf to get aligned.
                                        * note, struct wolfsentry_route is 8-byte-aligned by default, so there are no holes with normal sizeof(void *) alignment.
                                        */
};

struct wolfsentry_event {
    struct wolfsentry_table_ent_header header;

    struct wolfsentry_eventconfig_internal *config;

    struct wolfsentry_action_list action_list; /* in parent/trigger events, this decides whether to insert the route, and/or updates route state.
                                              * in child events, this does the work described immediately below.
                                              */
    struct wolfsentry_event *insert_event; /* child event with setup routines (if any) for routes inserted with this as parent_event. */
    struct wolfsentry_event *match_event; /* child event with state management for routes inserted with this as parent_event. */
    struct wolfsentry_event *delete_event; /* child event with cleanup routines (if any) for routes inserted with this as parent_event. */

    wolfsentry_priority_t priority;

    byte label_len;
    char label[0];
};

struct wolfsentry_event_table {
    struct wolfsentry_table_header header;
};

struct wolfsentry_route_endpoint {
    wolfsentry_port_t sa_port;
    wolfsentry_addr_bits_t addr_len;
    byte extra_port_count;
    byte interface;
};

struct wolfsentry_route {
    struct wolfsentry_table_ent_header header;

    struct wolfsentry_event *parent_event; /* applicable config is parent_event->config or if null, wolfsentry->config */

    wolfsentry_route_flags_t flags;

    wolfsentry_family_t sa_family;
    wolfsentry_proto_t sa_proto;
    struct wolfsentry_route_endpoint remote, local;

    uint16_t data_addr_offset; /* 0 if there's no private_data */
    uint16_t data_addr_size;

    struct wolfsentry_route_metadata meta;

    byte data[0]; /* first the caller's private data area (if any),
                   * then the remote addr in big endian padded up to
                   * nearest byte, then local addr, then
                   * remote_extra_ports, then local_extra_ports.
                   */
};

#define WOLFSENTRY_ROUTE_REMOTE_ADDR(r) ((r)->data + (r)->data_addr_offset)
#define WOLFSENTRY_ROUTE_REMOTE_ADDR_BYTES(r) WOLFSENTRY_BITS_TO_BYTES((r)->remote.addr_len)
#define WOLFSENTRY_ROUTE_LOCAL_ADDR(r) ((r)->data + (r)->data_addr_offset + WOLFSENTRY_ROUTE_REMOTE_ADDR_BYTES(r))
#define WOLFSENTRY_ROUTE_LOCAL_ADDR_BYTES(r) WOLFSENTRY_BITS_TO_BYTES((r)->local.addr_len)
#define WOLFSENTRY_ROUTE_REMOTE_PORT_COUNT(r) (1 + (r)->remote_extra_port_count)
#define WOLFSENTRY_ROUTE_LOCAL_PORT_COUNT(r) (1 + (r)->local_extra_port_count)
#define WOLFSENTRY_ROUTE_REMOTE_EXTRA_PORTS(r) ((wolfsentry_port_t *)(WOLFSENTRY_ROUTE_LOCAL_ADDR(r) + WOLFSENTRY_ROUTE_LOCAL_ADDR_BYTES(r) + ((WOLFSENTRY_ROUTE_REMOTE_ADDR_BYTES(r) + WOLFSENTRY_ROUTE_LOCAL_ADDR_BYTES(r)) & 1)))
#define WOLFSENTRY_ROUTE_LOCAL_EXTRA_PORTS(r) (WOLFSENTRY_ROUTE_REMOTE_EXTRA_PORTS(r) + WOLFSENTRY_ROUTE_REMOTE_PORT_COUNT(r))
#define WOLFSENTRY_ROUTE_BUF_SIZE(r) (WOLFSENTRY_ROUTE_REMOTE_ADDR_BYTES(r) + WOLFSENTRY_ROUTE_LOCAL_ADDR_BYTES(r) + ((WOLFSENTRY_ROUTE_REMOTE_ADDR_BYTES(r) + WOLFSENTRY_ROUTE_LOCAL_ADDR_BYTES(r)) & 1) + (WOLFSENTRY_ROUTE_REMOTE_PORT_COUNT(r) * sizeof(wolfsentry_port_t)) + (WOLFSENTRY_ROUTE_LOCAL_PORT_COUNT(r) * sizeof(wolfsentry_port_t)))

#define WOLFSENTRY_ROUTE_REMOTE_PORT_GET(r, i) (i ? WOLFSENTRY_ROUTE_REMOTE_EXTRA_PORTS(r)[i-1] : (r)->sa_remote_port)
#define WOLFSENTRY_ROUTE_LOCAL_PORT_GET(r, i) (i ? WOLFSENTRY_ROUTE_LOCAL_EXTRA_PORTS(r)[i-1] : (r)->sa_local_port)

struct wolfsentry_route_table {
    struct wolfsentry_table_header header;
    struct wolfsentry_event *default_event; /* used as the event by wolfsentry_route_dispatch() for a static route match with a null parent_event. */
    wolfsentry_time_t purge_age; /* when now - last_transition_time >= purge_age, purge from the route table. */
    wolfsentry_action_res_t default_policy;
};

struct wolfsentry_context {
#ifdef WOLFSENTRY_THREADSAFE
    struct wolfsentry_rwlock lock;
#endif
    struct wolfsentry_allocator allocator;
    struct wolfsentry_timecbs timecbs;
    wolfsentry_make_id_cb_t mk_id_cb;
    union {
        void *mk_id_cb_arg;
        wolfsentry_ent_id_t id_counter;
    };
    struct wolfsentry_eventconfig_internal config;
    struct wolfsentry_event_table events;
    struct wolfsentry_action_table actions;
    struct wolfsentry_route_table routes_static;
    struct wolfsentry_route_table routes_dynamic;
    struct wolfsentry_table_header ents_by_id;
};

#define WOLFSENTRY_MALLOC(size) wolfsentry->allocator.malloc(wolfsentry->allocator.context, size)
#define WOLFSENTRY_FREE(ptr) wolfsentry->allocator.free(wolfsentry->allocator.context, ptr)
#define WOLFSENTRY_REALLOC(ptr, size) wolfsentry->allocator.realloc(wolfsentry->allocator.context, ptr, size)
#define WOLFSENTRY_MEMALIGN(alignment, size) (wolfsentry->allocator.memalign ? wolfsentry->allocator.memalign(wolfsentry->allocator.context, alignment, size) : NULL)

#define WOLFSENTRY_GET_TIME(time_p) wolfsentry->timecbs.get_time(wolfsentry->timecbs.context, time_p)
#define WOLFSENTRY_DIFF_TIME(later, earlier) wolfsentry->timecbs.diff_time(later, earlier)
#define WOLFSENTRY_ADD_TIME(start_time, time_interval) wolfsentry->timecbs.add_time(start_time, time_interval)
#define WOLFSENTRY_TO_EPOCH_TIME(when, epoch_secs, epoch_nsecs) wolfsentry->timecbs.to_epoch_time(when, epoch_secs, epoch_nsecs)
#define WOLFSENTRY_FROM_EPOCH_TIME(epoch_secs, epoch_nsecs, when) wolfsentry->timecbs.from_epoch_time(epoch_secs, epoch_nsecs, when)
#define WOLFSENTRY_INTERVAL_TO_SECONDS(howlong, howlong_secs, howlong_nsecs) wolfsentry->timecbs.interval_to_seconds(howlong, howlong_secs, howlong_nsecs)
#define WOLFSENTRY_INTERVAL_FROM_SECONDS(howlong_secs, howlong_nsecs, howlong) wolfsentry->timecbs.interval_from_seconds(howlong_secs, howlong_nsecs, howlong)

wolfsentry_errcode_t wolfsentry_id_generate(struct wolfsentry_context *wolfsentry, wolfsentry_object_type_t object_type, wolfsentry_ent_id_t *id);

int wolfsentry_event_key_cmp(struct wolfsentry_event *left, struct wolfsentry_event *right);
int wolfsentry_action_key_cmp(struct wolfsentry_action *left, struct wolfsentry_action *right);
int wolfsentry_route_key_cmp(struct wolfsentry_route *left, struct wolfsentry_route *right);

wolfsentry_errcode_t wolfsentry_table_ent_insert(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header *ent, struct wolfsentry_table_header *table, int unique_p);
wolfsentry_errcode_t wolfsentry_table_ent_get(struct wolfsentry_table_header *table, struct wolfsentry_table_ent_header **ent);
wolfsentry_errcode_t wolfsentry_table_ent_delete(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header **ent);
wolfsentry_errcode_t wolfsentry_table_ent_drop_reference(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header *ent, wolfsentry_action_res_t *action_results);
wolfsentry_errcode_t wolfsentry_table_ent_delete_1(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header *ent);

wolfsentry_errcode_t wolfsentry_table_ent_insert_by_id(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header *ent);
wolfsentry_errcode_t wolfsentry_table_ent_get_by_id(struct wolfsentry_context *wolfsentry, wolfsentry_ent_id_t id, struct wolfsentry_table_ent_header **ent);
void wolfsentry_table_ent_delete_by_id_1(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_ent_header *ent);
wolfsentry_errcode_t wolfsentry_table_ent_delete_by_id(struct wolfsentry_context *wolfsentry, wolfsentry_ent_id_t id, struct wolfsentry_table_ent_header **ent);

wolfsentry_errcode_t wolfsentry_table_free_ents(struct wolfsentry_context *wolfsentry, struct wolfsentry_table_header *table);

wolfsentry_errcode_t wolfsentry_table_cursor_init(struct wolfsentry_context *wolfsentry, struct wolfsentry_cursor *cursor);
wolfsentry_errcode_t wolfsentry_table_cursor_seek_to_head(struct wolfsentry_table_header *table, struct wolfsentry_cursor *cursor);
wolfsentry_errcode_t wolfsentry_table_cursor_seek_to_tail(struct wolfsentry_table_header *table, struct wolfsentry_cursor *cursor);
struct wolfsentry_table_ent_header * wolfsentry_table_cursor_current(struct wolfsentry_cursor *cursor);
struct wolfsentry_table_ent_header * wolfsentry_table_cursor_prev(struct wolfsentry_cursor *cursor);
struct wolfsentry_table_ent_header * wolfsentry_table_cursor_next(struct wolfsentry_cursor *cursor);
wolfsentry_errcode_t wolfsentry_table_cursor_seek(struct wolfsentry_table_header *table, struct wolfsentry_table_ent_header *ent, struct wolfsentry_cursor *cursor, int *cursor_position);

wolfsentry_errcode_t wolfsentry_table_filter(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_table_header *table,
    wolfsentry_filter_function_t filter,
    void *filter_arg,
    wolfsentry_dropper_function_t dropper,
    void *dropper_arg);

wolfsentry_errcode_t wolfsentry_action_list_append(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len);

wolfsentry_errcode_t wolfsentry_action_list_prepend(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len);

wolfsentry_errcode_t wolfsentry_action_list_insert_after(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len,
    const char *point_label,
    int point_label_len);

wolfsentry_errcode_t wolfsentry_action_list_delete(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len);

wolfsentry_errcode_t wolfsentry_action_list_delete_all(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list);

wolfsentry_errcode_t wolfsentry_action_list_dispatch(
    struct wolfsentry_context *wolfsentry,
    void *caller_arg,
    struct wolfsentry_action_list *action_list,
    struct wolfsentry_event *trigger_event,
    struct wolfsentry_route_table *route_table,
    struct wolfsentry_route *route,
    wolfsentry_action_res_t *action_results);

wolfsentry_errcode_t wolfsentry_eventconfig_load(
    const struct wolfsentry_eventconfig *supplied,
    struct wolfsentry_eventconfig_internal *internal);

#endif /* WOLFSENTRY_INTERNAL_H */