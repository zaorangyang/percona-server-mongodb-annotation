/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include <algorithm>

#include "mongo/base/data_range.h"

#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

#include "aead_encryption.h"

namespace mongo {
namespace {

// The first test is to ensure that the length of the cipher is correct when
// calling AEAD encrypt.
TEST(AEAD, aeadCipherOutputLength) {
    size_t plainTextLen = 16;
    auto cipherLen = crypto::aeadCipherOutputLength(plainTextLen);
    ASSERT_EQ(cipherLen, size_t(80));

    plainTextLen = 10;
    cipherLen = crypto::aeadCipherOutputLength(plainTextLen);
    ASSERT_EQ(cipherLen, size_t(64));
}

TEST(AEAD, EncryptAndDecrypt) {
    // Test case from RFC:
    // https://tools.ietf.org/html/draft-mcgrew-aead-aes-cbc-hmac-sha2-05#section-5.4

    const uint8_t aesAlgorithm = 0x1;

    std::array<uint8_t, 64> symKey = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
        0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
        0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
        0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};

    SecureVector<uint8_t> aesVector = SecureVector<uint8_t>(symKey.begin(), symKey.end());
    SymmetricKey key = SymmetricKey(aesVector, aesAlgorithm, "aeadEncryptDecryptTest");

    const std::array<uint8_t, 128> plainTextTest = {
        0x41, 0x20, 0x63, 0x69, 0x70, 0x68, 0x65, 0x72, 0x20, 0x73, 0x79, 0x73, 0x74, 0x65, 0x6d,
        0x20, 0x6d, 0x75, 0x73, 0x74, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x65,
        0x71, 0x75, 0x69, 0x72, 0x65, 0x64, 0x20, 0x74, 0x6f, 0x20, 0x62, 0x65, 0x20, 0x73, 0x65,
        0x63, 0x72, 0x65, 0x74, 0x2c, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x69, 0x74, 0x20, 0x6d, 0x75,
        0x73, 0x74, 0x20, 0x62, 0x65, 0x20, 0x61, 0x62, 0x6c, 0x65, 0x20, 0x74, 0x6f, 0x20, 0x66,
        0x61, 0x6c, 0x6c, 0x20, 0x69, 0x6e, 0x74, 0x6f, 0x20, 0x74, 0x68, 0x65, 0x20, 0x68, 0x61,
        0x6e, 0x64, 0x73, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x65, 0x6e, 0x65, 0x6d,
        0x79, 0x20, 0x77, 0x69, 0x74, 0x68, 0x6f, 0x75, 0x74, 0x20, 0x69, 0x6e, 0x63, 0x6f, 0x6e,
        0x76, 0x65, 0x6e, 0x69, 0x65, 0x6e, 0x63, 0x65};

    std::array<uint8_t, 192> cryptoBuffer = {};

    std::array<uint8_t, 16> iv = {0x1a,
                                  0xf3,
                                  0x8c,
                                  0x2d,
                                  0xc2,
                                  0xb9,
                                  0x6f,
                                  0xfd,
                                  0xd8,
                                  0x66,
                                  0x94,
                                  0x09,
                                  0x23,
                                  0x41,
                                  0xbc,
                                  0x04};

    std::array<uint8_t, 42> associatedData = {
        0x54, 0x68, 0x65, 0x20, 0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64, 0x20, 0x70, 0x72, 0x69,
        0x6e, 0x63, 0x69, 0x70, 0x6c, 0x65, 0x20, 0x6f, 0x66, 0x20, 0x41, 0x75, 0x67, 0x75,
        0x73, 0x74, 0x65, 0x20, 0x4b, 0x65, 0x72, 0x63, 0x6b, 0x68, 0x6f, 0x66, 0x66, 0x73};

    const size_t dataLen = 42;

    std::array<uint8_t, sizeof(uint64_t)> dataLenBitsEncodedStorage;
    DataRange dataLenBitsEncoded(dataLenBitsEncodedStorage);
    dataLenBitsEncoded.write<BigEndian<uint64_t>>(dataLen * 8);

    const size_t outLen = crypto::aeadCipherOutputLength(128);

    ASSERT_OK(crypto::aeadEncryptWithIV(symKey,
                                        plainTextTest.data(),
                                        plainTextTest.size(),
                                        iv.data(),
                                        iv.size(),
                                        associatedData.data(),
                                        dataLen,
                                        dataLenBitsEncoded,
                                        cryptoBuffer.data(),
                                        outLen));

    std::array<uint8_t, 192> cryptoBufferTest = {
        0x1a, 0xf3, 0x8c, 0x2d, 0xc2, 0xb9, 0x6f, 0xfd, 0xd8, 0x66, 0x94, 0x09, 0x23, 0x41, 0xbc,
        0x04, 0x4a, 0xff, 0xaa, 0xad, 0xb7, 0x8c, 0x31, 0xc5, 0xda, 0x4b, 0x1b, 0x59, 0x0d, 0x10,
        0xff, 0xbd, 0x3d, 0xd8, 0xd5, 0xd3, 0x02, 0x42, 0x35, 0x26, 0x91, 0x2d, 0xa0, 0x37, 0xec,
        0xbc, 0xc7, 0xbd, 0x82, 0x2c, 0x30, 0x1d, 0xd6, 0x7c, 0x37, 0x3b, 0xcc, 0xb5, 0x84, 0xad,
        0x3e, 0x92, 0x79, 0xc2, 0xe6, 0xd1, 0x2a, 0x13, 0x74, 0xb7, 0x7f, 0x07, 0x75, 0x53, 0xdf,
        0x82, 0x94, 0x10, 0x44, 0x6b, 0x36, 0xeb, 0xd9, 0x70, 0x66, 0x29, 0x6a, 0xe6, 0x42, 0x7e,
        0xa7, 0x5c, 0x2e, 0x08, 0x46, 0xa1, 0x1a, 0x09, 0xcc, 0xf5, 0x37, 0x0d, 0xc8, 0x0b, 0xfe,
        0xcb, 0xad, 0x28, 0xc7, 0x3f, 0x09, 0xb3, 0xa3, 0xb7, 0x5e, 0x66, 0x2a, 0x25, 0x94, 0x41,
        0x0a, 0xe4, 0x96, 0xb2, 0xe2, 0xe6, 0x60, 0x9e, 0x31, 0xe6, 0xe0, 0x2c, 0xc8, 0x37, 0xf0,
        0x53, 0xd2, 0x1f, 0x37, 0xff, 0x4f, 0x51, 0x95, 0x0b, 0xbe, 0x26, 0x38, 0xd0, 0x9d, 0xd7,
        0xa4, 0x93, 0x09, 0x30, 0x80, 0x6d, 0x07, 0x03, 0xb1, 0xf6, 0x4d, 0xd3, 0xb4, 0xc0, 0x88,
        0xa7, 0xf4, 0x5c, 0x21, 0x68, 0x39, 0x64, 0x5b, 0x20, 0x12, 0xbf, 0x2e, 0x62, 0x69, 0xa8,
        0xc5, 0x6a, 0x81, 0x6d, 0xbc, 0x1b, 0x26, 0x77, 0x61, 0x95, 0x5b, 0xc5};

    ASSERT_EQ(0, std::memcmp(cryptoBuffer.data(), cryptoBufferTest.data(), 192));

    std::array<uint8_t, 144> plainText = {};
    size_t plainTextDecryptLen = 144;
    ASSERT_OK(crypto::aeadDecrypt(key,
                                  ConstDataRange(cryptoBuffer),
                                  associatedData.data(),
                                  dataLen,
                                  plainText.data(),
                                  &plainTextDecryptLen));

    ASSERT_EQ(0, std::memcmp(plainText.data(), plainTextTest.data(), 128));

    // Decrypt should fail if we alter the key.
    (*aesVector)[0] ^= 1;
    key = SymmetricKey(aesVector, aesAlgorithm, "aeadEncryptDecryptTest");
    ASSERT_NOT_OK(crypto::aeadDecrypt(key,
                                      ConstDataRange(cryptoBuffer),
                                      associatedData.data(),
                                      dataLen,
                                      plainText.data(),
                                      &plainTextDecryptLen));
}
}  // namespace
}  // namespace mongo
