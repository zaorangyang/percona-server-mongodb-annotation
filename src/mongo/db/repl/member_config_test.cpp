/**
 *    Copyright 2014 MongoDB Inc.
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

#include <algorithm>
#include <vector>

#include "mongo/db/jsobj.h"
#include "mongo/db/repl/member_config.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace repl {
namespace {

    TEST(MemberConfig, ParseMinimalMemberConfigAndCheckDefaults) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "localhost:12345")));
        ASSERT_EQUALS(0, mc.getId());
        ASSERT_EQUALS(HostAndPort("localhost", 12345), mc.getHostAndPort());
        ASSERT_EQUALS(1.0, mc.getPriority());
        ASSERT_EQUALS(0, mc.getSlaveDelay().total_seconds());
        ASSERT_TRUE(mc.isVoter());
        ASSERT_FALSE(mc.isHidden());
        ASSERT_FALSE(mc.isArbiter());
        ASSERT_TRUE(mc.shouldBuildIndexes());
        ASSERT_EQUALS(0U, mc.getNumTags());
        ASSERT_OK(mc.validate());
    }

    TEST(MemberConfig, ParseFailsWithIllegalFieldName) {
        MemberConfig mc;
        ASSERT_EQUALS(ErrorCodes::BadValue,
                      mc.initialize(BSON("_id" << 0 << "host" << "localhost" << "frim" << 1)));
    }

    TEST(MemberConfig, ParseFailsWithMissingIdField) {
        MemberConfig mc;
        ASSERT_EQUALS(ErrorCodes::NoSuchKey, mc.initialize(BSON("host" << "localhost:12345")));
    }

    TEST(MemberConfig, ParseFailsWithBadIdField) {
        MemberConfig mc;
        ASSERT_EQUALS(ErrorCodes::NoSuchKey, mc.initialize(BSON("host" << "localhost:12345")));
        ASSERT_EQUALS(ErrorCodes::TypeMismatch,
                      mc.initialize(BSON("_id" << "0" << "host" << "localhost:12345")));
        ASSERT_EQUALS(ErrorCodes::TypeMismatch,
                      mc.initialize(BSON("_id" << Date_t(0) << "host" << "localhost:12345")));
    }

    TEST(MemberConfig, ParseFailsWithMissingHostField) {
        MemberConfig mc;
        ASSERT_EQUALS(ErrorCodes::NoSuchKey, mc.initialize(BSON("_id" << 0)));
    }


    TEST(MemberConfig, ParseFailsWithBadHostField) {
        MemberConfig mc;
        ASSERT_EQUALS(ErrorCodes::TypeMismatch, mc.initialize(BSON("_id" << 0 << "host" << 0)));
        ASSERT_EQUALS(ErrorCodes::FailedToParse, mc.initialize(BSON("_id" << 0 << "host" << "")));
        ASSERT_EQUALS(ErrorCodes::FailedToParse,
                      mc.initialize(BSON("_id" << 0 << "host" << "myhost:zabc")));
    }

    TEST(MemberConfig, ParseArbiterOnly) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "arbiterOnly" << 1.0)));
        ASSERT_TRUE(mc.isArbiter());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "arbiterOnly" << false)));
        ASSERT_TRUE(!mc.isArbiter());
    }

    TEST(MemberConfig, ParseHidden) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "hidden" << 1.0)));
        ASSERT_TRUE(mc.isHidden());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "hidden" << false)));
        ASSERT_TRUE(!mc.isHidden());
        ASSERT_EQUALS(ErrorCodes::TypeMismatch,
                      mc.initialize(BSON("_id" << 0 << "host" << "h" << "hidden" << "1.0")));
    }

    TEST(MemberConfig, ParseBuildIndexes) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "buildIndexes" << 1.0)));
        ASSERT_TRUE(mc.shouldBuildIndexes());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "buildIndexes" << false)));
        ASSERT_TRUE(!mc.shouldBuildIndexes());
    }

    TEST(MemberConfig, ParseVotes) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << 1.0)));
        ASSERT_TRUE(mc.isVoter());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << 0)));
        ASSERT_FALSE(mc.isVoter());

        // For backwards compatibility, truncate 1.X to 1, and 0.X to 0 (and -0.X to 0).
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << 1.5)));
        ASSERT_TRUE(mc.isVoter());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << 0.5)));
        ASSERT_FALSE(mc.isVoter());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << -0.5)));
        ASSERT_FALSE(mc.isVoter());
        ASSERT_EQUALS(ErrorCodes::BadValue,
                      mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << 2)));

        ASSERT_EQUALS(ErrorCodes::TypeMismatch,
                      mc.initialize(BSON("_id" << 0 << "host" << "h" << "votes" << Date_t(2))));
    }

    TEST(MemberConfig, ParsePriority) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1)));
        ASSERT_EQUALS(1.0, mc.getPriority());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 0)));
        ASSERT_EQUALS(0.0, mc.getPriority());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 100.8)));
        ASSERT_EQUALS(100.8, mc.getPriority());

        ASSERT_EQUALS(ErrorCodes::TypeMismatch,
                      mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << Date_t(2))));
    }

    TEST(MemberConfig, ParseSlaveDelay) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "slaveDelay" << 100)));
        ASSERT_EQUALS(100, mc.getSlaveDelay().total_seconds());
    }

    TEST(MemberConfig, ParseTags) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" <<
                                     "tags" << BSON("k1" << "v1" << "k2" << "v2"))));
        ASSERT_EQUALS(2U, mc.getNumTags());
        std::vector<ReplicaSetTag> tags(mc.tagsBegin(), mc.tagsEnd());
        ASSERT_EQUALS(2U, tags.size());
        ASSERT_EQUALS(1, std::count(tags.begin(), tags.end(), ReplicaSetTag("k1", "v1")));
        ASSERT_EQUALS(1, std::count(tags.begin(), tags.end(), ReplicaSetTag("k2", "v2")));
    }

    TEST(MemberConfig, ValidateFailsWithIdOutOfRange) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << -1 << "host" << "localhost::12345")));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 256 << "host" << "localhost::12345")));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
    }

    TEST(MemberConfig, ValidatePriorityRanges) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 0)));
        ASSERT_OK(mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1000)));
        ASSERT_OK(mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << -1)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1001)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
    }

    TEST(MemberConfig, ValidateSlaveDelays) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 0 <<
                                     "slaveDelay" << 0)));
        ASSERT_OK(mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 0 <<
                                     "slaveDelay" << 3600 * 10)));
        ASSERT_OK(mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 0 <<
                                     "slaveDelay" << -1)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 0 <<
                                     "slaveDelay" << 3600 * 24 * 400)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
    }

    TEST(MemberConfig, ValidatePriorityAndSlaveDelayRelationship) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1 <<
                                     "slaveDelay" << 60)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
    }

    TEST(MemberConfig, ValidatePriorityAndHiddenRelationship) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1 <<
                                     "hidden" << true)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1 <<
                                     "hidden" << false)));
        ASSERT_OK(mc.validate());
    }

    TEST(MemberConfig, ValidatePriorityAndBuildIndexesRelationship) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1 <<
                                     "buildIndexes" << false)));

        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" << "priority" << 1 <<
                                     "buildIndexes" << true)));
        ASSERT_OK(mc.validate());
    }

    TEST(MemberConfig, ValidateArbiterVotesRelationship) {
        MemberConfig mc;
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" <<
                                     "votes" << 1 << "arbiterOnly" << true)));
        ASSERT_OK(mc.validate());

        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" <<
                                     "votes" << 0 << "arbiterOnly" << false)));
        ASSERT_OK(mc.validate());
        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" <<
                                     "votes" << 1 << "arbiterOnly" << false)));
        ASSERT_OK(mc.validate());

        ASSERT_OK(mc.initialize(BSON("_id" << 0 << "host" << "h" <<
                                     "votes" << 0 << "arbiterOnly" << true)));
        ASSERT_EQUALS(ErrorCodes::BadValue, mc.validate());
    }

}  // namespace
}  // namespace repl
}  // namespace mongo
