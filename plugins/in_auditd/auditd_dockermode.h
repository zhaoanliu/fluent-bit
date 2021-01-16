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

#ifndef FLB_AUDITD_DOCKERMODE_H
#define FLB_AUDITD_DOCKERMODE_H

#include "auditd_file.h"
#define FLB_AUDITD_DMODE_FLUSH 4

int flb_auditd_dmode_create(struct flb_auditd_config *ctx,
                          struct flb_input_instance *i_ins, struct flb_config *config);
int flb_auditd_dmode_process_content(time_t now,
                                   char* line, size_t line_len,
                                   char **repl_line, size_t *repl_line_len,
                                   struct flb_auditd_file *file,
                                   struct flb_auditd_config *ctx);
void flb_auditd_dmode_flush(msgpack_sbuffer *mp_sbuf, msgpack_packer *mp_pck,
                          struct flb_auditd_file *file, struct flb_auditd_config *ctx);
int flb_auditd_dmode_pending_flush(struct flb_input_instance *i_ins,
                                 struct flb_config *config, void *context);

#endif
