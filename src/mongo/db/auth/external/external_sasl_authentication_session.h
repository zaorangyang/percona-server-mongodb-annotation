/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
/*======
This file is part of Percona Server for MongoDB.

Copyright (C) 2018-present Percona and/or its affiliates. All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the Server Side Public License, version 1,
    as published by MongoDB, Inc.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Server Side Public License for more details.

    You should have received a copy of the Server Side Public License
    along with this program. If not, see
    <http://www.mongodb.com/licensing/server-side-public-license>.

    As a special exception, the copyright holders give permission to link the
    code of portions of this program with the OpenSSL library under certain
    conditions as described in each individual source file and distribute
    linked combinations including the program with the OpenSSL library. You
    must comply with the Server Side Public License in all respects for
    all of the code used other than as permitted herein. If you modify file(s)
    with this exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do so,
    delete this exception statement from your version. If you delete this
    exception statement from all source files in the program, then also delete
    it in the license file.
======= */

#pragma once

#include <boost/scoped_ptr.hpp>
#include <cstdint>
#include <string>

#include "mongo/base/disallow_copying.h"
#include "mongo/base/status.h"
#include "mongo/base/string_data.h"
#include "mongo/db/auth/authentication_session.h"
#include "mongo/db/auth/sasl_authentication_session.h"

namespace mongo {

    /**
     * Authentication session data for the server side of external SASL authentication.
     */
    class ExternalSaslAuthenticationSession : public SaslAuthenticationSession {
        MONGO_DISALLOW_COPYING(ExternalSaslAuthenticationSession);
    public:
        explicit ExternalSaslAuthenticationSession(AuthorizationSession* authSession);
        virtual ~ExternalSaslAuthenticationSession();
        virtual Status start(StringData authenticationDatabase,
                             StringData mechanism,
                             StringData serviceName,
                             StringData serviceHostname,
                             int64_t conversationId,
                             bool autoAuthorize);
        virtual Status step(StringData inputData, std::string* outputData);
        virtual std::string getPrincipalId() const;
        virtual const char* getMechanism() const;
        static Status getInitializationError(int);

    private:
        Status initializeConnection();
        void processInitialClientPayload(const StringData& payload);
        void processNextClientPayload(const StringData& payload);
        void updateDoneStatus();
        void copyStepOutput(std::string *output) const;
        Status getStepResult() const;
        int getUserName(const char **username) const;

    private:
        sasl_conn_t * _saslConnection;
        std::string _mechanism;
        struct SaslServerResults {
            int result;
            const char *output;
            unsigned length;
            inline void initialize_results() {
                result = SASL_OK;
                output = NULL;
                length = 0;
            };
            inline bool resultsAreOK() const {
                return result == SASL_OK;
            };
            inline bool resultsShowNoError() const {
                return result == SASL_OK || result == SASL_CONTINUE;
            }
        } _results;
    };

}  // namespace mongo
