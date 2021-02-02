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

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_kv.h>

#include "auditd_config.h"
#include "auditd_multiline.h"

static int auditd_mult_append(struct flb_parser *parser,
                            struct flb_auditd_config *ctx)
{
    struct flb_auditd_mult *mp;

    mp = flb_malloc(sizeof(struct flb_auditd_mult));
    if (!mp) {
        flb_errno();
        return -1;
    }

    mp->parser = parser;
    mk_list_add(&mp->_head, &ctx->mult_parsers);

    return 0;
}

int flb_auditd_mult_create(struct flb_auditd_config *ctx,
                         struct flb_input_instance *i_ins,
                         struct flb_config *config)
{
    int ret;
    const char *tmp;
    struct mk_list *head;
    struct flb_parser *parser;
    struct flb_kv *kv;

    tmp = flb_input_get_property("multiline_flush", i_ins);
    if (!tmp) {
        ctx->multiline_flush = FLB_AUDITD_MULT_FLUSH;
    }
    else {
        ctx->multiline_flush = atoi(tmp);
        if (ctx->multiline_flush <= 0) {
            ctx->multiline_flush = 1;
        }
    }

    /* Create firstline parser */
    const char *str_regex = "^(?<log>type=\\S+\\smsg=audit\\(\\d+.\\d+:\\d+\\):.*)";
    parser = flb_parser_create("auditd_log_parser_head_f3c55c1c-e917-446b-8b47-42327b647c69", "regex", str_regex,
                               NULL, NULL, NULL, MK_FALSE, MK_TRUE, NULL, 0,
                               NULL, config);

    if (!parser) {
        flb_error("[in_auditd] multiline: invalid parser '%s'", str_regex);
        return -1;
    }

    ctx->mult_parser_firstline = parser;
    mk_list_init(&ctx->mult_parsers);

    /* Read all multiline rules 
    mk_list_foreach(head, &i_ins->properties) {
        kv = mk_list_entry(head, struct flb_kv, _head);
        if (strcasecmp("parser_firstline", kv->key) == 0) {
            continue;
        }

        if (strncasecmp("parser_", kv->key, 7) == 0) {
            parser = flb_parser_get(kv->val, config);
            if (!parser) {
                flb_error("[in_auditd] multiline: invalid parser '%s'", kv->val);
                return -1;
            }

            ret = auditd_mult_append(parser, ctx);
            if (ret == -1) {
                return -1;
            }
        }
    } */

    return 0;
}

int flb_auditd_mult_destroy(struct flb_auditd_config *ctx)
{
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_auditd_mult *mp;

    if (ctx->multiline == FLB_FALSE) {
        return 0;
    }

    mk_list_foreach_safe(head, tmp, &ctx->mult_parsers) {
        mp = mk_list_entry(head, struct flb_auditd_mult, _head);
        mk_list_del(&mp->_head);
        flb_free(mp);
    }

    return 0;
}

/*
 * Pack a line that did not matched a firstline and is not part of a multiline
 * message.
 */
static int pack_line(char *data, size_t data_size, struct flb_auditd_file *file,
                     struct flb_auditd_config *ctx)
{
    msgpack_sbuffer mp_sbuf;
    msgpack_packer mp_pck;
    struct flb_time out_time;

    msgpack_sbuffer_init(&mp_sbuf);
    msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);
    flb_time_get(&out_time);

    flb_auditd_file_pack_line(&mp_sbuf, &mp_pck, &out_time, data, data_size, file);
    flb_input_chunk_append_raw(ctx->i_ins,
                               file->tag_buf,
                               file->tag_len,
                               mp_sbuf.data,
                               mp_sbuf.size);
    msgpack_sbuffer_destroy(&mp_sbuf);

    return 0;
}

/* Process the result of a firstline match */
int flb_auditd_mult_process_first(time_t now,
                                char *buf, size_t size,
                                struct flb_time *out_time,
                                struct flb_auditd_file *file,
                                struct flb_auditd_config *ctx)
{
    int ret;
    size_t off;
    msgpack_object map;
    msgpack_unpacked result;

    /* Remark as first multiline message */
    file->mult_firstline = FLB_TRUE;

    /* Validate obtained time, if not set, set the current time */
    if (flb_time_to_double(out_time) == 0) {
        flb_time_get(out_time);
    }

    /* Should we skip this multiline record ? */
    if (ctx->ignore_older > 0) {
        if ((now - ctx->ignore_older) > out_time->tm.tv_sec) {
            flb_free(buf);
            file->mult_skipping = FLB_TRUE;
            file->mult_firstline = FLB_TRUE;

            /* we expect more data to skip */
            return FLB_AUDITD_MULT_MORE;
        }
    }

    /* Re-initiate buffers */
    msgpack_sbuffer_init(&file->mult_sbuf);
    msgpack_packer_init(&file->mult_pck, &file->mult_sbuf, msgpack_sbuffer_write);

    /*
     * flb_parser_do() always return a msgpack buffer, so we tweak our
     * local msgpack reference to avoid an extra allocation. The only
     * concern is that we don't know what's the real size of the memory
     * allocated, so we assume it's just 'out_size'.
     */
    file->mult_flush_timeout = now + (ctx->multiline_flush - 1);
    file->mult_sbuf.data = buf;
    file->mult_sbuf.size = size;
    file->mult_sbuf.alloc = size;

    /* Set multiline status */
    file->mult_firstline = FLB_TRUE;
    file->mult_skipping = FLB_FALSE;
    flb_time_copy(&file->mult_time, out_time);

    off = 0;
    msgpack_unpacked_init(&result);
    ret = msgpack_unpack_next(&result, buf, size, &off);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        msgpack_sbuffer_destroy(&file->mult_sbuf);
        msgpack_unpacked_destroy(&result);
        return FLB_AUDITD_MULT_NA;
    }

    map = result.data;
    file->mult_keys = map.via.map.size;
    msgpack_unpacked_destroy(&result);

    /* We expect more data */
    return FLB_AUDITD_MULT_MORE;
}

/* Append a raw log entry to the last structured field in the mult buffer */
static inline void flb_auditd_mult_append_raw(char *buf, int size,
                                            struct flb_auditd_file *file,
                                            struct flb_auditd_config *config)
{
    /* Append the raw string */
    msgpack_pack_str(&file->mult_pck, size);
    msgpack_pack_str_body(&file->mult_pck, buf, size);
}

int flb_auditd_mult_process_content(time_t now,
                                  char *buf, int len,
                                  char *currentAuditId, char *newAuditId,
                                  struct flb_auditd_file *file,
                                  struct flb_auditd_config *ctx)
{
    struct flb_time out_time = {0};

//    flb_debug("[in_auditd] flb_auditd_mult_process_content started");

    /* Always check if this line is the beginning of a new multiline message */
    if (currentAuditId[0] == '\0' ||
        strcmp(currentAuditId, newAuditId) != 0)
    {
        flb_debug("[in_auditd] FIRST line");

        msgpack_sbuffer mp_sbuf;
        msgpack_packer mp_pck;

        /* If a previous multiline context already exists, flush first */
        if (file->mult_firstline == FLB_TRUE && file->mult_skipping == FLB_FALSE) {
            msgpack_sbuffer_init(&mp_sbuf);
            msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

            flb_auditd_mult_flush(&mp_sbuf, &mp_pck, file, ctx);
            flb_input_chunk_append_raw(ctx->i_ins,
                                    file->tag_buf,
                                    file->tag_len,
                                    mp_sbuf.data,
                                    mp_sbuf.size);
            msgpack_sbuffer_destroy(&mp_sbuf);
        }

        if (newAuditId[0] != '\0')
        {
            void *out_buf;
            size_t out_size = 0;
            
            flb_parser_do(ctx->mult_parser_firstline,
                                buf, len,
                                &out_buf, &out_size, &out_time);
            flb_auditd_mult_process_first(now, out_buf, out_size, &out_time,
                                            file, ctx);

            if (strcmp(currentAuditId, newAuditId) != 0)
            {
                strcpy(currentAuditId, newAuditId);
                flb_debug("[in_auditd] currentAuditId is changed to %s", currentAuditId);
            }
        }
        else
        {
            pack_line(buf, len, file, ctx);
            currentAuditId[0] = '\0';
            flb_debug("[in_auditd] currentAuditId is changed to NULL.");
        }
    }
    else
    {
        flb_debug("[in_auditd] NOT FIRST line");

        if (file->mult_firstline == FLB_TRUE) {
            flb_auditd_mult_append_raw(buf, len, file, ctx);
        }
        else {
            pack_line(buf, len, file, ctx);
        }
    }

//    flb_debug("[in_auditd] flb_auditd_mult_process_content ended");
    return FLB_AUDITD_MULT_MORE;
}

/* Flush any multiline context data into outgoing buffers */
int flb_auditd_mult_flush(msgpack_sbuffer *mp_sbuf, msgpack_packer *mp_pck,
                        struct flb_auditd_file *file, struct flb_auditd_config *ctx)
{
    int i;
    int map_size;
    size_t total;
    size_t off = 0;
    size_t next_off = 0;
    size_t bytes;
    void *data;
    msgpack_unpacked result;
    msgpack_unpacked cont;
    msgpack_object root;
    msgpack_object next;
    msgpack_object k;
    msgpack_object v;

    /* nothing to flush */
    if (file->mult_firstline == FLB_FALSE) {
        return -1;
    }

    if (file->mult_keys == 0) {
        return -1;
    }

    /* Compose the new record with the multiline content */
    msgpack_pack_array(mp_pck, 2);
    flb_time_append_to_msgpack(&file->mult_time, mp_pck, 0);

    /* New Map size */
    map_size = file->mult_keys;
    if (file->config->path_key != NULL) {
        map_size++;
    }
    msgpack_pack_map(mp_pck, map_size);

    /* Append Path_Key ? */
    if (file->config->path_key != NULL) {
        msgpack_pack_str(mp_pck, file->config->path_key_len);
        msgpack_pack_str_body(mp_pck, file->config->path_key,
                              file->config->path_key_len);
        msgpack_pack_str(mp_pck, file->name_len);
        msgpack_pack_str_body(mp_pck, file->name, file->name_len);
    }

    data = file->mult_sbuf.data;
    bytes = file->mult_sbuf.size;

    msgpack_unpacked_init(&result);
    msgpack_unpacked_init(&cont);

    while (msgpack_unpack_next(&result, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS) {
        root = result.data;
        if (root.type != MSGPACK_OBJECT_MAP) {
            continue;
        }

        total = 0;
        next_off = off;

        for (i = 0; i < root.via.map.size; i++) {
            k = root.via.map.ptr[i].key;
            v = root.via.map.ptr[i].val;

            /* Always check if the 'next' entry is a continuation */
            total = 0;
            if (i + 1 == root.via.map.size) {
                while (msgpack_unpack_next(&cont, data, bytes, &next_off) ==
                       MSGPACK_UNPACK_SUCCESS) {

                    next = cont.data;
                    if (next.type != MSGPACK_OBJECT_STR) {
                        break;
                    }

                    /* Sum total bytes to append */
                    total += next.via.str.size + 1;
                }
            }

            msgpack_pack_object(mp_pck, k);
            if (total > 0 && v.type == MSGPACK_OBJECT_STR) {
                msgpack_pack_str(mp_pck, v.via.str.size + total);
                msgpack_pack_str_body(mp_pck, v.via.str.ptr, v.via.str.size);
            }
            else {
                msgpack_pack_object(mp_pck, v);
            }

            if (total > 0) {
                /*
                 * We have extra lines that needs to be merged inside this
                 * value field.
                 */
                next_off = off;
                while (msgpack_unpack_next(&cont, data, bytes, &next_off) ==
                       MSGPACK_UNPACK_SUCCESS) {

                    next = cont.data;
                    if (next.type != MSGPACK_OBJECT_STR) {
                        break;
                    }

                    msgpack_pack_str_body(mp_pck, "\n", 1);
                    msgpack_pack_str_body(mp_pck, next.via.str.ptr,
                                          next.via.str.size);
                }
            }
        }
    }

    msgpack_unpacked_destroy(&result);
    msgpack_unpacked_destroy(&cont);

    /* Reset status */
    file->mult_firstline = FLB_FALSE;
    file->mult_skipping = FLB_FALSE;
    file->mult_keys = 0;
    file->mult_flush_timeout = 0;
    msgpack_sbuffer_destroy(&file->mult_sbuf);
    flb_time_zero(&file->mult_time);

    return 0;
}

int flb_auditd_mult_pending_flush(struct flb_input_instance *i_ins,
                                struct flb_config *config, void *context)
{
    time_t now;
    msgpack_sbuffer mp_sbuf;
    msgpack_packer mp_pck;
    struct mk_list *head;
    struct flb_auditd_file *file;
    struct flb_auditd_config *ctx = context;

    now = time(NULL);

    /* Iterate promoted event files with pending bytes */
    mk_list_foreach(head, &ctx->files_event) {
        file = mk_list_entry(head, struct flb_auditd_file, _head);

        if (file->mult_flush_timeout > now) {
            continue;
        }

        if (file->mult_firstline == FLB_FALSE) {
            if (file->mult_sbuf.data == NULL || file->mult_sbuf.size <= 0) {
                continue;
            }
        }

        msgpack_sbuffer_init(&mp_sbuf);
        msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

        flb_auditd_mult_flush(&mp_sbuf, &mp_pck, file, ctx);

        flb_input_chunk_append_raw(i_ins,
                                   file->tag_buf,
                                   file->tag_len,
                                   mp_sbuf.data,
                                   mp_sbuf.size);
        msgpack_sbuffer_destroy(&mp_sbuf);
    }

    return 0;
}