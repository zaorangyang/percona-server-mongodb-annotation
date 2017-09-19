// #file dbtests.cpp : Runs db unit tests.
//

/**
 *    Copyright (C) 2008 10gen Inc.
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

#include "mongo/dbtests/dbtests.h"

#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/client/dbclientinterface.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_manager_global.h"
#include "mongo/db/catalog/index_create.h"
#include "mongo/db/commands.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/logical_clock.h"
#include "mongo/db/repl/replication_coordinator_global.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/service_context.h"
#include "mongo/db/service_context_d.h"
#include "mongo/db/wire_version.h"
#include "mongo/dbtests/framework.h"
#include "mongo/scripting/engine.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/clock_source_mock.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/signal_handlers_synchronous.h"
#include "mongo/util/startup_test.h"
#include "mongo/util/text.h"

namespace mongo {
namespace dbtests {
namespace {
const auto kIndexVersion = IndexDescriptor::IndexVersion::kV2;
}  // namespace

void initWireSpec() {
    WireSpec& spec = WireSpec::instance();
    // accept from any version
    spec.incoming.minWireVersion = RELEASE_2_4_AND_BEFORE;
    spec.incoming.maxWireVersion = LATEST_WIRE_VERSION;
    // connect to any version
    spec.outgoing.minWireVersion = RELEASE_2_4_AND_BEFORE;
    spec.outgoing.maxWireVersion = LATEST_WIRE_VERSION;
}

Status createIndex(OperationContext* opCtx, StringData ns, const BSONObj& keys, bool unique) {
    BSONObjBuilder specBuilder;
    specBuilder.append("name", DBClientBase::genIndexName(keys));
    specBuilder.append("ns", ns);
    specBuilder.append("key", keys);
    specBuilder.append("v", static_cast<int>(kIndexVersion));
    if (unique) {
        specBuilder.appendBool("unique", true);
    }
    return createIndexFromSpec(opCtx, ns, specBuilder.done());
}

Status createIndexFromSpec(OperationContext* opCtx, StringData ns, const BSONObj& spec) {
    AutoGetOrCreateDb autoDb(opCtx, nsToDatabaseSubstring(ns), MODE_X);
    Collection* coll;
    {
        WriteUnitOfWork wunit(opCtx);
        coll = autoDb.getDb()->getOrCreateCollection(opCtx, NamespaceString(ns));
        invariant(coll);
        wunit.commit();
    }
    MultiIndexBlock indexer(opCtx, coll);
    Status status = indexer.init(spec).getStatus();
    if (status == ErrorCodes::IndexAlreadyExists) {
        return Status::OK();
    }
    if (!status.isOK()) {
        return status;
    }
    status = indexer.insertAllDocumentsInCollection();
    if (!status.isOK()) {
        return status;
    }
    WriteUnitOfWork wunit(opCtx);
    indexer.commit();
    wunit.commit();
    return Status::OK();
}

}  // namespace dbtests
}  // namespace mongo


int dbtestsMain(int argc, char** argv, char** envp) {
    Command::testCommandsEnabled = true;
    ::mongo::setupSynchronousSignalHandlers();
    mongo::dbtests::initWireSpec();
    mongo::runGlobalInitializersOrDie(argc, argv, envp);
    serverGlobalParams.featureCompatibility.version.store(
        ServerGlobalParams::FeatureCompatibility::Version::k36);
    serverGlobalParams.featureCompatibility.isSchemaVersion36.store(true);
    repl::ReplSettings replSettings;
    replSettings.setOplogSizeBytes(10 * 1024 * 1024);
    ServiceContext* service = getGlobalServiceContext();

    auto logicalClock = stdx::make_unique<LogicalClock>(service);
    LogicalClock::set(service, std::move(logicalClock));

    auto fastClock = stdx::make_unique<ClockSourceMock>();
    // Timestamps are split into two 32-bit integers, seconds and "increments". Currently (but
    // maybe not for eternity), a Timestamp with a value of `0` seconds is always considered
    // "null" by `Timestamp::isNull`, regardless of its increment value. Ticking the
    // `ClockSourceMock` only bumps the "increment" counter, thus by default, generating "null"
    // timestamps. Bumping by one second here avoids any accidental interpretations.
    fastClock->advance(Seconds(1));
    service->setFastClockSource(std::move(fastClock));

    auto preciseClock = stdx::make_unique<ClockSourceMock>();
    // See above.
    preciseClock->advance(Seconds(1));
    service->setPreciseClockSource(std::move(preciseClock));

    repl::setGlobalReplicationCoordinator(
        new repl::ReplicationCoordinatorMock(service, replSettings));
    repl::getGlobalReplicationCoordinator()
        ->setFollowerMode(repl::MemberState::RS_PRIMARY)
        .ignore();

    getGlobalAuthorizationManager()->setAuthEnabled(false);
    ScriptEngine::setup();
    StartupTest::runTests();
    return mongo::dbtests::runDbTests(argc, argv);
}

#if defined(_WIN32)
// In Windows, wmain() is an alternate entry point for main(), and receives the same parameters
// as main() but encoded in Windows Unicode (UTF-16); "wide" 16-bit wchar_t characters.  The
// WindowsCommandLine object converts these wide character strings to a UTF-8 coded equivalent
// and makes them available through the argv() and envp() members.  This enables dbtestsMain()
// to process UTF-8 encoded arguments and environment variables without regard to platform.
int wmain(int argc, wchar_t* argvW[], wchar_t* envpW[]) {
    WindowsCommandLine wcl(argc, argvW, envpW);
    int exitCode = dbtestsMain(argc, wcl.argv(), wcl.envp());
    quickExit(exitCode);
}
#else
int main(int argc, char* argv[], char** envp) {
    int exitCode = dbtestsMain(argc, argv, envp);
    quickExit(exitCode);
}
#endif
