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

#define IV_LEN 16
#define KEY_LEN 32

typedef struct {
    // WT_ENCRYPTOR must be the first field
    WT_ENCRYPTOR encryptor;
    WT_EXTENSION_API *wt_api;
    const EVP_CIPHER *cipher;
    EVP_CIPHER_CTX *ctx;
    unsigned char key[KEY_LEN];
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

static int handleErrors() {
    //TODO: use wt_api->err_printf
    int ret = 0;
    int e;
    while ((e = ERR_get_error())) {
        printf("libcrypto error: %d\n", e);
        ret = e;
    }
    return ret;
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

static int dump_config_arg(PERCONA_ENCRYPTOR *encryptor, WT_SESSION *session, WT_CONFIG_ARG *config) {
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

static int parse_customization_config(PERCONA_ENCRYPTOR *encryptor, WT_SESSION *session, WT_CONFIG_ARG *config) {
    WT_EXTENSION_API *wt_api = encryptor->wt_api;
    WT_CONFIG_PARSER *parser = NULL;
    int ret = wt_api->config_parser_open_arg(wt_api, session, config, &parser);
    if (ret != 0)
        return ret;
    WT_CONFIG_ITEM k, v;
    while (parser->next(parser, &k, &v) == 0) {
        if (!strncmp("keyid", k.str, (int)k.len)) {
            if (0 != get_key_by_id(v.str, v.len, encryptor->key)) {
                ret = report_error(encryptor, session, EINVAL, "cannot get key by keyid");
                break;
            }
        }
    }
    parser->close(parser);
    return ret;
}

static void store_IV(PERCONA_ENCRYPTOR *pe, uint8_t *dst) {
    uint8_t buf[IV_LEN];
    store_pseudo_bytes(buf, IV_LEN);
    //TODO: encrypt pseudo bytes same way as PS does
    memcpy(dst, buf, IV_LEN);
}

static int percona_encrypt(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len,
    size_t *result_lenp)
{
    printf("entering encrypt %lu %lu\n", src_len, dst_len);
    int ret = 0;
    int encrypted_len = 0;
    PERCONA_ENCRYPTOR *pe = (PERCONA_ENCRYPTOR*)encryptor;
    if (dst_len < IV_LEN + src_len + EVP_CIPHER_block_size(pe->cipher))
        return (report_error(pe, session,
                ENOMEM, "encrypt buffer not big enough"));

    *result_lenp = 0;
    EVP_CIPHER_CTX_init(pe->ctx);

    store_IV(pe, dst);
    *result_lenp += IV_LEN;

    if(1 != EVP_EncryptInit_ex(pe->ctx, pe->cipher, NULL, pe->key, dst))
        goto err;

    if(1 != EVP_EncryptUpdate(pe->ctx, dst + *result_lenp, &encrypted_len, src, src_len))
        goto err;
    *result_lenp += encrypted_len;

    if(1 != EVP_EncryptFinal_ex(pe->ctx, dst + *result_lenp, &encrypted_len))
        goto err;
    *result_lenp += encrypted_len;

    EVP_CIPHER_CTX_cleanup(pe->ctx);
    printf("exiting encrypt %lu\n", *result_lenp);
    return 0;

err:
    ret = handleErrors();
    EVP_CIPHER_CTX_cleanup(pe->ctx);
    return ret;
}

static int percona_decrypt(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len,
    size_t *result_lenp)
{
    printf("entering decrypt %lu %lu\n", src_len, dst_len);
    int ret = 0;
    int decrypted_len = 0;
    PERCONA_ENCRYPTOR *pe = (PERCONA_ENCRYPTOR*)encryptor;

    *result_lenp = 0;
    EVP_CIPHER_CTX_init(pe->ctx);

    if(1 != EVP_DecryptInit_ex(pe->ctx, pe->cipher, NULL, pe->key, src))
        goto err;
    src += IV_LEN;
    src_len -= IV_LEN;

    if(1 != EVP_DecryptUpdate(pe->ctx, dst, &decrypted_len, src, src_len))
        goto err;
    *result_lenp += decrypted_len;
    dst += decrypted_len;

    if(1 != EVP_DecryptFinal_ex(pe->ctx, dst, &decrypted_len))
        goto err;
    *result_lenp += decrypted_len;

    EVP_CIPHER_CTX_cleanup(pe->ctx);
    printf("exiting decrypt %lu\n", *result_lenp);
    return 0;

err:
    ret = handleErrors();
    EVP_CIPHER_CTX_cleanup(pe->ctx);
    return ret;
}

static int percona_sizing(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    size_t *expansion_constantp)
{
    printf("entering sizing\n");
    (void)session;              /* Unused parameters */

    //*expansion_constantp = CHKSUM_LEN + IV_LEN;
    *expansion_constantp = IV_LEN + EVP_CIPHER_block_size(((PERCONA_ENCRYPTOR*)encryptor)->cipher);
    return 0;
}

static int percona_customize(WT_ENCRYPTOR *encryptor, WT_SESSION *session,
    WT_CONFIG_ARG *encrypt_config, WT_ENCRYPTOR **customp)
{
    printf("entering customize\n");
    PERCONA_ENCRYPTOR *pe = (PERCONA_ENCRYPTOR*)encryptor;
    dump_config_arg(pe, session, encrypt_config);
    PERCONA_ENCRYPTOR *cpe;
    if ((cpe = calloc(1, sizeof(PERCONA_ENCRYPTOR))) == NULL)
            return errno;
    *cpe = *pe;
    cpe->ctx = EVP_CIPHER_CTX_new();
    if (!cpe->ctx) {
        int ret = report_error(cpe, session, EINVAL, "cannot create cipher context");
        free(cpe);
        return ret;
    }
    // new instance passed to parse_customization_config because it should fill encryption key field
    int ret = parse_customization_config(cpe, session, encrypt_config);
    if (ret != 0) {
        free(cpe);
        return ret;
    }
    *customp = (WT_ENCRYPTOR*)cpe;
    return 0;
}

static int percona_terminate(WT_ENCRYPTOR *encryptor, WT_SESSION *session)
{
    printf("entering terminate\n");
    PERCONA_ENCRYPTOR *pe = (PERCONA_ENCRYPTOR*)encryptor;
    EVP_CIPHER_CTX_free(pe->ctx);
    free(encryptor);
    return 0;
}

static int init_from_config(PERCONA_ENCRYPTOR *pe, WT_CONFIG_ARG *config)
{
    WT_EXTENSION_API *wt_api = pe->wt_api;
    bool cipherMode = false;
    WT_CONFIG_PARSER *parser = NULL;
    int ret = wt_api->config_parser_open_arg(wt_api, NULL, config, &parser);
    if (ret != 0)
        return ret;

    WT_CONFIG_ITEM k, v;
    while ((ret = parser->next(parser, &k, &v)) == 0) {
        if (!strncmp("cipher", k.str, (int)k.len)) {
            if (!strncmp("AES256-CBC", v.str, (int)v.len)) {
                cipherMode = true;
                pe->cipher = EVP_aes_256_cbc();
            }
            else if (!strncmp("AES256-GCM", v.str, (int)v.len)) {
                cipherMode = true;
                pe->cipher = EVP_aes_256_gcm();
            }
            else
                return (report_error(pe, NULL, EINVAL, "specified cipher mode is not supported"));
        }
    }
    parser->close(parser);

    if (!cipherMode)
        return (report_error(pe, NULL, EINVAL, "cipher mode is not specified"));
    return 0;
}

int percona_encryption_extension_init(WT_CONNECTION *connection, WT_CONFIG_ARG *config) {
    printf("hello from the percona_encryption_extension_init\n");
    int ret = 0;
    PERCONA_ENCRYPTOR *pe;

    if ((pe = calloc(1, sizeof(PERCONA_ENCRYPTOR))) == NULL)
            return errno;

    pe->wt_api = connection->get_extension_api(connection);

    dump_config_arg(pe, NULL, config);

    ret = init_from_config(pe, config);
    if (ret != 0)
        goto failure;

    pe->ctx = EVP_CIPHER_CTX_new();
    if (!pe->ctx) {
        ret = report_error(pe, NULL, EINVAL, "cannot create cipher context");
        goto failure;
    }

#if 0
    // get default key (masterkey)
    ret = get_key_by_id(NULL, 0, pe->key);
    if (ret != 0) {
        ret = report_error(pe, NULL, EINVAL, "cannot get default key");
        goto failure;
    }
#else
    // calloc initializes all allocated memory to zero
    // thus pe->key is filled with zeros
    // actual encryption keys are loaded by 'customize' callback
#endif

    pe->encryptor.encrypt = percona_encrypt;
    pe->encryptor.decrypt = percona_decrypt;
    pe->encryptor.sizing = percona_sizing;
    pe->encryptor.customize = percona_customize;
    pe->encryptor.terminate = percona_terminate;

    return connection->add_encryptor(connection, "percona", (WT_ENCRYPTOR*)pe, NULL);

failure:
    free(pe);
    return ret;
}

