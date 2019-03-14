/*
 * Copyright 2018-present MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"){}
 *
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

#include "kms_request_opt_private.h"

#include <stdlib.h>

kms_request_opt_t *
kms_request_opt_new (void)
{
   return calloc (1, sizeof (kms_request_opt_t));
}

void
kms_request_opt_destroy (kms_request_opt_t *request)
{
   free (request);
}

void
kms_request_opt_set_connection_close (kms_request_opt_t *opt,
                                      bool connection_close)
{
   opt->connection_close = connection_close;
}
