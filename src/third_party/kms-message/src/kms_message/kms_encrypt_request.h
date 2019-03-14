/*
 * Copyright 2018-present MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef KMS_ENCRYPT_REQUEST_H
#define KMS_ENCRYPT_REQUEST_H

#include "kms_message.h"

#ifdef __cplusplus
extern "C" {
#endif

KMS_MSG_EXPORT (kms_request_t *)
kms_encrypt_request_new (const uint8_t *plaintext,
                         size_t plaintext_length,
                         const char *key_id,
                         const kms_request_opt_t *opt);

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* KMS_ENCRYPT_REQUEST_H */
