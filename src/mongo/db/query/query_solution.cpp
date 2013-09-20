/**
 *    Copyright (C) 2013 10gen Inc.
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

#include "mongo/db/query/query_solution.h"

namespace mongo {

    //
    // CollectionScanNode
    //

    CollectionScanNode::CollectionScanNode() : tailable(false), direction(1), filter(NULL) { }

    void CollectionScanNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "COLLSCAN";
        addIndent(ss, indent + 1);
        *ss <<  "ns = " << name;
        if (NULL != filter) {
            addIndent(ss, indent + 1);
            *ss << " filter = " << filter->toString() << endl;
        }
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
    }

    //
    // AndHashNode
    //

    AndHashNode::AndHashNode() : filter(NULL) { }

    AndHashNode::~AndHashNode() {
        for (size_t i = 0; i < children.size(); ++i) {
            delete children[i];
        }
    }

    void AndHashNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "AND_HASH";
        if (NULL != filter) {
            addIndent(ss, indent + 1);
            *ss << " filter = " << filter->toString() << endl;
        }
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        for (size_t i = 0; i < children.size(); ++i) {
            *ss << "Child " << i << ": ";
            children[i]->appendToString(ss, indent + 1);
        }
    }

    bool AndHashNode::fetched() const {
        // Any WSM output from this stage came from all children stages.  If any child provides
        // fetched data, we merge that fetched data into the WSM we output.
        for (size_t i = 0; i < children.size(); ++i) {
            if (children[i]->fetched()) {
                return true;
            }
        }
        return false;
    }

    bool AndHashNode::hasField(const string& field) const {
        // Any WSM output from this stage came from all children stages.  Therefore we have all
        // fields covered in our children.
        for (size_t i = 0; i < children.size(); ++i) {
            if (children[i]->hasField(field)) {
                return true;
            }
        }
        return false;
    }

    //
    // OrNode
    //

    OrNode::OrNode() : dedup(true), filter(NULL) { }

    OrNode::~OrNode() {
        for (size_t i = 0; i < children.size(); ++i) {
            delete children[i];
        }
    }

    void OrNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "OR\n";
        if (NULL != filter) {
            addIndent(ss, indent + 1);
            *ss << " filter = " << filter->toString() << endl;
        }
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        for (size_t i = 0; i < children.size(); ++i) {
            addIndent(ss, indent + 1);
            *ss << "Child " << i << ":\n";
            children[i]->appendToString(ss, indent + 2);
            *ss << endl;
        }
    }

    bool OrNode::fetched() const {
        // Any WSM output from this stage came exactly one child stage.  Given that we don't know
        // what child stage it came from, we require that all children provide fetched data in order
        // to guarantee that our output is fetched.
        for (size_t i = 0; i < children.size(); ++i) {
            if (!children[i]->fetched()) {
                return false;
            }
        }
        return true;
    }

    /**
     * Any WSM output from this stage came from exactly one child stage.  Therefore, if
     * we want to guarantee that any output has a certain field, all of our children must
     * have that field.
     */
    bool OrNode::hasField(const string& field) const {
        for (size_t i = 0; i < children.size(); ++i) {
            if (!children[i]->hasField(field)) {
                return false;
            }
        }
        return true;
    }

    //
    // FetchNode
    //

    FetchNode::FetchNode() : filter(NULL) { }

    void FetchNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "FETCH\n";
        if (NULL != filter) {
            addIndent(ss, indent + 1);
            StringBuilder sb;
            *ss << "filter:\n";
            filter->debugString(sb, indent + 2);
            *ss << sb.str();
        }
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "Child:" << endl;
        child->appendToString(ss, indent + 2);
    }

    //
    // IndexScanNode
    //

    IndexScanNode::IndexScanNode() : filter(NULL), limit(0), direction(1) { }

    void IndexScanNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "IXSCAN\n";
        addIndent(ss, indent + 1);
        *ss << "keyPattern = " << indexKeyPattern << endl;
        if (NULL != filter) {
            addIndent(ss, indent + 1);
            *ss << " filter= " << filter->toString() << endl;
        }
        addIndent(ss, indent + 1);
        *ss << "dir = " << direction << endl;
        addIndent(ss, indent + 1);
        *ss << "bounds = " << bounds.toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
    }

    bool IndexScanNode::hasField(const string& field) const {
        // XXX XXX: multikey?  do we store that the index is multikey in the scan?
        BSONObjIterator it(indexKeyPattern);
        while (it.more()) {
            if (field == it.next().fieldName()) {
                return true;
            }
        }
        return false;
    }

    bool IndexScanNode::sortedByDiskLoc() const {
        // Indices use DiskLoc as an additional key after the actual index key.
        // Therefore, if we're only examining one index key, the output is sorted
        // by DiskLoc.

        // If it's a simple range query, it's easy to determine if the range is a point.
        if (bounds.isSimpleRange) {
            return 0 == bounds.startKey.woCompare(bounds.endKey, indexKeyPattern);
        }

        // If it's a more complex bounds query, we make sure that each field is a point.
        for (size_t i = 0; i < bounds.fields.size(); ++i) {
            const OrderedIntervalList& oil = bounds.fields[i];
            if (1 != oil.intervals.size()) {
                return false;
            }
            const Interval& interval = oil.intervals[0];
            if (0 != interval.start.woCompare(interval.end, false)) {
                return false;
            }
        }

        return true;
    }

    //
    // ProjectionNode
    //

    void ProjectionNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "PROJ\n";
        addIndent(ss, indent + 1);
        *ss << "proj = " << projection.toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "Child:" << endl;
        child->appendToString(ss, indent + 2);
    }

    //
    // SortNode
    //

    void SortNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "SORT\n";
        addIndent(ss, indent + 1);
        *ss << "pattern = " << pattern.toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "Child:" << endl;
        child->appendToString(ss, indent + 2);
    }

    //
    // LimitNode
    //
    

    void LimitNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "LIMIT\n";
        addIndent(ss, indent + 1);
        *ss << "limit = " << limit << endl;
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "Child:" << endl;
        child->appendToString(ss, indent + 2);
    }

    //
    // SkipNode
    //

    void SkipNode::appendToString(stringstream* ss, int indent) const {
        addIndent(ss, indent);
        *ss << "SKIP\n";
        addIndent(ss, indent + 1);
        *ss << "skip= " << skip << endl;
        addIndent(ss, indent + 1);
        *ss << "fetched = " << fetched() << endl;
        addIndent(ss, indent + 1);
        *ss << "sortedByDiskLoc = " << sortedByDiskLoc() << endl;
        addIndent(ss, indent + 1);
        *ss << "getSort = " << getSort().toString() << endl;
        addIndent(ss, indent + 1);
        *ss << "Child:" << endl;
        child->appendToString(ss, indent + 2);
    }

}  // namespace mongo
