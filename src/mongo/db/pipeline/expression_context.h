/**
 * Copyright (c) 2011 10gen Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "mongo/pch.h"

#include "mongo/util/intrusive_counter.h"
#include "mongo/db/namespace_string.h"

namespace mongo {

    class InterruptStatus;

    class ExpressionContext :
        public IntrusiveCounterUnsigned {
    public:
        virtual ~ExpressionContext();

        void setInShard(bool b) { inShard = b; }
        void setInRouter(bool b) { inRouter = b; }
        void setExtSortAllowed(bool b) { extSortAllowed = b; }
        void setNs(NamespaceString ns) { _ns = ns; }

        bool getInShard() const { return inShard; }
        bool getInRouter() const { return inRouter; }
        bool getExtSortAllowed() const { return extSortAllowed; }
        const NamespaceString& getNs() const { return _ns; }

        /**
           Used by a pipeline to check for interrupts so that killOp() works.

           @throws if the operation has been interrupted
         */
        void checkForInterrupt();

        ExpressionContext* clone();

        static ExpressionContext *create(InterruptStatus *pStatus, const NamespaceString& ns);

    private:
        ExpressionContext(InterruptStatus *pStatus, const NamespaceString& ns);
        
        bool inShard;
        bool inRouter;
        bool extSortAllowed;
        unsigned intCheckCounter; // interrupt check counter
        InterruptStatus *const pStatus;
        NamespaceString _ns;
    };
}
