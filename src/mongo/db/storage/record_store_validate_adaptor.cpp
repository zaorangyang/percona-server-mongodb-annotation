/*-
 *    Copyright (C) 2017 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include <third_party/murmurhash3/MurmurHash3.h>

#include "mongo/bson/bsonobj.h"
#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/index/index_access_method.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/storage/key_string.h"
#include "mongo/db/storage/record_store.h"
#include "mongo/db/storage/record_store_validate_adaptor.h"
#include "mongo/rpc/object_check.h"

namespace mongo {

namespace {
uint32_t hashIndexEntry(KeyString& ks, uint32_t hash) {
    MurmurHash3_x86_32(ks.getTypeBits().getBuffer(), ks.getTypeBits().getSize(), hash, &hash);
    MurmurHash3_x86_32(ks.getBuffer(), ks.getSize(), hash, &hash);
    return hash % kKeyCountTableSize;
}
}

Status RecordStoreValidateAdaptor::validate(const RecordId& recordId,
                                            const RecordData& record,
                                            size_t* dataSize) {
    BSONObj recordBson = record.toBson();

    const Status status = validateBSON(
        recordBson.objdata(), recordBson.objsize(), Validator<BSONObj>::enabledBSONVersion());
    if (status.isOK()) {
        *dataSize = recordBson.objsize();
    } else {
        return status;
    }

    if (!_indexCatalog->haveAnyIndexes()) {
        return status;
    }

    IndexCatalog::IndexIterator i = _indexCatalog->getIndexIterator(_opCtx, false);

    while (i.more()) {
        const IndexDescriptor* descriptor = i.next();
        const std::string indexNs = descriptor->indexNamespace();
        ValidateResults curRecordResults;

        const IndexAccessMethod* iam = _indexCatalog->getIndex(descriptor);

        if (descriptor->isPartial()) {
            const IndexCatalogEntry* ice = _indexCatalog->getEntry(descriptor);
            if (!ice->getFilterExpression()->matchesBSON(recordBson)) {
                (*_indexNsResultsMap)[indexNs] = curRecordResults;
                continue;
            }
        }

        BSONObjSet documentKeySet = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
        // There's no need to compute the prefixes of the indexed fields that cause the
        // index to be multikey when validating the index keys.
        MultikeyPaths* multikeyPaths = nullptr;
        iam->getKeys(recordBson,
                     IndexAccessMethod::GetKeysMode::kEnforceConstraints,
                     &documentKeySet,
                     multikeyPaths);

        if (!descriptor->isMultikey(_opCtx) && documentKeySet.size() > 1) {
            std::string msg = str::stream() << "Index " << descriptor->indexName()
                                            << " is not multi-key but has more than one"
                                            << " key in document " << recordId;
            curRecordResults.errors.push_back(msg);
            curRecordResults.valid = false;
        }

        uint32_t indexNsHash;
        const auto& pattern = descriptor->keyPattern();
        const Ordering ord = Ordering::make(pattern);
        MurmurHash3_x86_32(indexNs.c_str(), indexNs.size(), 0, &indexNsHash);

        for (const auto& key : documentKeySet) {
            if (key.objsize() >= IndexKeyMaxSize) {
                // Index keys >= 1024 bytes are not indexed.
                _longKeys[indexNs]++;
                continue;
            }

            // We want to use the latest version of KeyString here.
            KeyString ks(KeyString::kLatestVersion, key, ord, recordId);
            uint32_t indexEntryHash = hashIndexEntry(ks, indexNsHash);

            if ((*_ikc)[indexEntryHash] == 0) {
                _indexKeyCountTableNumEntries++;
            }
            (*_ikc)[indexEntryHash]++;
        }
        (*_indexNsResultsMap)[indexNs] = curRecordResults;
    }
    return status;
}

void RecordStoreValidateAdaptor::traverseIndex(const IndexAccessMethod* iam,
                                               const IndexDescriptor* descriptor,
                                               ValidateResults* results,
                                               int64_t* numTraversedKeys) {
    auto indexNs = descriptor->indexNamespace();
    int64_t numKeys = 0;

    uint32_t indexNsHash;
    MurmurHash3_x86_32(indexNs.c_str(), indexNs.size(), 0, &indexNsHash);

    const auto& key = descriptor->keyPattern();
    const Ordering ord = Ordering::make(key);
    KeyString::Version version = KeyString::kLatestVersion;
    std::unique_ptr<KeyString> prevIndexKeyString = nullptr;
    bool isFirstEntry = true;

    std::unique_ptr<SortedDataInterface::Cursor> cursor = iam->newCursor(_opCtx, true);
    // Seeking to BSONObj() is equivalent to seeking to the first entry of an index.
    for (auto indexEntry = cursor->seek(BSONObj(), true); indexEntry; indexEntry = cursor->next()) {

        // We want to use the latest version of KeyString here.
        std::unique_ptr<KeyString> indexKeyString =
            stdx::make_unique<KeyString>(version, indexEntry->key, ord, indexEntry->loc);
        // Ensure that the index entries are in increasing or decreasing order.
        if (!isFirstEntry && *indexKeyString < *prevIndexKeyString) {
            if (results->valid) {
                results->errors.push_back(
                    "one or more indexes are not in strictly ascending or descending "
                    "order");
            }
            results->valid = false;
        }

        // Cache the index keys to cross-validate with documents later.
        uint32_t keyHash = hashIndexEntry(*indexKeyString, indexNsHash);
        uint64_t& indexEntryCount = (*_ikc)[keyHash];
        if (indexEntryCount != 0) {
            indexEntryCount--;
            dassert(indexEntryCount >= 0);
            if (indexEntryCount == 0) {
                _indexKeyCountTableNumEntries--;
            }
        } else {
            _hasDocWithoutIndexEntry = true;
            results->valid = false;
        }
        numKeys++;

        isFirstEntry = false;
        prevIndexKeyString.swap(indexKeyString);
    }

    _keyCounts[indexNs] = numKeys;
    *numTraversedKeys = numKeys;
}

void RecordStoreValidateAdaptor::validateIndexKeyCount(IndexDescriptor* idx,
                                                       int64_t numRecs,
                                                       ValidateResults& results) {
    const std::string indexNs = idx->indexNamespace();
    int64_t numIndexedKeys = _keyCounts[indexNs];
    int64_t numLongKeys = _longKeys[indexNs];
    auto totalKeys = numLongKeys + numIndexedKeys;

    bool hasTooFewKeys = false;
    bool noErrorOnTooFewKeys = !failIndexKeyTooLong.load() && (_level != kValidateFull);

    if (idx->isIdIndex() && totalKeys != numRecs) {
        hasTooFewKeys = totalKeys < numRecs ? true : hasTooFewKeys;
        std::string msg = str::stream() << "number of _id index entries (" << numIndexedKeys
                                        << ") does not match the number of documents in the index ("
                                        << numRecs - numLongKeys << ")";
        if (noErrorOnTooFewKeys && (numIndexedKeys < numRecs)) {
            results.warnings.push_back(msg);
        } else {
            results.errors.push_back(msg);
            results.valid = false;
        }
    }

    if (results.valid && !idx->isMultikey(_opCtx) && totalKeys > numRecs) {
        std::string err = str::stream()
            << "index " << idx->indexName() << " is not multi-key, but has more entries ("
            << numIndexedKeys << ") than documents in the index (" << numRecs - numLongKeys << ")";
        results.errors.push_back(err);
        results.valid = false;
    }
    // Ignore any indexes with a special access method. If an access method name is given, the
    // index may be a full text, geo or special index plugin with different semantics.
    if (results.valid && !idx->isSparse() && !idx->isPartial() && !idx->isIdIndex() &&
        idx->getAccessMethodName() == "" && totalKeys < numRecs) {
        hasTooFewKeys = true;
        std::string msg = str::stream()
            << "index " << idx->indexName() << " is not sparse or partial, but has fewer entries ("
            << numIndexedKeys << ") than documents in the index (" << numRecs - numLongKeys << ")";
        if (noErrorOnTooFewKeys) {
            results.warnings.push_back(msg);
        } else {
            results.errors.push_back(msg);
            results.valid = false;
        }
    }

    if ((_level != kValidateFull) && hasTooFewKeys) {
        std::string warning = str::stream()
            << "index " << idx->indexName()
            << " has fewer keys than records. This may be the result of currently or "
               "previously running the server with the failIndexKeyTooLong parameter set to "
               "false. Please re-run the validate command with {full: true}";
        results.warnings.push_back(warning);
    }
}
}  // namespace
