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

#pragma once

void store_pseudo_bytes(uint8_t *buf, int len);
int get_iv_gcm(uint8_t *buf, int len);
int get_key_by_id(const char *keyid, size_t len, unsigned char *key, void *pe);

#ifdef __cplusplus
extern "C" {
#endif

int percona_encryption_extension_drop_keyid(void *vp);

#ifdef __cplusplus
}
#endif
