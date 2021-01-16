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

#ifndef FLB_AUDITD_H
#define FLB_AUDITD_H

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_input.h>

/* Internal return values */
#define FLB_AUDITD_ERROR  -1
#define FLB_AUDITD_OK      0
#define FLB_AUDITD_WAIT    1
#define FLB_AUDITD_BUSY    2

/* Consuming mode */
#define FLB_AUDITD_STATIC  0  /* Data is being consumed through read(2) */
#define FLB_AUDITD_EVENT   1  /* Data is being consumed through inotify */

/* Config */
#define FLB_AUDITD_CHUNK        32*1024 /* buffer chunk = 32KB            */
#define FLB_AUDITD_REFRESH      60      /* refresh every 60 seconds       */
#define FLB_AUDITD_ROTATE_WAIT  5       /* time to monitor after rotation */

int in_auditd_collect_event(void *file, struct flb_config *config);

#endif
