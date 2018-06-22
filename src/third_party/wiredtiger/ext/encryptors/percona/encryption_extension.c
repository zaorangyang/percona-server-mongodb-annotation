/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2018, Percona and/or its affiliates. All rights reserved.

    Percona Server for MongoDB is free software: you can redistribute
    it and/or modify it under the terms of the GNU Affero General
    Public License, version 3, as published by the Free Software
    Foundation.

    Percona Server for MongoDB is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public
    License along with Percona Server for MongoDB.  If not, see
    <http://www.gnu.org/licenses/>.
======= */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
//#include <assert>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include <wiredtiger.h>
#include <wiredtiger_ext.h>

#include "encryption_keydb_c_api.h"

#define IV_LEN sizeof(clock_t)

typedef struct {
    // WT_ENCRYPTOR must be the first field
    WT_ENCRYPTOR encryptor;
    WT_EXTENSION_API *wt_api;
} PERCONA_ENCRYPTOR;

static int report_error(
    PERCONA_ENCRYPTOR *encryptor, WT_SESSION *session, int err, const char *msg)
{
    WT_EXTENSION_API *wt_api;

    wt_api = encryptor->wt_api;
    (void)wt_api->err_printf(wt_api, session,
                             "encryption: %s: %s", msg, wt_api->strerror(wt_api, NULL, err));
    return err;
}

static char value_type_char(int type)
{
    switch (type) {
        case WT_CONFIG_ITEM_STRING:
            return 's';
        case WT_CONFIG_ITEM_BOOL:
            return 'b';
        case WT_CONFIG_ITEM_ID:
            return '#';
        case WT_CONFIG_ITEM_NUM:
            return 'n';
        case WT_CONFIG_ITEM_STRUCT:
            return 'z';
    }
    return 'x';
}

static int dump_config_arg(PERCONA_ENCRYPTOR *encryptor, WT_SESSION *session, WT_CONFIG_ARG *config)
{
    WT_EXTENSION_API *wt_api = encryptor->wt_api;
    WT_CONFIG_PARSER *parser = NULL;
    int ret = wt_api->config_parser_open_arg(wt_api, session, config, &parser);
    if (ret != 0)
        return ret;
    WT_CONFIG_ITEM k, v;
    while ((ret = parser->next(parser, &k, &v)) == 0) {
        printf("%c%c:%.*s:%.*s\n", value_type_char(k.type), value_type_char(v.type), (int)k.len, k.str, (int)v.len, v.str);
    }
    parser->close(parser);
    return 0;
}

static int percona_encrypt(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len,
    size_t *result_lenp)
{
    printf("entering encrypt %lu %lu\n", src_len, dst_len);
    PERCONA_ENCRYPTOR *pe = (PERCONA_ENCRYPTOR*)encryptor;
    if (dst_len < IV_LEN + src_len)
        return (report_error(pe, session,
                ENOMEM, "encrypt buffer not big enough"));
    *(clock_t*)dst = clock();
    printf("clock cnt: %lu\n", *(clock_t*)dst);
    memcpy(dst + IV_LEN, src, src_len);
    *result_lenp = IV_LEN + src_len;
    return 0;
}

static int percona_decrypt(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len,
    size_t *result_lenp)
{
    printf("entering decrypt %lu %lu\n", src_len, dst_len);
    printf("clock cnt: %lu\n", *(clock_t*)src);
    /*
     *          The destination length is the number of unencrypted bytes we're
     *          expected to return.
     *                            */
    memcpy(dst, src + IV_LEN, dst_len);
    *result_lenp = dst_len;

    return 0;
}

static int percona_sizing(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    size_t *expansion_constantp)
{
    printf("entering sizing\n");
    (void)encryptor;            /* Unused parameters */
    (void)session;              /* Unused parameters */

    //*expansion_constantp = CHKSUM_LEN + IV_LEN;
    *expansion_constantp = IV_LEN;
    return 0;
}

static int percona_customize(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    WT_CONFIG_ARG *encrypt_config, WT_ENCRYPTOR **customp)
{
    printf("entering customize\n");
    PERCONA_ENCRYPTOR *pe = (PERCONA_ENCRYPTOR*)encryptor;
    dump_config_arg(pe, session, encrypt_config);
    return 0;
}

static int percona_terminate(WT_ENCRYPTOR *encryptor, WT_SESSION *session)
{
    printf("entering terminate\n");
    free(encryptor);
    return 0;
}

int percona_encryption_extension_init(WT_CONNECTION *connection, WT_CONFIG_ARG *config) {
    printf("hello from the percona_encryption_extension_init\n");
    PERCONA_ENCRYPTOR *pe;

    if ((pe = calloc(1, sizeof(PERCONA_ENCRYPTOR))) == NULL)
            return errno;

    pe->encryptor.encrypt = percona_encrypt;
    pe->encryptor.decrypt = percona_decrypt;
    pe->encryptor.sizing = percona_sizing;
    pe->encryptor.customize = percona_customize;
    pe->encryptor.terminate = percona_terminate;
    pe->wt_api = connection->get_extension_api(connection);

    dump_config_arg(pe, NULL, config);
    if (config != NULL) {
        char **cfg = (char**)config;
        if (*cfg != NULL) {
            printf("%s\n", *cfg);
        }
    }
    return connection->add_encryptor(connection, "percona", (WT_ENCRYPTOR*)pe, NULL);
}

