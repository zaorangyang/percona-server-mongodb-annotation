/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#pragma once

#include <string>

#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/operation_context.h"

namespace mongo {

struct MultikeyPathInfo {
    NamespaceString nss;
    std::string indexName;
    MultikeyPaths multikeyPaths;
};

/**
 * An OperationContext decoration that tracks which indexes should be made multikey. This is used
 * by IndexCatalogEntryImpl::setMultikey() to track what indexes should be set as multikey during
 * secondary oplog application. This both marks if the multikey path information should be tracked
 * instead of set immediately and saves the multikey path information for later if needed.
 */
class MultikeyPathTracker {
public:
    static const OperationContext::Decoration<MultikeyPathTracker> get;

    // Decoration requires a default constructor.
    MultikeyPathTracker() = default;

    /**
     * Appends the provided multikey path information to the list of indexes to set as multikey
     * after the current replication batch finishes.
     * Must call startTrackingMultikeyPathInfo() first.
     */
    void addMultikeyPathInfo(MultikeyPathInfo info);

    /**
     * Returns the multikey path information that has been saved.
     */
    const std::vector<MultikeyPathInfo>& getMultikeyPathInfo() const;

    /**
     * Specifies that we should track multikey path information on this MultikeyPathTracker. This is
     * only expected to be called during oplog application on secondaries. We cannot simply check
     * 'canAcceptWritesFor' because background index builds use their own OperationContext and
     * cannot store their multikey path info here.
     */
    void startTrackingMultikeyPathInfo();

    /**
     * Specifies to stop tracking multikey path information.
     */
    void stopTrackingMultikeyPathInfo();

    /**
     * Returns if we've called startTrackingMultikeyPathInfo() and not yet called
     * stopTrackingMultikeyPathInfo().
     */
    bool isTrackingMultikeyPathInfo() const;


private:
    std::vector<MultikeyPathInfo> _multikeyPathInfo;
    bool _trackMultikeyPathInfo = false;
};

}  // namespace mongo
