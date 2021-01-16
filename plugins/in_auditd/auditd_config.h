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

#ifndef FLB_AUDITD_CONFIG_H
#define FLB_AUDITD_CONFIG_H

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_parser.h>
#include <fluent-bit/flb_macros.h>
#ifdef FLB_HAVE_REGEX
#include <fluent-bit/flb_regex.h>
#endif

/* Metrics */
#ifdef FLB_HAVE_METRICS
#define FLB_AUDITD_METRIC_F_OPENED  200  /* number of opened files  */
#define FLB_AUDITD_METRIC_F_CLOSED  201  /* number of closed files  */
#define FLB_AUDITD_METRIC_F_ROTATED 202  /* number of rotated files */
#endif

struct flb_auditd_config {
    int fd_notify;             /* inotify fd               */
#ifdef _WIN32
    intptr_t ch_manager[2];    /* pipe: channel manager    */
    intptr_t ch_pending[2];    /* pipe: pending events     */
#else
    int ch_manager[2];         /* pipe: channel manager    */
    int ch_pending[2];         /* pipe: pending events     */
#endif

    /* Buffer Config */
    size_t buf_chunk_size;     /* allocation chunks        */
    size_t buf_max_size;       /* max size of a buffer     */

    /* Collectors */
    int coll_fd_static;
    int coll_fd_scan;
    int coll_fd_rotated;
    int coll_fd_pending;
    int coll_fd_dmode_flush;
    int coll_fd_mult_flush;

    /* Backend collectors */
    int coll_fd_fs1;           /* used by fs_inotify & fs_stat */
    int coll_fd_fs2;           /* only used by fs_stat         */

    /* Configuration */
    int dynamic_tag;           /* dynamic tag ? e.g: abc.*     */
#ifdef FLB_HAVE_REGEX
    struct flb_regex *tag_regex;/* path to tag regex           */
    struct flb_regex *auditid_regex;/* regex to parse auditid from audit logs           */
#endif
    int refresh_interval_sec;  /* seconds to re-scan           */
    long refresh_interval_nsec;/* nanoseconds to re-scan       */
    int rotate_wait;           /* sec to wait on rotated files */
    int ignore_older;          /* ignore fields older than X seconds        */
    time_t last_pending;       /* last time a 'pending signal' was emitted' */
    const char *path;          /* lookup path (glob)           */
    const char *exclude_path;  /* exclude path                 */
    const char *path_key;      /* key name of file path        */
    int   path_key_len;        /* length of key name           */
    char *key;                 /* key for unstructured record  */
    int   key_len;             /* length of key ^              */
    int   skip_long_lines;     /* skip long lines              */
    int   exit_on_eof;         /* exit fluent-bit on EOF, test */

    /* Database */
#ifdef FLB_HAVE_SQLDB
    struct flb_sqldb *db;
    int db_sync;
#endif

    /* Parser / Format */
    struct flb_parser *parser;

    /* Multiline */
    int multiline;             /* multiline enabled ?  */
    int multiline_flush;       /* multiline flush/wait */
    struct flb_parser *mult_parser_firstline;
    struct mk_list mult_parsers;

    /* Docker mode */
    int docker_mode;           /* Docker mode enabled ?  */
    int docker_mode_flush;     /* Docker mode flush/wait */

    /* Lists head for files consumed statically (read) and by events (inotify) */
    struct mk_list files_static;
    struct mk_list files_event;

    /* List of rotated files that needs to be removed after 'rotate_wait' */
    struct mk_list files_rotated;

    /* List of shell patterns used to exclude certain file names */
    struct mk_list *exclude_list;

    /* Plugin input instance */
    struct flb_input_instance *i_ins;
};

struct flb_auditd_config *flb_auditd_config_create(struct flb_input_instance *i_ins,
                                               struct flb_config *config);
int flb_auditd_config_destroy(struct flb_auditd_config *config);

#endif
