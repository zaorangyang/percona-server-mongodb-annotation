/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
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

#include <sasl/sasl.h>

#include "mongo/base/status.h"
#include "mongo/base/string_data.h"
#include "mongo/db/auth/sasl_mechanism_policies.h"

namespace mongo {

class SaslExternalLDAPServerMechanism : public MakeServerMechanism<PLAINPolicy> {
public:
    static const bool isInternal = false;

    explicit SaslExternalLDAPServerMechanism(std::string authenticationDatabase)
        : MakeServerMechanism<PLAINPolicy>(std::move(authenticationDatabase)) {}

    virtual ~SaslExternalLDAPServerMechanism();

private:
    int _step{0};
    sasl_conn_t * _saslConnection{nullptr};

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

    StatusWith<std::tuple<bool, std::string>> stepImpl(OperationContext* opCtx,
                                                       StringData input) final;
    virtual StringData getPrincipalName() const override final;

    Status initializeConnection();
    StatusWith<std::tuple<bool, std::string>> processInitialClientPayload(const StringData& payload);
    StatusWith<std::tuple<bool, std::string>> processNextClientPayload(const StringData& payload);
    StatusWith<std::tuple<bool, std::string>> getStepResult() const;
};

class ExternalLDAPServerFactory : public MakeServerFactory<SaslExternalLDAPServerMechanism> {
public:
    bool canMakeMechanismForUser(const User* user) const final {
        auto credentials = user->getCredentials();
        return credentials.isExternal && (credentials.scram<SHA1Block>().isValid() ||
                                          credentials.scram<SHA256Block>().isValid());
    }
};


}  // namespace mongo
