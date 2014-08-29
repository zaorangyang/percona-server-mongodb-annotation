// shardkey.cpp

/**
*    Copyright (C) 2008 10gen Inc.
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
*    must comply with the GNU Affero General Public License in all respects
*    for all of the code used other than as permitted herein. If you modify
*    file(s) with this exception, you may extend this exception to your
*    version of the file(s), but you are not obligated to do so. If you do not
*    wish to do so, delete this exception statement from your version. If you
*    delete this exception statement from all source files in the program,
*    then also delete it in the license file.
*/

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/chunk.h"
#include "mongo/s/shard_key_pattern.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/json.h"
#include "mongo/util/log.h"
#include "mongo/util/startup_test.h"
#include "mongo/util/timer.h"

namespace mongo {

    ShardKeyPattern::ShardKeyPattern( BSONObj p ) : pattern( p.getOwned() ) {
        pattern.toBSON().getFieldNames( patternfields );

        BSONObjBuilder min;
        BSONObjBuilder max;

        BSONObjIterator it(p);
        while (it.more()) {
            BSONElement e (it.next());
            min.appendMinKey(e.fieldName());
            max.appendMaxKey(e.fieldName());
        }

        gMin = min.obj();
        gMax = max.obj();
    }

    static bool _hasShardKey(const BSONObj& doc,
                             const set<string>& patternFields,
                             bool allowRegex) {

        // this is written s.t. if doc has lots of fields, if the shard key fields are early,
        // it is fast.  so a bit more work to try to be semi-fast.

        for (set<string>::const_iterator it = patternFields.begin(); it != patternFields.end();
            ++it) {
            BSONElement shardKeyField = doc.getFieldDotted(it->c_str());
            if (shardKeyField.eoo()
                || shardKeyField.type() == Array
                || (!allowRegex && shardKeyField.type() == RegEx)
                || (shardKeyField.type() == Object &&
                    !shardKeyField.embeddedObject().okForStorage())) {
                // Don't allow anything for a shard key we can't store -- like $gt/$lt ops
                return false;
            }
        }
        return true;
    }

    bool ShardKeyPattern::hasShardKey(const BSONObj& doc) const {
        return _hasShardKey(doc, patternfields, true);
    }

    bool ShardKeyPattern::hasTargetableShardKey(const BSONObj& doc) const {
        return _hasShardKey(doc, patternfields, false);
    }

    bool ShardKeyPattern::isUniqueIndexCompatible( const KeyPattern& uniqueIndexPattern ) const {
        return mongo::isUniqueIndexCompatible( pattern.toBSON(), uniqueIndexPattern.toBSON() );
    }

    string ShardKeyPattern::toString() const {
        return pattern.toString();
    }

    /* things to test for compound :
       \ middle (deprecating?)
    */
    class ShardKeyUnitTest : public StartupTest {
    public:

        void hasshardkeytest() {
            ShardKeyPattern k( BSON( "num" << 1 ) );

            BSONObj x = fromjson("{ zid : \"abcdefg\", num: 1.0, name: \"eliot\" }");
            verify( k.hasShardKey(x) );
            verify( !k.hasShardKey( fromjson("{foo:'a'}") ) );
            verify( !k.hasShardKey( fromjson("{x: {$gt: 1}}") ) );
            verify( !k.hasShardKey( fromjson("{num: {$gt: 1}}") ) );
            BSONObj obj = BSON( "num" << BSON( "$ref" << "coll" << "$id" << 1));
            verify( k.hasShardKey(obj));

            // try compound key
            {
                ShardKeyPattern k( fromjson("{a:1,b:-1,c:1}") );
                verify( k.hasShardKey( fromjson("{foo:'a',a:'b',c:'z',b:9,k:99}") ) );
                BSONObj obj = BSON( "foo" << "a" <<
                                    "a" << BSON("$ref" << "coll" << "$id" << 1) <<
                                    "c" << 1 << "b" << 9 << "k" << 99 );
                verify( k.hasShardKey(  obj ) );
                verify( !k.hasShardKey( fromjson("{foo:'a',a:[1,2],c:'z',b:9,k:99}") ) );
                verify( !k.hasShardKey( fromjson("{foo:'a',a:{$gt:1},c:'z',b:9,k:99}") ) );
                verify( !k.hasShardKey( fromjson("{foo:'a',a:'b',c:'z',bb:9,k:99}") ) );
                verify( !k.hasShardKey( fromjson("{k:99}") ) );
            }

            // try dotted key
            {
                ShardKeyPattern k( fromjson("{'a.b':1}") );
                verify( k.hasShardKey( fromjson("{a:{b:1,c:1},d:1}") ) );
                verify( k.hasShardKey( fromjson("{'a.b':1}") ) );
                BSONObj obj = BSON( "c" << "a" <<
                                    "a" << BSON("$ref" << "coll" << "$id" << 1) );
                verify( !k.hasShardKey(  obj ) );
                obj = BSON( "c" << "a" <<
                            "a" << BSON( "b" << BSON("$ref" << "coll" << "$id" << 1) <<
                                         "c" << 1));
                verify( k.hasShardKey(  obj ) );
                verify( !k.hasShardKey( fromjson("{'a.c':1}") ) );
                verify( !k.hasShardKey( fromjson("{'a':[{b:1}, {c:1}]}") ) );
                verify( !k.hasShardKey( fromjson("{a:{b:[1,2]},d:1}") ) );
                verify( !k.hasShardKey( fromjson("{a:{c:1},d:1}") ) );
                verify( !k.hasShardKey( fromjson("{a:1}") ) );
                verify( !k.hasShardKey( fromjson("{b:1}") ) );
            }

        }

        void extractkeytest() {
            ShardKeyPattern k( fromjson("{a:1,'sub.b':-1,'sub.c':1}") );

            BSONObj x = fromjson("{a:1,'sub.b':2,'sub.c':3}");
            verify( k.extractKeyFromQueryOrDoc( fromjson("{a:1,sub:{b:2,c:3}}") ).binaryEqual(x) );
            verify( k.extractKeyFromQueryOrDoc( fromjson("{sub:{b:2,c:3},a:1}") ).binaryEqual(x) );
        }

        void uniqueIndexCompatibleTest() {
            ShardKeyPattern k1( BSON( "a" << 1 ) );
            verify( k1.isUniqueIndexCompatible( BSON( "_id" << 1 ) ) );
            verify( k1.isUniqueIndexCompatible( BSON( "a" << 1 << "b" << 1 ) ) );
            verify( k1.isUniqueIndexCompatible( BSON( "a" << -1 ) ) );
            verify( ! k1.isUniqueIndexCompatible( BSON( "b" << 1 ) ) );

            ShardKeyPattern k2( BSON( "a" <<  "hashed") );
            verify( k2.isUniqueIndexCompatible( BSON( "a" << 1 ) ) );
            verify( ! k2.isUniqueIndexCompatible( BSON( "b" << 1 ) ) );
        }

        void run() {
            extractkeytest();

            ShardKeyPattern k( BSON( "key" << 1 ) );

            BSONObj min = k.globalMin();

//            cout << min.jsonString(TenGen) << endl;

            BSONObj max = k.globalMax();

            BSONObj k1 = BSON( "key" << 5 );

            verify( min < max );
            verify( min < k.extractKeyFromQueryOrDoc( k1 ) );
            verify( max > min );

            hasshardkeytest();
            verify( k.hasShardKey( k1 ) );
            verify( ! k.hasShardKey( BSON( "key2" << 1 ) ) );

            BSONObj a = k1;
            BSONObj b = BSON( "key" << 999 );

            verify( k.extractKeyFromQueryOrDoc( a ) <  k.extractKeyFromQueryOrDoc( b ) );

            // add middle multitype tests

            uniqueIndexCompatibleTest();

            LOG(1) << "shardKeyTest passed" << endl;
        }
    } shardKeyTest;

} // namespace mongo
