/**
 *    Copyright (C) 2014 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/crypto/sha1_block.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

// SHA-1 test vectors from http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA_All.pdf
const struct {
    const char* msg;
    SHA1Block hash;
} sha1Tests[] = {{"abc",
                  SHA1Block::HashType{0xa9,
                                      0x99,
                                      0x3e,
                                      0x36,
                                      0x47,
                                      0x06,
                                      0x81,
                                      0x6a,
                                      0xba,
                                      0x3e,
                                      0x25,
                                      0x71,
                                      0x78,
                                      0x50,
                                      0xc2,
                                      0x6c,
                                      0x9c,
                                      0xd0,
                                      0xd8,
                                      0x9d}},

                 {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                  SHA1Block::HashType{0x84,
                                      0x98,
                                      0x3E,
                                      0x44,
                                      0x1C,
                                      0x3B,
                                      0xD2,
                                      0x6E,
                                      0xBA,
                                      0xAE,
                                      0x4A,
                                      0xA1,
                                      0xF9,
                                      0x51,
                                      0x29,
                                      0xE5,
                                      0xE5,
                                      0x46,
                                      0x70,
                                      0xF1}}};

TEST(CryptoVectors, SHA1) {
    size_t numTests = sizeof(sha1Tests) / sizeof(sha1Tests[0]);
    for (size_t i = 0; i < numTests; i++) {
        SHA1Block result = SHA1Block::computeHash(
            reinterpret_cast<const unsigned char*>(sha1Tests[i].msg), strlen(sha1Tests[i].msg));
        ASSERT(sha1Tests[i].hash == result) << "Failed SHA1 iteration " << i;
    }
}

const int maxKeySize = 80;
const int maxDataSize = 54;
// HMAC-SHA-1 test vectors from http://tools.ietf.org/html/rfc2202.html
const struct {
    unsigned char key[maxKeySize];
    int keyLen;
    unsigned char data[maxDataSize];
    int dataLen;
    SHA1Block hash;
} hmacSha1Tests[] = {
    // RFC test case 1
    {{0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b,
      0x0b},
     20,
     {0x48, 0x69, 0x20, 0x54, 0x68, 0x65, 0x72, 0x65},
     8,
     SHA1Block::HashType{0xb6,
                         0x17,
                         0x31,
                         0x86,
                         0x55,
                         0x05,
                         0x72,
                         0x64,
                         0xe2,
                         0x8b,
                         0xc0,
                         0xb6,
                         0xfb,
                         0x37,
                         0x8c,
                         0x8e,
                         0xf1,
                         0x46,
                         0xbe,
                         0x00}},

    // RFC test case 3
    {{0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa},
     20,
     {0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd,
      0xdd},
     50,
     SHA1Block::HashType{0x12,
                         0x5d,
                         0x73,
                         0x42,
                         0xb9,
                         0xac,
                         0x11,
                         0xcd,
                         0x91,
                         0xa3,
                         0x9a,
                         0xf4,
                         0x8a,
                         0xa1,
                         0x7b,
                         0x4f,
                         0x63,
                         0xf1,
                         0x75,
                         0xd3}},

    // RFC test case 4
    {{0x01,
      0x02,
      0x03,
      0x04,
      0x05,
      0x06,
      0x07,
      0x08,
      0x09,
      0x0a,
      0x0b,
      0x0c,
      0x0d,
      0x0e,
      0x0f,
      0x10,
      0x11,
      0x12,
      0x13,
      0x14,
      0x15,
      0x16,
      0x17,
      0x18,
      0x19},
     25,
     {0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd,
      0xcd},
     50,
     SHA1Block::HashType{0x4c,
                         0x90,
                         0x07,
                         0xf4,
                         0x02,
                         0x62,
                         0x50,
                         0xc6,
                         0xbc,
                         0x84,
                         0x14,
                         0xf9,
                         0xbf,
                         0x50,
                         0xc8,
                         0x6c,
                         0x2d,
                         0x72,
                         0x35,
                         0xda}},

    // RFC test case 6
    {{0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa,
      0xaa},
     80,
     {0x54,
      0x65,
      0x73,
      0x74,
      0x20,
      0x55,
      0x73,
      0x69,
      0x6e,
      0x67,
      0x20,
      0x4c,
      0x61,
      0x72,
      0x67,
      0x65,
      0x72,
      0x20,
      0x54,
      0x68,
      0x61,
      0x6e,
      0x20,
      0x42,
      0x6c,
      0x6f,
      0x63,
      0x6b,
      0x2d,
      0x53,
      0x69,
      0x7a,
      0x65,
      0x20,
      0x4b,
      0x65,
      0x79,
      0x20,
      0x2d,
      0x20,
      0x48,
      0x61,
      0x73,
      0x68,
      0x20,
      0x4b,
      0x65,
      0x79,
      0x20,
      0x46,
      0x69,
      0x72,
      0x73,
      0x74},
     54,
     SHA1Block::HashType{0xaa,
                         0x4a,
                         0xe5,
                         0xe1,
                         0x52,
                         0x72,
                         0xd0,
                         0x0e,
                         0x95,
                         0x70,
                         0x56,
                         0x37,
                         0xce,
                         0x8a,
                         0x3b,
                         0x55,
                         0xed,
                         0x40,
                         0x21,
                         0x12}}};

TEST(CryptoVectors, HMACSHA1) {
    size_t numTests = sizeof(hmacSha1Tests) / sizeof(hmacSha1Tests[0]);
    for (size_t i = 0; i < numTests; i++) {
        SHA1Block result = SHA1Block::computeHmac(hmacSha1Tests[i].key,
                                                  hmacSha1Tests[i].keyLen,
                                                  hmacSha1Tests[i].data,
                                                  hmacSha1Tests[i].dataLen);
        ASSERT(hmacSha1Tests[i].hash == result) << "Failed HMAC-SHA1 iteration " << i;
    }
}

TEST(SHA1Block, BinDataRoundTrip) {
    SHA1Block::HashType rawHash;
    rawHash.fill(0);
    for (size_t i = 0; i < rawHash.size(); i++) {
        rawHash[i] = i;
    }

    SHA1Block testHash(rawHash);

    BSONObjBuilder builder;
    testHash.appendAsBinData(builder, "hash");
    auto newObj = builder.done();

    auto hashElem = newObj["hash"];
    ASSERT_EQ(BinData, hashElem.type());
    ASSERT_EQ(BinDataGeneral, hashElem.binDataType());

    int binLen = 0;
    auto rawBinData = hashElem.binData(binLen);
    ASSERT_EQ(SHA1Block::kHashLength, static_cast<size_t>(binLen));

    auto newHashStatus =
        SHA1Block::fromBinData(BSONBinData(rawBinData, binLen, hashElem.binDataType()));
    ASSERT_OK(newHashStatus.getStatus());
    ASSERT_TRUE(testHash == newHashStatus.getValue());
}

TEST(SHA1Block, CanOnlyConstructFromBinGeneral) {
    std::string dummy(SHA1Block::kHashLength, 'x');

    auto newHashStatus = SHA1Block::fromBinData(BSONBinData(dummy.c_str(), dummy.size(), newUUID));
    ASSERT_EQ(ErrorCodes::UnsupportedFormat, newHashStatus.getStatus());
}

TEST(SHA1Block, FromBinDataShouldRegectWrongSize) {
    std::string dummy(SHA1Block::kHashLength - 1, 'x');

    auto newHashStatus =
        SHA1Block::fromBinData(BSONBinData(dummy.c_str(), dummy.size(), BinDataGeneral));
    ASSERT_EQ(ErrorCodes::UnsupportedFormat, newHashStatus.getStatus());
}

TEST(SHA1Block, FromBufferShouldRejectWrongLength) {
    std::string dummy(SHA1Block::kHashLength - 1, 'x');

    auto newHashStatus =
        SHA1Block::fromBuffer(reinterpret_cast<const uint8_t*>(dummy.c_str()), dummy.size());
    ASSERT_EQ(ErrorCodes::InvalidLength, newHashStatus.getStatus());
}


}  // namespace
}  // namespace mongo
