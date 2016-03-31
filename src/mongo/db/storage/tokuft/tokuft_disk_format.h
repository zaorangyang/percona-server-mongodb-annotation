// tokuft_disk_format.h

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

#include "mongo/base/status.h"
#include "mongo/bson/bson_field.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/storage/kv/slice.h"

namespace mongo {

    class OperationContext;
    class KVDictionary;

    class TokuFTDiskFormatVersion {
      public:
        enum VersionID {
            DISK_VERSION_INVALID = 0,
            DISK_VERSION_1 = 1,  // Implicit version before we serialized version numbers
            DISK_VERSION_2 = 2,  // Initial prerelease version, BSON index keys, memcmp-able RecordIds
            DISK_VERSION_3 = 3,  // Use KeyString for index entries, incompatible with earlier versions
            DISK_VERSION_4 = 4,  // KeyString gained compressed format, RecordId also uses compressed format, incompatible with earlier versions
            DISK_VERSION_5 = 5,  // KeyString gained type bits, incompatible with earlier versions
            DISK_VERSION_NEXT,
            DISK_VERSION_CURRENT = DISK_VERSION_NEXT - 1,
            MIN_SUPPORTED_VERSION = DISK_VERSION_5,
            MAX_SUPPORTED_VERSION = DISK_VERSION_CURRENT,
            FIRST_SERIALIZED_VERSION = DISK_VERSION_2,
        };

      private:
        VersionID _startupVersion;
        VersionID _currentVersion;
        KVDictionary *_metadataDict;

        static const Slice versionInfoKey;
        static const BSONField<int> originalVersionField;
        static const BSONField<int> currentVersionField;
        static const BSONField<BSONArray> historyField;
        static const BSONField<int> upgradedToField;
        static const BSONField<Date_t> upgradedAtField;
        static const BSONField<BSONObj> upgradedByField;
        static const BSONField<std::string> mongodbVersionField;
        static const BSONField<std::string> mongodbGitField;
        static const BSONField<std::string> tokuftGitField;
        static const BSONField<std::string> sysInfoField;

        Status upgradeToVersion(OperationContext *opCtx, VersionID targetVersion);

      public:
        TokuFTDiskFormatVersion(KVDictionary *metadataDict);

        Status initialize(OperationContext *opCtx);

        Status upgradeToCurrent(OperationContext *opCtx);

        Status getInfo(OperationContext *opCtx, BSONObj &b) const;
    };

} // namespace mongo
