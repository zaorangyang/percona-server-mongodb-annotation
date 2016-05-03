/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2016, Percona and/or its affiliates. All rights reserved.

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

#include <mutex>
#include <string>

#include <boost/scoped_ptr.hpp>

#include "mongo/util/background.h"

#include <ftcxx/db_env.hpp>

namespace mongo {

    class JournalListener;

namespace TokuFT {

    class DurableJournal {
    public:
        DurableJournal(const ftcxx::DBEnv &env);
        virtual ~DurableJournal() {}
        void setJournalListener(JournalListener *listener);
        int forceDurability();
    private:
        stdx::mutex _pointerGuardian;
        const ftcxx::DBEnv &_env;
        JournalListener *_listener;
    };

    class PeriodicallyDurableJournal : public DurableJournal, public BackgroundJob {
    public:
        PeriodicallyDurableJournal(const ftcxx::DBEnv &env);
        virtual std::string name() const;
        virtual void run();
    private:
        void sleepIfNeeded() const;
    };

} // namespace TokuFT
} // namespace mongo
