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

#include "mongo/db/query/query_planner.h"

#include <map>
#include <set>
#include <stack>
#include <vector>

// For QueryOption_foobar
#include "mongo/client/dbclientinterface.h"
#include "mongo/db/matcher/expression_array.h"
#include "mongo/db/matcher/expression_parser.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/query/index_bounds_builder.h"
#include "mongo/db/query/index_tag.h"
#include "mongo/db/query/plan_enumerator.h"
#include "mongo/db/query/predicate_map.h"
#include "mongo/db/query/query_solution.h"

namespace mongo {

    /**
     * Scan the parse tree, adding all predicates to the provided map.
     */
    void makePredicateMap(MatchExpression* node, PredicateMap* out) {
        StringData path = node->path();

        // If we've seen this path before, link 'node' into that bunch.
        // Otherwise, create a new entry <path, node> in the predicate map.
        if (!path.empty()) {
            PredicateMap::iterator it = out->lower_bound(path.toString());
            if (it != out->end()) {
                vector<MatchExpression*>& nodes = it->second.nodes;
                nodes.push_back(node);
            }
            else {
                out->insert(std::make_pair(path.toString(), node));
            }
        }

        // XXX XXX XXX XXX
        // XXX Do we do this if the node is logical, or if it's array, or both?
        // XXX XXX XXX XXX
        for (size_t i = 0; i < node->numChildren(); ++i) {
            makePredicateMap(node->getChild(i), out);
        }
    }

    /**
     * Find all indices prefixed by fields we have predicates over.  Only these indices are useful
     * in answering the query.
     */
    void findRelevantIndices(const PredicateMap& pm, const vector<BSONObj>& allIndices,
                             vector<BSONObj>* out) {
        for (size_t i = 0; i < allIndices.size(); ++i) {
            BSONObjIterator it(allIndices[i]);
            verify(it.more());
            BSONElement elt = it.next();
            if (pm.end() != pm.find(elt.fieldName())) {
                out->push_back(allIndices[i]);
            }
        }
    }

    /**
     * Given the set of relevant indices, annotate predicates with any applicable indices.  Also
     * mark how applicable the indices are (see RelevantIndex::Relevance).
     */
    void rateIndices(const vector<BSONObj>& indices, PredicateMap* predicates) {
        for (size_t i = 0; i < indices.size(); ++i) {
            BSONObjIterator kpIt(indices[i]);
            BSONElement elt = kpIt.next();

            // We're looking at the first element in the index.  We can definitely use any index
            // prefixed by the predicate's field to answer that predicate.
            PredicateMap::iterator it = predicates->find(elt.fieldName());
            if (it != predicates->end()) {
                it->second.relevant.insert(RelevantIndex(i, RelevantIndex::FIRST));
            }

            // We're now looking at the subsequent elements of the index.  We can only use these if
            // we have a restriction of all the previous fields.  We won't figure that out until
            // later.
            while (kpIt.more()) {
                elt = kpIt.next();
                PredicateMap::iterator it = predicates->find(elt.fieldName());
                if (it != predicates->end()) {
                    it->second.relevant.insert(RelevantIndex(i, RelevantIndex::NOT_FIRST));
                }
            }
        }
    }

    bool hasPredicate(const PredicateMap& pm, MatchExpression::MatchType type) {
        for (PredicateMap::const_iterator itMap = pm.begin(); itMap != pm.end(); ++itMap) {
            const vector<MatchExpression*>& nodes = itMap->second.nodes;
            for (vector<MatchExpression*>::const_iterator itVec = nodes.begin();
                 itVec != nodes.end();
                 ++itVec) {
                if ((*itVec)->matchType() == type) {
                    return true;
                }
            }
        }
        return false;
    }

    QuerySolution* makeCollectionScan(const CanonicalQuery& query, bool tailable) {
        auto_ptr<QuerySolution> soln(new QuerySolution());
        soln->filter.reset(query.root()->shallowClone());
        // BSONValue, where are you?
        soln->filterData = query.getQueryObj();
        verify(soln->filterData.isOwned());
        soln->ns = query.ns();

        // Make the (only) node, a collection scan.
        CollectionScanNode* csn = new CollectionScanNode();
        csn->name = query.ns();
        csn->filter = soln->filter.get();
        csn->tailable = tailable;

        const BSONObj& sortObj = query.getParsed().getSort();

        QuerySolutionNode* solnRoot = csn;

        // XXX: call analyzeDataAccess to do the work below once $natural is dealt with better.

        // TODO: We need better checking for this.  Should be done in CanonicalQuery and we should
        // be able to assume it's correct.
        if (!sortObj.isEmpty()) {
            BSONElement natural = sortObj.getFieldDotted("$natural");
            if (!natural.eoo()) {
                csn->direction = natural.numberInt() >= 0 ? 1 : -1;
            }
            else {
                soln->hasSortStage = true;
                SortNode* sort = new SortNode();
                sort->pattern = sortObj;
                sort->child.reset(csn);
                solnRoot = sort;
            }
        }

        if (NULL != query.getProj()) {
            ProjectionNode* proj = new ProjectionNode();
            proj->projection = query.getProj();
            proj->child.reset(solnRoot);
            solnRoot = proj;
        }

        if (0 != query.getParsed().getSkip()) {
            SkipNode* skip = new SkipNode();
            skip->skip = query.getParsed().getSkip();
            skip->child.reset(solnRoot);
            solnRoot = skip;
        }

        soln->root.reset(solnRoot);
        // cout << "Outputting collscan " << soln->toString() << endl;
        return soln.release();
    }

    // TODO: Document when this settles
    struct FilterInfo {
        MatchExpression* filterNode;
        size_t currChild;
        FilterInfo(MatchExpression* f, int c) : filterNode(f), currChild(c) {}
    };

    IndexScanNode* makeIndexScan(const BSONObj& indexKeyPattern, MatchExpression* expr,
                                 bool* exact) {

        IndexScanNode* isn = new IndexScanNode();
        isn->indexKeyPattern = indexKeyPattern;

        if (indexKeyPattern.firstElement().fieldName() != expr->path().toString()) {
            cout << "trying to build index w/bad prefix, indexkp fn is "
                 << indexKeyPattern.firstElement().fieldName()
                 << " path is " << expr->path().toString() << endl;
            verify(0);
        }

        BSONObjIterator it(isn->indexKeyPattern);
        BSONElement elt = it.next();

        OrderedIntervalList oil(expr->path().toString());
        int direction = (elt.numberInt() >= 0) ? 1 : -1;
        IndexBoundsBuilder::translate(expr, direction, &oil, exact);
        // TODO(opt): this is a surplus copy, could build right in the original
        isn->bounds.fields.push_back(oil);

        // Pad the remaining fields if it's a compound index.
        while (it.more()) {
            isn->bounds.fields.push_back(IndexBoundsBuilder::allValuesForField(it.next()));
        }

        return isn;
    }

    QuerySolutionNode* buildIndexedDataAccess(MatchExpression* root,
                                              const vector<BSONObj>& indexKeyPatterns) {
        if (root->isLogical()) {
            // The children of AND and OR nodes are sorted by the index that the subtree rooted at
            // that node uses.  Child nodes that use the same index are adjacent to one another to
            // facilitate grouping of index scans.
            //
            // See tagForSort and sortUsingTags in index_tag.h
            if (MatchExpression::AND == root->matchType()) {
                // XXX: If all children are sortedByDiskLoc() this should be AndSortedNode.
                auto_ptr<AndHashNode> theAnd(new AndHashNode());

                // Process all IXSCANs, possibly combining them.
                auto_ptr<IndexScanNode> currentScan;
                size_t currentIndexNumber = IndexTag::kNoIndex;
                size_t curChild = 0;
                while (curChild < root->numChildren()) {
                    MatchExpression* child = root->getChild(curChild);

                    // No tag, it's not an IXSCAN.  We've sorted our children such that the tagged
                    // children are first, so we stop now.
                    if (NULL == child->getTag()) { break; }

                    IndexTag* ixtag = static_cast<IndexTag*>(child->getTag());
                    // If there's a tag it must be valid.
                    verify(IndexTag::kNoIndex != ixtag->index);

                    // TODO(opt): If the child is logical, it could collapse into an ixscan.  We
                    // ignore this for now.
                    if (child->isLogical()) {
                        QuerySolutionNode* childSolution = buildIndexedDataAccess(child,
                                                                                  indexKeyPatterns);
                        if (NULL == childSolution) { return NULL; }
                        theAnd->children.push_back(childSolution);
                        // The logical sub-tree is responsible for fully evaluating itself.  Any
                        // required filters or fetches are already hung on it.  As such, we remove
                        // the filter branch from our tree.
                        // XXX: Verify this is the right policy.
                        root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                        // XXX: Do we delete the curChild-th child??
                        continue;
                    }

                    // We now know that 'child' can use an index and that it's a predicate over a
                    // field (leaf).  There is no current index scan, make the current scan this
                    // child.
                    if (NULL == currentScan.get()) {
                        verify(IndexTag::kNoIndex == currentIndexNumber);
                        currentIndexNumber = ixtag->index;

                        bool exact = false;
                        currentScan.reset(makeIndexScan(indexKeyPatterns[currentIndexNumber], child, &exact));

                        if (exact) {
                            // The bounds answer the predicate, and we can remove the expression from the root.
                            // TODO(opt): Erasing entry 0, 1, 2, ... could be kind of n^2, maybe optimize later.
                            root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                            // Don't increment curChild.
                            // XXX: Do we delete the curChild-th child??
                        }
                        else {
                            // We keep curChild in the AND for affixing later.
                            ++curChild;
                        }
                        continue;
                    }

                    // If the child uses a different index than the current index scan, and there is
                    // a valid current index scan, add the current index scan as a child to the AND,
                    // as we're done with it.
                    //
                    // The child then becomes our new current index scan.

                    // XXX temporary until we can merge bounds.
                    bool childUsesNewIndex = true;

                    // XXX: uncomment when we can combine ixscans via bounds merging.
                    // bool childUsesNewIndex = (currentIndexNumber != ixtag->index);

                    if (childUsesNewIndex) {
                        verify(NULL != currentScan.get());
                        theAnd->children.push_back(currentScan.release());
                        currentIndexNumber = ixtag->index;

                        bool exact = false;
                        currentScan.reset(makeIndexScan(indexKeyPatterns[currentIndexNumber], child, &exact));

                        if (exact) {
                            // The bounds answer the predicate.
                            // TODO(opt): Erasing entry 0, 1, 2, ... could be kind of n^2, maybe optimize later.
                            root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                            // XXX: Do we delete the curChild-th child??
                        }
                        else {
                            // We keep curChild in the AND for affixing later.
                            ++curChild;
                        }
                        continue;
                    }
                    else {
                        // The child uses the same index we're currently building a scan for.  Merge the
                        // bounds and filters.
                        verify(currentIndexNumber == ixtag->index);

                        // First, make the bounds.
                        OrderedIntervalList oil(child->path().toString());

                        // TODO(opt): We can cache this as part of the index rating process
                        BSONObjIterator kpIt(indexKeyPatterns[currentIndexNumber]);
                        BSONElement elt = kpIt.next();
                        while (elt.fieldName() != oil.name) {
                            verify(kpIt.more());
                            elt = kpIt.next();
                        }
                        verify(!elt.eoo());
                        int direction = (elt.numberInt() >= 0) ? 1 : -1;
                        bool exact = false;
                        IndexBoundsBuilder::translate(child, direction, &oil, &exact);

                        // Merge the bounds with the existing.
                        currentScan->bounds.joinAnd(oil, indexKeyPatterns[currentIndexNumber]);

                        if (exact) {
                            // The bounds answer the predicate.
                            // TODO(opt): Erasing entry 0, 1, 2, ... could be kind of n^2, maybe optimize later.
                            root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                            // XXX: Do we delete the curChild-th child??
                        }
                        else {
                            // We keep curChild in the AND for affixing later.
                            ++curChild;
                        }
                    }
                }

                // Output the scan we're done with.
                if (NULL != currentScan.get()) {
                    theAnd->children.push_back(currentScan.release());
                }

                //
                // Process all non-indexed predicates.  We hang these above the AND with a fetch and
                // filter.
                //

                // This is the node we're about to return.
                QuerySolutionNode* andResult;

                // We must use an index for at least one child of the AND.
                verify(theAnd->children.size() >= 1);

                // Short-circuit: an AND of one child is just the child.
                if (theAnd->children.size() == 1) {
                    andResult = theAnd->children[0];
                    theAnd->children.clear();
                    // Deletes theAnd but we cleared the children.
                    theAnd.reset();
                }
                else {
                    andResult = theAnd.release();
                }

                // If there are any nodes still attached to the AND, we can't answer them using the
                // index, so we put a fetch with filter.
                if (root->numChildren() > 0) {
                    FetchNode* fetch = new FetchNode();
                    fetch->filter = root;
                    // takes ownership
                    fetch->child.reset(andResult);
                    andResult = fetch;
                }
                else {
                    // XXX: If root has no children, who deletes it/owns it?  What happens?
                }

                return andResult;
            }
            else if (MatchExpression::OR == root->matchType()) {
                // XXX: If all children have the same getSort() this should be MergeSortNode
                auto_ptr<OrNode> theOr(new OrNode());

                // Process all IXSCANs, possibly combining them.
                auto_ptr<IndexScanNode> currentScan;
                size_t currentIndexNumber = IndexTag::kNoIndex;
                size_t curChild = 0;
                while (curChild < root->numChildren()) {
                    MatchExpression* child = root->getChild(curChild);

                    // No tag, it's not an IXSCAN.
                    if (NULL == child->getTag()) { break; }

                    IndexTag* ixtag = static_cast<IndexTag*>(child->getTag());
                    // If there's a tag it must be valid.
                    verify(IndexTag::kNoIndex != ixtag->index);

                    // TODO(opt): If the child is logical, it could collapse into an ixscan.  We
                    // ignore this for now.
                    if (child->isLogical()) {
                        QuerySolutionNode* childSolution = buildIndexedDataAccess(child,
                                                                                  indexKeyPatterns);
                        if (NULL == childSolution) { return NULL; }
                        theOr->children.push_back(childSolution);
                        // The logical sub-tree is responsible for fully evaluating itself.  Any
                        // required filters or fetches are already hung on it.  As such, we remove
                        // the filter branch from our tree.
                        // XXX: Verify this is the right policy.
                        // XXX: Do we delete the curChild-th child??
                        root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                        continue;
                    }

                    // We now know that 'child' can use an index and that it's a predicate over a
                    // field.  There is no current index scan, make the current scan this child.
                    if (NULL == currentScan.get()) {
                        verify(IndexTag::kNoIndex == currentIndexNumber);
                        currentIndexNumber = ixtag->index;

                        bool exact = false;
                        currentScan.reset(makeIndexScan(indexKeyPatterns[currentIndexNumber], child, &exact));

                        if (exact) {
                            // The bounds answer the predicate, and we can remove the expression from the root.
                            // TODO(opt): Erasing entry 0, 1, 2, ... could be kind of n^2, maybe optimize later.
                            root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                            // Don't increment curChild.
                            // XXX: Do we delete the curChild-th child??
                        }
                        else {
                            // We keep curChild in the OR for affixing later as a filter.
                            ++curChild;
                        }
                        continue;
                    }

                    // If the child uses a different index than the current index scan, and there is
                    // a valid current index scan, add the current index scan as a child to the AND,
                    // as we're done with it.
                    //
                    // The child then becomes our new current index scan.

                    // XXX temporary until we can merge bounds.
                    bool childUsesNewIndex = true;

                    // XXX: uncomment when we can combine ixscans via bounds merging.
                    // bool childUsesNewIndex = (currentIndexNumber != ixtag->index);

                    if (childUsesNewIndex) {
                        verify(NULL != currentScan.get());
                        theOr->children.push_back(currentScan.release());
                        currentIndexNumber = ixtag->index;

                        bool exact = false;
                        currentScan.reset(makeIndexScan(indexKeyPatterns[currentIndexNumber], child, &exact));

                        if (exact) {
                            // The bounds answer the predicate.
                            // TODO(opt): Erasing entry 0, 1, 2, ... could be kind of n^2, maybe optimize later.
                            root->getChildVector()->erase(root->getChildVector()->begin() + curChild);
                            // XXX: Do we delete the curChild-th child??
                        }
                        else {
                            // We keep curChild in the AND for affixing later.
                            ++curChild;
                        }
                        continue;
                    }
                    else {
                        // The child uses the same index we're currently building a scan for.  Merge the
                        // bounds and filters.
                        verify(currentIndexNumber == ixtag->index);

                        // First, make the bounds.
                        OrderedIntervalList oil(child->path().toString());

                        // TODO(opt): We can cache this as part of the index rating process
                        BSONObjIterator kpIt(indexKeyPatterns[currentIndexNumber]);
                        BSONElement elt = kpIt.next();
                        while (elt.fieldName() != oil.name) {
                            verify(kpIt.more());
                            elt = kpIt.next();
                        }
                        verify(!elt.eoo());
                        int direction = (elt.numberInt() >= 0) ? 1 : -1;
                        bool exact = false;
                        IndexBoundsBuilder::translate(child, direction, &oil, &exact);

                        // Merge the bounds with the existing.
                        currentScan->bounds.joinOr(oil, indexKeyPatterns[currentIndexNumber]);

                        if (exact) {
                            // The bounds answer the predicate.
                            // TODO(opt): Erasing entry 0, 1, 2, ... could be kind of n^2, maybe optimize later.
                            root->getChildVector()->erase(root->getChildVector()->begin() + curChild);

                            // XXX: Do we delete the curChild-th child??
                        }
                        else {
                            // We keep curChild in the OR for affixing later.
                            ++curChild;
                        }
                    }
                }

                // Output the scan we're done with.
                if (NULL != currentScan.get()) {
                    theOr->children.push_back(currentScan.release());
                }

                // Unlike an AND, an OR cannot have filters hanging off of it.  We stop processing
                // when any of our children lack index tags.  If a node lacks an index tag it cannot
                // be answered via an index.
                if (curChild != root->numChildren()) {
                    warning() << "planner OR error, non-indexed branch.";
                    // XXX: don't verify in prod environment but good for debugging now.
                    verify(0);
                    return NULL;
                }

                // If there are any nodes still attached to the OR, we can't answer them using the
                // index, so we put a fetch with filter.
                if (root->numChildren() > 0) {
                    FetchNode* fetch = new FetchNode();
                    fetch->filter = root;
                    // takes ownership
                    fetch->child.reset(theOr.release());
                    return fetch;
                }
                else {
                    return theOr.release();
                }
            }
            else {
                // NOT or NOR, can't do anything.
                return NULL;
            }
        }
        else {
            // isArray or isLeaf is true.  Either way, it's over one field, and the bounds builder
            // deals with it.
            if (NULL == root->getTag()) {
                // No index to use here, not in the context of logical operator, so we're SOL.
                return NULL;
            }
            else {
                // Make an index scan over the tagged index #.
                IndexTag* tag = static_cast<IndexTag*>(root->getTag());

                bool exact = false;
                auto_ptr<IndexScanNode> isn(makeIndexScan(indexKeyPatterns[tag->index], root, &exact));

                // If the bounds are exact, the set of documents that satisfy the predicate is exactly
                // equal to the set of documents that the scan provides.
                //
                // If the bounds are not exact, the set of documents returned from the scan is a
                // superset of documents that satisfy the predicate, and we must check the
                // predicate.
                if (!exact) {
                    FetchNode* fetch = new FetchNode();
                    fetch->filter = root;
                    fetch->child.reset(isn.release());
                    return fetch;
                }

                return isn.release();
            }
        }
        return NULL;
    }

    QuerySolution* analyzeDataAccess(const CanonicalQuery& query, MatchExpression* taggedRoot,
                                     QuerySolutionNode* solnRoot) {
        auto_ptr<QuerySolution> soln(new QuerySolution());
        soln->filter.reset(taggedRoot);
        soln->filterData = query.getQueryObj();
        verify(soln->filterData.isOwned());
        soln->ns = query.ns();

        // solnRoot finds all our results.

        // Sort the results.
        if (!query.getParsed().getSort().isEmpty()) {
            // See if solnRoot gives us the sort.  If so, we're done.
            if (0 == query.getParsed().getSort().woCompare(solnRoot->getSort())) {
                // Sort is already provided!
            }
            else {
                // If solnRoot isn't already sorted, let's see if it has the fields we're sorting
                // on.  If it's fetched, it has all the fields by definition.  If it's not, we check
                // sort field by sort field.
                if (!solnRoot->fetched()) {
                    bool sortCovered = true;
                    BSONObjIterator it(query.getParsed().getSort());
                    while (it.more()) {
                        if (!solnRoot->hasField(it.next().fieldName())) {
                            sortCovered = false;
                            break;
                        }
                    }

                    if (!sortCovered) {
                        FetchNode* fetch = new FetchNode();
                        fetch->child.reset(solnRoot);
                        solnRoot = fetch;
                    }
                }

                soln->hasSortStage = true;
                SortNode* sort = new SortNode();
                sort->pattern = query.getParsed().getSort();
                sort->child.reset(solnRoot);
                solnRoot = sort;
            }
        }

        // Project the results.
        if (NULL != query.getProj()) {
            if (query.getProj()->requiresDocument()) {
                if (!solnRoot->fetched()) {
                    FetchNode* fetch = new FetchNode();
                    fetch->child.reset(solnRoot);
                    solnRoot = fetch;
                }
            }
            else {
                const vector<string>& fields = query.getProj()->requiredFields();
                bool covered = true;
                for (size_t i = 0; i < fields.size(); ++i) {
                    if (!solnRoot->hasField(fields[i])) {
                        covered = false;
                        break;
                    }
                }
                if (!covered) {
                    FetchNode* fetch = new FetchNode();
                    fetch->child.reset(solnRoot);
                    solnRoot = fetch;
                }
            }

            // We now know we have whatever data is required for the projection.
            ProjectionNode* projNode = new ProjectionNode();
            projNode->projection = query.getProj();
            projNode->child.reset(solnRoot);
            solnRoot = projNode;
        }
        else {
            if (!solnRoot->fetched()) {
                FetchNode* fetch = new FetchNode();
                fetch->child.reset(solnRoot);
                solnRoot = fetch;
            }
        }

        if (0 != query.getParsed().getSkip()) {
            SkipNode* skip = new SkipNode();
            skip->skip = query.getParsed().getSkip();
            skip->child.reset(solnRoot);
            solnRoot = skip;
        }
        soln->root.reset(solnRoot);
        return soln.release();
    }

    void dumpPredMap(const PredicateMap& predicates) {
        for (PredicateMap::const_iterator it = predicates.begin(); it != predicates.end(); ++it) {
            cout << "field " << it->first << endl;
            const PredicateInfo& pi = it->second;
            cout << "\tRelevant indices:\n";
            for (set<RelevantIndex>::iterator si = pi.relevant.begin(); si != pi.relevant.end(); ++si) {
                cout << "\t\tidx #" << si->index << " relevance: ";
                if (RelevantIndex::FIRST == si->relevance) {
                    cout << "first";
                }
                else {
                    cout << "second";
                }
            }
            cout << "\n\tNodes:\n";
            for (size_t i = 0; i < pi.nodes.size(); ++i) {
                cout << "\t\t" << pi.nodes[i]->toString();
            }
        }
    }

    bool hasNode(MatchExpression* root, MatchExpression::MatchType type) {
        if (type == root->matchType()) {
            return true;
        }
        for (size_t i = 0; i < root->numChildren(); ++i) {
            if (hasNode(root->getChild(i), type)) {
                return true;
            }
        }
        return false;
    }

    // Copied from db/index.h
    static bool isIdIndex( const BSONObj &pattern ) {
        BSONObjIterator i(pattern);
        BSONElement e = i.next();
        //_id index must have form exactly {_id : 1} or {_id : -1}.
        //Allows an index of form {_id : "hashed"} to exist but
        //do not consider it to be the primary _id index
        if(! ( strcmp(e.fieldName(), "_id") == 0
                && (e.numberInt() == 1 || e.numberInt() == -1)))
            return false;
        return i.next().eoo();
    }

    // static
    void QueryPlanner::plan(const CanonicalQuery& query, const vector<BSONObj>& indexKeyPatterns,
                            vector<QuerySolution*>* out) {
        // XXX: If pq.hasOption(QueryOption_OplogReplay) use FindingStartCursor equivalent which
        // must be translated into stages.

        //
        // Planner Section 1: Calculate predicate/index data.
        //

        // Get all the predicates (and their fields).
        PredicateMap predicates;
        makePredicateMap(query.root(), &predicates);

        // If the query requests a tailable cursor, the only solution is a collscan + filter with
        // tailable set on the collscan.  TODO: This is a policy departure.  Previously I think you
        // could ask for a tailable cursor and it just tried to give you one.  Now, we fail if we
        // can't provide one.  Is this what we want?
        if (query.getParsed().hasOption(QueryOption_CursorTailable)) {
            if (!hasPredicate(predicates, MatchExpression::GEO_NEAR)) {
                out->push_back(makeCollectionScan(query, true));
            }
            return;
        }

        // NOR and NOT we can't handle well with indices.  If we see them here, they weren't
        // rewritten.  Just output a collscan for those.
        if (hasNode(query.root(), MatchExpression::NOT)
            || hasNode(query.root(), MatchExpression::NOR)) {

            // If there's a near predicate, we can't handle this.
            // TODO: Should canonicalized query detect this?
            if (hasPredicate(predicates, MatchExpression::GEO_NEAR)) {
                warning() << "Can't handle NOT/NOR with GEO_NEAR";
                return;
            }
            out->push_back(makeCollectionScan(query, false));
            return;
        }

        // Filter our indices so we only look at indices that are over our predicates.
        vector<BSONObj> relevantIndices;

        // Hints require us to only consider the hinted index.
        BSONObj hintIndex = query.getParsed().getHint();

        // Snapshot is a form of a hint.
        if (query.getParsed().isSnapshot()) {
            // Snapshot is equivalent to:
            // 1. Try to use _id index to make a real plan.  If that fails, just scan
            // the _id index.
            // Find the ID index in indexKeyPatterns.
            for (size_t i = 0; i < indexKeyPatterns.size(); ++i) {
                if (isIdIndex(indexKeyPatterns[i])) {
                    hintIndex = indexKeyPatterns[i];
                    break;
                }
            }
        }

        if (!hintIndex.isEmpty()) {
            relevantIndices.push_back(hintIndex);
            cout << "hint specified, restricting indices to " << hintIndex.toString() << endl;
        }
        else {
            findRelevantIndices(predicates, indexKeyPatterns, &relevantIndices);
        }

        if (0 < relevantIndices.size()) {
            for (size_t i = 0; i < relevantIndices.size(); ++i) {
                cout << "relevant idx " << i << " is " << relevantIndices[i].toString() << endl;
            }

            // Figure out how useful each index is to each predicate.
            rateIndices(relevantIndices, &predicates);
            dumpPredMap(predicates);

            //
            // Planner Section 2: Use predicate/index data to output sets of indices that we can
            // use.
            //

            PlanEnumerator isp(query.root(), &predicates, &relevantIndices);
            isp.init();

            MatchExpression* rawTree;
            while (isp.getNext(&rawTree)) {
                cout << "about to build solntree from tagged tree:\n" << rawTree->toString()
                     << endl;

                QuerySolutionNode* solnRoot = buildIndexedDataAccess(rawTree, relevantIndices);
                if (NULL == solnRoot) { continue; }
                QuerySolution* soln = analyzeDataAccess(query, rawTree, solnRoot);
                verify(NULL != soln);

                cout << "Adding solution:\n" << soln->toString() << endl;
                out->push_back(soln);
            }
        }

        // An index was hinted.  If there are any solutions, they use the hinted index.  If not,
        // we scan the entire index to provide results and output that as our plan.
        if (!hintIndex.isEmpty() && (0 == out->size())) {
            // Build an ixscan over the id index, use it, and return it.
            IndexScanNode* isn = new IndexScanNode();
            isn->indexKeyPattern = hintIndex;

            // XXX: use simple bounds for ixscan once builder supports them
            BSONObjIterator it(isn->indexKeyPattern);
            while (it.more()) {
                isn->bounds.fields.push_back(
                        IndexBoundsBuilder::allValuesForField(it.next()));
            }

            QuerySolution* soln = analyzeDataAccess(query, query.root()->shallowClone(),
                    isn);
            verify(NULL != soln);
            out->push_back(soln);
            cout << "using hinted index as scan, soln = " << soln->toString() << endl;
            return;
        }

        if (!query.getParsed().getSort().isEmpty()) {
            // See if we have a sort provided from an index already.
            bool usingIndexToSort = false;
            for (size_t i = 0; i < out->size(); ++i) {
                QuerySolution* soln = (*out)[i];
                if (!soln->hasSortStage) {
                    usingIndexToSort = true;
                    break;
                }
            }

            if (!usingIndexToSort) {
                for (size_t i = 0; i < indexKeyPatterns.size(); ++i) {
                    const BSONObj& kp = indexKeyPatterns[i];
                    if (0 == kp.woCompare(query.getParsed().getSort())) {
                        IndexScanNode* isn = new IndexScanNode();
                        isn->indexKeyPattern = kp;

                        // XXX: use simple bounds for ixscan once builder supports them
                        BSONObjIterator it(isn->indexKeyPattern);
                        while (it.more()) {
                            isn->bounds.fields.push_back(
                                    IndexBoundsBuilder::allValuesForField(it.next()));
                        }

                        QuerySolution* soln = analyzeDataAccess(query, query.root()->shallowClone(),
                                isn);
                        verify(NULL != soln);
                        out->push_back(soln);
                        cout << "using index to provide sort, soln = " << soln->toString() << endl;
                        break;
                    }
                }
            }
        }

        // TODO: Do we always want to offer a collscan solution?
        if (!hasPredicate(predicates, MatchExpression::GEO_NEAR)) {
            QuerySolution* collscan = makeCollectionScan(query, false);
            out->push_back(collscan);
        }
    }

}  // namespace mongo
