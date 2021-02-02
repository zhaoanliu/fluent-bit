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

/*
 * This file implements glob-like patch matching feature for Windows
 * based on Win32 API.
 */

#include <fluent-bit/flb_compat.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_utils.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <shlwapi.h>

#include "auditd.h"
#include "auditd_file.h"
#include "auditd_signal.h"
#include "auditd_config.h"

static int auditd_is_excluded(char *path, struct flb_auditd_config *ctx)
{
    struct mk_list *head;
    struct flb_split_entry *pattern;

    if (!ctx->exclude_list) {
        return FLB_FALSE;
    }

    mk_list_foreach(head, ctx->exclude_list) {
        pattern = mk_list_entry(head, struct flb_split_entry, _head);
        if (PathMatchSpecA(path, pattern->value)) {
            return FLB_TRUE;
        }
    }

    return FLB_FALSE;
}

static int auditd_exclude_generate(struct flb_auditd_config *ctx)
{
    struct mk_list *list;

    /*
     * The exclusion path might content multiple exclusion patterns, first
     * let's split the content into a list.
     */
    list = flb_utils_split(ctx->exclude_path, ',', -1);
    if (!list) {
        return -1;
    }

    if (mk_list_is_empty(list) == 0) {
        return 0;
    }

    /* We use the same list head returned by flb_utils_split() */
    ctx->exclude_list = list;
    return 0;
}

static int auditd_filepath(char *buf, int len, const char *basedir, const char *filename)
{
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    char tmp[MAX_PATH];
    int ret;

    ret = _splitpath_s(basedir, drive, _MAX_DRIVE, dir, _MAX_DIR, NULL, 0, NULL, 0);
    if (ret) {
        return -1;
    }

    ret = _splitpath_s(filename, NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (ret) {
        return -1;
    }

    ret = _makepath_s(tmp, MAX_PATH, drive, dir, fname, ext);
    if (ret) {
        return -1;
    }

    if (_fullpath(buf, tmp, len) == NULL) {
        return -1;
    }

    return 0;
}

int flb_auditd_scan(const char *pattern, struct flb_auditd_config *ctx)
{
    HANDLE h;
    WIN32_FIND_DATA found;
    struct stat st;
    char path[MAX_PATH];
    int ret;

    flb_debug("[in_auditd] scanning path %s", pattern);

    if (ctx->exclude_path) {
        auditd_exclude_generate(ctx);
    }

    h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE) {
        switch (GetLastError()) {
            case ERROR_FILE_NOT_FOUND:
                flb_debug("[in_auditd] NO matches for path: %s", pattern);
                return 0;
            default:
                flb_error("[in_auditd] Cannot read info from: %s", pattern);
                return -1;
        }
    }

    do {
        /* WIN32_FIND_DATA.cFileName is just a file name, we need to
         * construct a proper path by combining the original pattern.
         */
        ret = auditd_filepath(path, MAX_PATH, pattern, found.cFileName);
        if (ret) {
            flb_error("[in_auditd] fail to get a path for %s", found.cFileName);
            continue;
        }

        ret = stat(path, &st);
        if (ret == 0 && S_ISREG(st.st_mode)) {
            if (auditd_is_excluded(path, ctx) == FLB_TRUE) {
                flb_debug("[in_auditd] excluded=%s", path);
                continue;
            }

            flb_auditd_file_append(path, &st, FLB_AUDITD_STATIC, ctx);
        }
    } while(FindNextFileA(h, &found));

    FindClose(h);
    return 0;
}

int flb_auditd_scan_callback(struct flb_input_instance *i_ins,
                           struct flb_config *config, void *context)
{
    HANDLE h;
    WIN32_FIND_DATA found;
    struct stat st;
    struct flb_auditd_config *ctx;
    char path[MAX_PATH];
    char *pattern;
    int ret;
    int count = 0;

    ctx = (struct flb_auditd_config *) context;
    pattern = ctx->path;

    h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE) {
        switch (GetLastError()) {
            case ERROR_FILE_NOT_FOUND:
                return 0;
            default:
                flb_error("[in_auditd] Cannot read info from: %s", pattern);
                return -1;
        }
    }

    do {
        ret = auditd_filepath(path, MAX_PATH, pattern, found.cFileName);
        if (ret) {
            flb_error("[in_auditd] fail to get a path for %s", found.cFileName);
            continue;
        }

        ret = stat(path, &st);
        if (ret == 0 && S_ISREG(st.st_mode)) {
            if (auditd_is_excluded(path, ctx) == FLB_TRUE) {
                continue;
            }
            ret = flb_auditd_file_exists(path, ctx);
            if (ret == FLB_TRUE) {
                continue;
            }

            flb_debug("[in_auditd] append new file: %s", path);

            flb_auditd_file_append(path, &st, FLB_AUDITD_STATIC, ctx);

            count++;
        } else {
            flb_debug("[in_auditd] skip (invalid) entry=%s", path);
        }
    } while(FindNextFileA(h, &found));

    FindClose(h);

    if (count > 0) {
        auditd_signal_manager(ctx);
    }

    return 0;
}
