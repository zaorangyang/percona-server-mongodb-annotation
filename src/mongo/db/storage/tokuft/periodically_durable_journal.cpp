/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include <boost/scoped_ptr.hpp>

#include "mongo/base/checked_cast.h"
#include "mongo/db/client.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/operation_context_noop.h"
#include "mongo/db/storage/journal_listener.h"
#include "mongo/db/storage/key_string.h"
#include "mongo/db/storage/kv/dictionary/kv_dictionary_update.h"
#include "mongo/db/storage/tokuft/periodically_durable_journal.h"
#include "mongo/db/storage/tokuft/tokuft_dictionary.h"
#include "mongo/db/storage/tokuft/tokuft_disk_format.h"
#include "mongo/db/storage/tokuft/tokuft_engine.h"
#include "mongo/db/storage/tokuft/tokuft_errors.h"
#include "mongo/db/storage/tokuft/tokuft_global_options.h"
#include "mongo/db/storage/tokuft/tokuft_recovery_unit.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/util/time_support.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/processinfo.h"

#include <ftcxx/db_env.hpp>
#include <ftcxx/db_env-inl.hpp>
#include <ftcxx/db.hpp>

namespace mongo {
namespace TokuFT {

    DurableJournal::DurableJournal(const ftcxx::DBEnv &env) :
        _env(env), _listener(&NoOpJournalListener::instance) {
    }

    void DurableJournal::setJournalListener(JournalListener *listener) {
        stdx::unique_lock<stdx::mutex> lock(_pointerGuardian);
        _listener = listener;
    }

    // NOTE: Returns result of log flush to caller.
    int DurableJournal::forceDurability() {
        stdx::unique_lock<stdx::mutex> lock(_pointerGuardian);
        JournalListener::Token token = _listener->getToken();
        const int r = _env.env()->log_flush(_env.env(), NULL);
        // NOTE: Does the journal listener deal with errors?
        _listener->onDurable(token);
        return r;
    }

    PeriodicallyDurableJournal::PeriodicallyDurableJournal(const ftcxx::DBEnv &env) :
        DurableJournal(env), BackgroundJob(false), isRunning(false) {}

    void PeriodicallyDurableJournal::sleepIfNeeded() const {
        int ms = storageGlobalParams.journalCommitIntervalMs;
        if (!ms) {
            ms = 100;
        }

        sleepmillis(ms);
    }

    std::string PeriodicallyDurableJournal::name() const {
        return "TokuFT::PeriodicallyDurableJournal";
    }

    void PeriodicallyDurableJournal::run() {
        this->isRunning = true;
        Client::initThread(this->name().c_str());
        while (this->isRunning) {
            this->DurableJournal::forceDurability();
            this->sleepIfNeeded();
        }
    }

    void PeriodicallyDurableJournal::stop() {
        this->isRunning = false;
        BackgroundJob::wait();
    }
} // namespace TokuFT
} // namespace mongo
