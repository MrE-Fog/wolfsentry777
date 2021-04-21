/*
 * actions.c
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

#include "wolfsentry_internal.h"

#define WOLFSENTRY_SOURCE_ID WOLFSENTRY_SOURCE_ID_ACTIONS_C

static inline int wolfsentry_action_key_cmp_1(const char *left_label, unsigned const int left_label_len, const char *right_label, unsigned const int right_label_len) {
    int ret;

    if (left_label_len >= right_label_len) {
        ret = memcmp(left_label, right_label, left_label_len);
        if ((ret == 0) && (left_label_len != right_label_len))
            ret = 1;
    } else {
        ret = memcmp(left_label, right_label, right_label_len);
        if (ret == 0)
            ret = -1;
    }

    return ret;
}

int wolfsentry_action_key_cmp(struct wolfsentry_action *left, struct wolfsentry_action *right) {
    return wolfsentry_action_key_cmp_1(left->label, left->label_len, right->label, right->label_len);
}

static wolfsentry_errcode_t wolfsentry_action_init_1(const char *label, int label_len, wolfsentry_action_callback_t handler, void *handler_arg, struct wolfsentry_action *action, size_t action_size) {
    if (label_len <= 0)
        WOLFSENTRY_ERROR_RETURN(INVALID_ARG);

    if (action_size < sizeof *action + (size_t)label_len)
        WOLFSENTRY_ERROR_RETURN(BUFFER_TOO_SMALL);

    action->handler = handler;
    action->handler_arg = handler_arg;
    memcpy(action->label, label, (size_t)label_len);
    action->label_len = (byte)label_len;

    action->header.refcount = 1;
    action->header.id = WOLFSENTRY_ENT_ID_NONE;

    WOLFSENTRY_RETURN_OK;
}

static wolfsentry_errcode_t wolfsentry_action_new_1(struct wolfsentry_context *wolfsentry, const char *label, int label_len, wolfsentry_action_callback_t handler, void *handler_arg, struct wolfsentry_action **action) {
    size_t new_size;
    wolfsentry_errcode_t ret;

    if ((label_len == 0) || (label_len > WOLFSENTRY_MAX_LABEL_BYTES) || (label == NULL) || (handler == NULL) || (action == NULL))
        WOLFSENTRY_ERROR_RETURN(INVALID_ARG);

    if (label_len < 0) {
        label_len = (int)strlen(label);
        if (label_len > WOLFSENTRY_MAX_LABEL_BYTES)
            WOLFSENTRY_ERROR_RETURN(STRING_ARG_TOO_LONG);
    }

    new_size = sizeof **action + (size_t)label_len;

    if ((*action = (struct wolfsentry_action *)WOLFSENTRY_MALLOC(new_size)) == NULL)
        WOLFSENTRY_ERROR_RETURN(SYS_RESOURCE_FAILED);
    ret = wolfsentry_action_init_1(label, label_len, handler, handler_arg, *action, new_size);
    if (ret < 0) {
        WOLFSENTRY_FREE(*action);
        *action = NULL;
    }
    return ret;
}

wolfsentry_errcode_t wolfsentry_action_insert(struct wolfsentry_context *wolfsentry, const char *label, int label_len, wolfsentry_action_callback_t handler, void *handler_arg, wolfsentry_ent_id_t *id) {
    struct wolfsentry_action *new;
    wolfsentry_errcode_t ret;

    if ((ret = wolfsentry_action_new_1(wolfsentry, label, label_len, handler, handler_arg, &new)) < 0)
        return ret;
    if ((ret = wolfsentry_id_generate(wolfsentry, WOLFSENTRY_OBJECT_TYPE_ACTION, &new->header.id)) < 0) {
        WOLFSENTRY_FREE(new);
        return ret;
    }
    if (id)
        *id = new->header.id;
    if ((ret = wolfsentry_table_ent_insert(wolfsentry, &new->header, &wolfsentry->actions.header, 1 /* unique_p */)) < 0) {
        WOLFSENTRY_FREE(new);
        WOLFSENTRY_ERROR_RERETURN(ret);
    }
    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_delete(struct wolfsentry_context *wolfsentry, const char *label, int label_len, wolfsentry_action_res_t *action_results) {
    wolfsentry_errcode_t ret;
    struct {
        struct wolfsentry_action action;
        byte buf[WOLFSENTRY_MAX_LABEL_BYTES];
    } target;
    struct wolfsentry_table_ent_header *target_p = &target.action.header;

    if ((ret = wolfsentry_action_init_1(label, label_len, NULL, NULL, &target.action, sizeof target)) < 0)
        return ret;

    if ((ret = wolfsentry_table_ent_delete(wolfsentry, &target_p)) < 0)
        return ret;

    return wolfsentry_table_ent_drop_reference(wolfsentry, target_p, action_results);
}

static wolfsentry_errcode_t wolfsentry_action_get_reference_1(struct wolfsentry_context *wolfsentry, const struct wolfsentry_action *action_template, struct wolfsentry_action **action) {
    wolfsentry_errcode_t ret;
    struct wolfsentry_action *ret_action = (struct wolfsentry_action *)action_template;
    if ((ret = wolfsentry_table_ent_get(&wolfsentry->actions.header, (struct wolfsentry_table_ent_header **)&ret_action)) < 0)
        return ret;
    WOLFSENTRY_ATOMIC_INCREMENT(ret_action->header.refcount, 1);
    *action = ret_action;
    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_get_reference(struct wolfsentry_context *wolfsentry, const char *label, int label_len, struct wolfsentry_action **action) {
    struct wolfsentry_action *action_template;
    wolfsentry_errcode_t ret;
    if (label_len <= 0)
        WOLFSENTRY_ERROR_RETURN(INVALID_ARG);
    if (label_len > WOLFSENTRY_MAX_LABEL_BYTES)
        WOLFSENTRY_ERROR_RETURN(STRING_ARG_TOO_LONG);
    if ((action_template = WOLFSENTRY_MALLOC(sizeof *action_template + (size_t)label_len)) == NULL)
        WOLFSENTRY_ERROR_RETURN(SYS_RESOURCE_FAILED);
    action_template->label_len = (byte)label_len;
    memcpy(action_template->label, label, (size_t)label_len);
    ret = wolfsentry_action_get_reference_1(wolfsentry, action_template, action);
    WOLFSENTRY_FREE(action_template);
    return ret;
}

wolfsentry_errcode_t wolfsentry_action_drop_reference(struct wolfsentry_context *wolfsentry, const struct wolfsentry_action *action, wolfsentry_action_res_t *action_results) {
    return wolfsentry_table_ent_drop_reference(wolfsentry, (struct wolfsentry_table_ent_header *)action, action_results);
}

static inline int wolfsentry_action_list_find_1(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    struct wolfsentry_action *action,
    struct wolfsentry_action_list_ent **action_list_ent)
{
    struct wolfsentry_action_list_ent *i;
    (void)wolfsentry;
    for (wolfsentry_list_ent_get_first(&action_list->header, (struct wolfsentry_list_ent_header **)&i);
         i;
         wolfsentry_list_ent_get_next(&action_list->header, (struct wolfsentry_list_ent_header **)&i)) {
        if (i->action == action)
            break;
    }
    if (i) {
        if (action_list_ent)
            *action_list_ent = i;
        WOLFSENTRY_RETURN_OK;
    } else
        WOLFSENTRY_ERROR_RETURN(ITEM_NOT_FOUND);
}

static inline int wolfsentry_action_list_append_1(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    struct wolfsentry_action *action)
{
    struct wolfsentry_action_list_ent *new;
    if (wolfsentry_action_list_find_1(wolfsentry, action_list, action, NULL /* action_list_ent */) == 0)
        WOLFSENTRY_ERROR_RETURN(ITEM_ALREADY_PRESENT);
    if ((new  = (struct wolfsentry_action_list_ent *)WOLFSENTRY_MALLOC(sizeof *new)) == NULL)
        WOLFSENTRY_ERROR_RETURN(SYS_RESOURCE_FAILED);
    new->action = action;
    wolfsentry_list_ent_append(&action_list->header, &new->header);
    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_list_append(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len)
{
    wolfsentry_errcode_t ret;
    struct wolfsentry_action *action;
    if ((ret = wolfsentry_action_get_reference(wolfsentry, label, label_len, &action)) < 0)
        return ret;
    if ((ret = wolfsentry_action_list_append_1(wolfsentry, action_list, action)) < 0) {
        (void)wolfsentry_action_drop_reference(wolfsentry, action, NULL /* action_results */);
        return ret;
    }
    WOLFSENTRY_RETURN_OK;
}

static inline int wolfsentry_action_list_prepend_1(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    struct wolfsentry_action *action)
{
    struct wolfsentry_action_list_ent *new;
    if (wolfsentry_action_list_find_1(wolfsentry, action_list, action, NULL /* action_list_ent */) == 0)
        WOLFSENTRY_ERROR_RETURN(ITEM_ALREADY_PRESENT);
    if ((new  = (struct wolfsentry_action_list_ent *)WOLFSENTRY_MALLOC(sizeof *new)) == NULL)
        WOLFSENTRY_ERROR_RETURN(SYS_RESOURCE_FAILED);
    new->action = action;
    wolfsentry_list_ent_prepend(&action_list->header, &new->header);
    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_list_prepend(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len)
{
    wolfsentry_errcode_t ret;
    struct wolfsentry_action *action;
    if ((ret = wolfsentry_action_get_reference(wolfsentry, label, label_len, &action)) < 0)
        return ret;
    if ((ret = wolfsentry_action_list_prepend_1(wolfsentry, action_list, action)) < 0) {
        WOLFSENTRY_WARN_ON_FAILURE(wolfsentry_action_drop_reference(wolfsentry, action, NULL /* action_results */));
        return ret;
    }
    WOLFSENTRY_RETURN_OK;
}

static inline wolfsentry_errcode_t wolfsentry_action_list_insert_after_1(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    struct wolfsentry_action *action,
    struct wolfsentry_action *point_action)
{
    wolfsentry_errcode_t ret;
    struct wolfsentry_action_list_ent *point = NULL, *new;

    if (wolfsentry_action_list_find_1(wolfsentry, action_list, action, NULL /* action_list_ent */) == 0)
        WOLFSENTRY_ERROR_RETURN(ITEM_ALREADY_PRESENT);
    if ((ret = wolfsentry_action_list_find_1(wolfsentry, action_list, point_action, &point)) < 0)
        return ret;
    if ((new  = (struct wolfsentry_action_list_ent *)WOLFSENTRY_MALLOC(sizeof *new)) == NULL)
        WOLFSENTRY_ERROR_RETURN(SYS_RESOURCE_FAILED);
    new->action = action;
    wolfsentry_list_ent_insert_after(&action_list->header, &point->header, &new->header);
    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_list_insert_after(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len,
    const char *point_label,
    int point_label_len)
{
    wolfsentry_errcode_t ret;
    struct wolfsentry_action *action, *point_action = NULL;

    if ((ret = wolfsentry_action_get_reference(wolfsentry, label, label_len, &action)) < 0)
        return ret;
    if ((ret = wolfsentry_action_get_reference(wolfsentry, point_label, point_label_len, &action)) < 0)
        return ret;
    ret = wolfsentry_action_list_insert_after_1(wolfsentry, action_list, action, point_action);
    (void)wolfsentry_action_drop_reference(wolfsentry, point_action, NULL /* action_results */);
    if (ret < 0) {
        WOLFSENTRY_WARN_ON_FAILURE(wolfsentry_action_drop_reference(wolfsentry, action, NULL /* action_results */));
        return ret;
    }
    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_list_delete(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list,
    const char *label,
    int label_len)
{
    struct wolfsentry_action_list_ent *action_list_ent;

    for (wolfsentry_list_ent_get_first(&action_list->header, (struct wolfsentry_list_ent_header **)&action_list_ent);
         action_list_ent;
         wolfsentry_list_ent_get_next(&action_list->header, (struct wolfsentry_list_ent_header **)&action_list_ent)) {
        if (wolfsentry_action_key_cmp_1(action_list_ent->action->label, action_list_ent->action->label_len, label, (unsigned int)label_len) == 0)
            break;
    }

    if (action_list_ent == NULL)
        WOLFSENTRY_ERROR_RETURN(ITEM_NOT_FOUND);

    wolfsentry_list_ent_delete(&action_list->header, &action_list_ent->header);
    WOLFSENTRY_WARN_ON_FAILURE(wolfsentry_action_drop_reference(wolfsentry, action_list_ent->action, NULL /* action_results */));
    WOLFSENTRY_FREE(action_list_ent);

    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_list_delete_all(
    struct wolfsentry_context *wolfsentry,
    struct wolfsentry_action_list *action_list)
{
    struct wolfsentry_action_list_ent *i, *next;

    for (wolfsentry_list_ent_get_first(&action_list->header, (struct wolfsentry_list_ent_header **)&i);
         i;
         i = next) {
        next = i;
        wolfsentry_list_ent_get_next(&action_list->header, (struct wolfsentry_list_ent_header **)&next);

        wolfsentry_list_ent_delete(&action_list->header, &i->header);
        WOLFSENTRY_WARN_ON_FAILURE(wolfsentry_action_drop_reference(wolfsentry, i->action, NULL /* action_results */));
        WOLFSENTRY_FREE(i);
    }

    WOLFSENTRY_RETURN_OK;
}

wolfsentry_errcode_t wolfsentry_action_list_dispatch(
    struct wolfsentry_context *wolfsentry,
    void *caller_arg,
    struct wolfsentry_action_list *action_list,
    struct wolfsentry_event *trigger_event,
    struct wolfsentry_route_table *route_table,
    struct wolfsentry_route *route,
    wolfsentry_action_res_t *action_results)
{
    wolfsentry_errcode_t ret;
    struct wolfsentry_action_list_ent *i;

    if (*action_results & WOLFSENTRY_ACTION_RES_STOP)
        WOLFSENTRY_ERROR_RETURN(ALREADY_STOPPED);

    for (i = (struct wolfsentry_action_list_ent *)action_list->header.head;
         i;
         i = (struct wolfsentry_action_list_ent *)i->header.next) {
        if (! (route->flags & WOLFSENTRY_ROUTE_FLAG_DONT_COUNT_HITS))
            WOLFSENTRY_ATOMIC_INCREMENT(i->action->header.hitcount, 1);
        if ((ret = i->action->handler(wolfsentry, i->action->handler_arg, caller_arg, trigger_event, route_table, route, action_results)) < 0)
            return ret;
        if (WOLFSENTRY_CHECK_BITS(*action_results, WOLFSENTRY_ACTION_RES_STOP))
            WOLFSENTRY_RETURN_OK;
    }
    WOLFSENTRY_RETURN_OK;
}