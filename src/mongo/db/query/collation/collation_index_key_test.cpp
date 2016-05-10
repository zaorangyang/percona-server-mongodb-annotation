/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/query/collation/collation_index_key.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/query/collation/collator_interface_mock.h"
#include "mongo/unittest/unittest.h"

namespace {

using namespace mongo;

TEST(CollationIndexKeyTest, ShouldUseCollationKeyFalseWithNullCollator) {
    BSONObj obj = BSON("foo"
                       << "string");
    ASSERT_FALSE(CollationIndexKey::shouldUseCollationIndexKey(obj.firstElement(), nullptr));
}

TEST(CollationIndexKeyTest, ShouldUseCollationKeyFalseWithNonStringElement) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    BSONObj obj = BSON("foo" << BSON("bar"
                                     << "string"));
    ASSERT_FALSE(CollationIndexKey::shouldUseCollationIndexKey(obj.firstElement(), &collator));
}

TEST(CollationIndexKeyTest, ShouldUseCollationKeyTrueWithStringElement) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    BSONObj obj = BSON("foo"
                       << "string");
    ASSERT_TRUE(CollationIndexKey::shouldUseCollationIndexKey(obj.firstElement(), &collator));
}

TEST(CollationIndexKeyTest, CollationAwareAppendReversesStringWithReverseMockCollator) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    BSONObj dataObj = BSON("foo"
                           << "string");
    BSONObjBuilder out;
    CollationIndexKey::collationAwareIndexKeyAppend(dataObj.firstElement(), &collator, &out);
    ASSERT_EQ(out.obj(),
              BSON(""
                   << "gnirts"));
}

TEST(CollationIndexKeyTest, CollationAwareAppendCorrectlySerializesEmptyComparisonKey) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    BSONObjBuilder builder;
    builder.append("foo", StringData());
    BSONObj dataObj = builder.obj();

    BSONObjBuilder expectedBuilder;
    expectedBuilder.append("", StringData());
    BSONObj expectedObj = expectedBuilder.obj();

    BSONObjBuilder out;
    CollationIndexKey::collationAwareIndexKeyAppend(dataObj.firstElement(), &collator, &out);
    ASSERT_EQ(out.obj(), expectedObj);
}

TEST(CollationIndexKeyTest, CollationAwareAppendCorrectlySerializesWithEmbeddedNullByte) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    BSONObjBuilder builder;
    builder.append("foo", "a\0b"_sd);
    BSONObj dataObj = builder.obj();

    BSONObjBuilder expectedBuilder;
    expectedBuilder.append("", "b\0a"_sd);
    BSONObj expectedObj = expectedBuilder.obj();

    BSONObjBuilder out;
    CollationIndexKey::collationAwareIndexKeyAppend(dataObj.firstElement(), &collator, &out);
    ASSERT_EQ(out.obj(), expectedObj);
}

}  // namespace
