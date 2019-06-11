/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/util/options_parser/startup_option_init.h"

/*
 * These are the initializer groups for command line and config file option registration, parsing,
 * validation, and storage
 */

/* Groups for all of option handling */
MONGO_INITIALIZER_GROUP(BeginStartupOptionHandling,
                        ("GlobalLogManager", "ValidateLocale"),
                        ("EndStartupOptionHandling"))

/* Groups for option registration */
MONGO_INITIALIZER_GROUP(BeginStartupOptionRegistration,
                        ("BeginStartupOptionHandling"),
                        ("EndStartupOptionRegistration"))

/* Groups for general option registration (useful for controlling the order in which options are
 * registered for modules, which affects the order in which they are printed in help output) */
MONGO_INITIALIZER_GROUP(BeginGeneralStartupOptionRegistration,
                        ("BeginStartupOptionRegistration"),
                        ("EndGeneralStartupOptionRegistration"))
MONGO_INITIALIZER_GROUP(EndGeneralStartupOptionRegistration,
                        ("BeginGeneralStartupOptionRegistration"),
                        ("EndStartupOptionRegistration"))

MONGO_INITIALIZER_GROUP(EndStartupOptionRegistration,
                        ("BeginStartupOptionRegistration"),
                        ("BeginStartupOptionParsing"))

/* Groups for option parsing */
MONGO_INITIALIZER_GROUP(BeginStartupOptionParsing,
                        ("EndStartupOptionRegistration"),
                        ("EndStartupOptionParsing"))
MONGO_INITIALIZER_GROUP(EndStartupOptionParsing,
                        ("BeginStartupOptionParsing"),
                        ("BeginStartupOptionValidation"))

/* Groups for option validation */
MONGO_INITIALIZER_GROUP(BeginStartupOptionValidation,
                        ("EndStartupOptionParsing"),
                        ("EndStartupOptionValidation"))
MONGO_INITIALIZER_GROUP(EndStartupOptionValidation,
                        ("BeginStartupOptionValidation"),
                        ("BeginStartupOptionSetup"))

/* Groups for option setup */
MONGO_INITIALIZER_GROUP(BeginStartupOptionSetup,
                        ("EndStartupOptionValidation"),
                        ("EndStartupOptionSetup"))
MONGO_INITIALIZER_GROUP(EndStartupOptionSetup,
                        ("BeginStartupOptionSetup"),
                        ("BeginStartupOptionStorage"))

/* Groups for option storage */
MONGO_INITIALIZER_GROUP(BeginStartupOptionStorage,
                        ("EndStartupOptionSetup"),
                        ("EndStartupOptionStorage"))
MONGO_INITIALIZER_GROUP(EndStartupOptionStorage,
                        ("BeginStartupOptionStorage"),
                        ("BeginPostStartupOptionStorage"))

/* Groups for post option storage */
MONGO_INITIALIZER_GROUP(BeginPostStartupOptionStorage,
                        ("EndStartupOptionStorage"),
                        ("EndPostStartupOptionStorage"))
MONGO_INITIALIZER_GROUP(EndPostStartupOptionStorage,
                        ("BeginPostStartupOptionStorage"),
                        ("EndStartupOptionHandling"))

MONGO_INITIALIZER_GROUP(EndStartupOptionHandling, ("BeginStartupOptionHandling"), ("default"))
