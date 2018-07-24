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

#include <string>
#include <wiredtiger.h>

#include <mongo/platform/random.h>

namespace mongo {

class EncryptionKeyDB
{
public:
    EncryptionKeyDB(const std::string& path);
    ~EncryptionKeyDB();

    // tries to read master key from specified file
    // then opens WT connection
    // throws exceptions if something goes wrong
    void init();

    // returns encryption key from keys DB
    // create key if it does not exists
    // return key from keyfile if len == 0
    int get_key_by_id(const char *keyid, size_t len, unsigned char *key);

    // len should be multiple of 4
    void store_pseudo_bytes(uint8_t *buf, int len);

private:
    static constexpr int _key_len = 32;
    const std::string _path;
    unsigned char _masterkey[_key_len];
    WT_CONNECTION *_conn = nullptr;
    WT_SESSION *_sess = nullptr;
    std::unique_ptr<SecureRandom> _srng;
    std::unique_ptr<PseudoRandom> _prng;
};

}  // namespace mongo
