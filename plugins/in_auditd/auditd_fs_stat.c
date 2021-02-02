/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019      The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#define _DEFAULT_SOURCE

#include <fluent-bit/flb_compat.h>
#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_input.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "auditd_file.h"
#include "auditd_db.h"
#include "auditd_config.h"
#include "auditd_signal.h"

struct fs_stat {
    /* last time check */
    time_t checked;

    /* previous status */
    struct stat st;
};

static int auditd_fs_event(struct flb_input_instance *i_ins,
                         struct flb_config *config, void *in_context)
{
    int ret;
    struct mk_list *head;
    struct mk_list *tmp;
    struct flb_auditd_config *ctx = in_context;
    struct flb_auditd_file *file = NULL;
    struct fs_stat *fst;
    struct stat st;
    time_t t;

    t = time(NULL);

    /* Lookup watched file */
    mk_list_foreach_safe(head, tmp, &ctx->files_event) {
        file = mk_list_entry(head, struct flb_auditd_file, _head);
        fst = file->fs_backend;

        /* Check current status of the file */
        ret = fstat(file->fd, &st);
        if (ret == -1) {
            flb_errno();
            continue;
        }

        /* Check if the file was modified */
        if ((fst->st.st_mtime != st.st_mtime) ||
            (fst->st.st_size != st.st_size)) {
            /* Update stat info and trigger the notification */
            memcpy(&fst->st, &st, sizeof(struct stat));
            fst->checked = t;
            in_auditd_collect_event(file, config);
        }
    }

    return 0;
}

static int auditd_fs_check(struct flb_input_instance *i_ins,
                         struct flb_config *config, void *in_context)
{
    int ret;
    off_t offset;
    char *name;
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_auditd_config *ctx = in_context;
    struct flb_auditd_file *file = NULL;
    struct fs_stat *fst;
    struct stat st;

    /* Lookup watched file */
    mk_list_foreach_safe(head, tmp, &ctx->files_event) {
        file = mk_list_entry(head, struct flb_auditd_file, _head);
        fst = file->fs_backend;

        ret = fstat(file->fd, &st);
        if (ret == -1) {
            flb_debug("[in_auditd] error stat(2) %s, removing", file->name);
            flb_auditd_file_remove(file);
            continue;
        }

        /* Check if the file have been deleted */
        if (st.st_nlink == 0) {
            flb_debug("[in_auditd] deleted %s", file->name);
            flb_auditd_file_remove(file);
            continue;
        }

        /* Discover the current file name for the open file descriptor */
        name = flb_auditd_file_name(file);
        if (!name) {
            flb_debug("[in_auditd] could not resolve %s, removing", file->name);
            flb_auditd_file_remove(file);
            continue;
        }

        /*
         * Check if file still exists. This method requires explicity that the
         * user is using an absolute path, otherwise we will be rotating the
         * wrong file.
         */
        if (flb_auditd_file_name_cmp(name, file) != 0) {
            flb_auditd_file_rotated(file);
        }
        flb_free(name);

        /* Check if the file was truncated */
        if (file->offset > st.st_size) {
            offset = lseek(file->fd, 0, SEEK_SET);
            if (offset == -1) {
                flb_errno();
                return -1;
            }

            flb_debug("[in_auditd] truncated %s", file->name);
            file->offset = offset;
            file->buf_len = 0;
            memcpy(&fst->st, &st, sizeof(struct stat));

            /* Update offset in database file */
            if (ctx->db) {
                flb_auditd_db_file_offset(file, ctx);
            }
        }

        if (file->offset < st.st_size) {
            file->pending_bytes = (st.st_size - file->offset);
            auditd_signal_pending(ctx);
        }
        else {
            file->pending_bytes = 0;
        }
    }

    return 0;
}

/* File System events based on stat(2) */
int flb_auditd_fs_init(struct flb_input_instance *in,
                     struct flb_auditd_config *ctx, struct flb_config *config)
{
    int ret;

    /* Set a manual timer to collect events every 0.250 seconds */
    ret = flb_input_set_collector_time(in, auditd_fs_event,
                                       0, 250000000, config);
    if (ret < 0) {
        return -1;
    }
    ctx->coll_fd_fs1 = ret;

    /* Set a manual timer to check deleted/rotated files every 2.5 seconds */
    ret = flb_input_set_collector_time(in, auditd_fs_check,
                                       2, 500000000, config);
    if (ret < 0) {
        return -1;
    }
    ctx->coll_fd_fs2 = ret;

    return 0;
}

void flb_auditd_fs_pause(struct flb_auditd_config *ctx)
{
    flb_input_collector_pause(ctx->coll_fd_fs1, ctx->i_ins);
    flb_input_collector_pause(ctx->coll_fd_fs2, ctx->i_ins);
}

void flb_auditd_fs_resume(struct flb_auditd_config *ctx)
{
    flb_input_collector_resume(ctx->coll_fd_fs1, ctx->i_ins);
    flb_input_collector_resume(ctx->coll_fd_fs2, ctx->i_ins);
}

int flb_auditd_fs_add(struct flb_auditd_file *file)
{
    int ret;
    struct fs_stat *fst;

    fst = flb_malloc(sizeof(struct fs_stat));
    if (!fst) {
        flb_errno();
        return -1;
    }

    fst->checked = time(NULL);
    ret = stat(file->name, &fst->st);
    if (ret == -1) {
        flb_errno();
        flb_free(fst);
        return -1;
    }
    file->fs_backend = fst;

    return 0;
}

int flb_auditd_fs_remove(struct flb_auditd_file *file)
{
    if (file->auditd_mode == FLB_AUDITD_EVENT) {
        flb_free(file->fs_backend);
    }
    return 0;
}

int flb_auditd_fs_exit(struct flb_auditd_config *ctx)
{
    (void) ctx;
    return 0;
}