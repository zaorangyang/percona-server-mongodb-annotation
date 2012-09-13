/** @file loadgenerator.cpp */

/**
 *    Copyright (C) 2012 10gen Inc.
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
 */

/*
 *  LoadGenerator drives a certain number (# threads) of simultaneous operation into a
 *  specified number of databases  as quickly as it can at a mongo instance,
 *  continuously, for some number of seconds.
 *
 */

/*
 * For internal reference:
 * Each document generated by the docgenerator.cpp is 176 bytes long
 * Number of documents per instance size :
 * small (500 MB) : 2978905 docs spread over 5 dbs (each db is 100 MB)  Docs Per DB :  595781
 * medium (5 GB) : 30504030 docs spread over 5 dbs (each db is 1 GB)         Docs Per DB :  6100806
 * large (25 GB) : 152520145 docs evenly spread over 5 dbs (each db is 5 GB)  Docs Per DB :  30504029
 * vlarge (100 GB) : 621172954 docs evenly spread over 10 dbs (each db is 10 GB)  Docs Per DB : 61008058
 *
 */

#include <map>
#include <string>

#include <boost/program_options.hpp>
#include <boost/scoped_ptr.hpp>

#include "mongo/base/initializer.h"
#include "mongo/util/assert_util.h"
#include "mongo/scripting/bench.h"
#include "mongo/client/dbclientinterface.h"
#include "mongo/tools/docgenerator.h"
#include "mongo/util/map_util.h"
#include "mongo/util/md5.hpp"
#include "mongo/util/mongoutils/str.h"

using namespace std;

using namespace mongo;

namespace po = boost::program_options;

namespace {

struct LoadGeneratorOptions {
    LoadGeneratorOptions() :
        hostname("localhost"),
        type("query"),
        instanceSize( "large" ),
        numdbs( 5 ),
        resultNS( "" ),
        numOps( 60000 ),
        durationSeconds( 60 ),
        parallelThreads( 32 ),
        trials( 5 ),
        docsPerDB( 0 )
     { }

    string hostname;
    string type;
    string instanceSize;
    int numdbs;
    string resultNS;
    int numOps;
    double durationSeconds;
    int parallelThreads;
    int trials;
    unsigned long long docsPerDB;
};


struct OperationStats {
    OperationStats( const unsigned long long numEvents, const unsigned long long totalTimeMicros,
                    const long long opcounter ) :
        numEvents( numEvents ),
        totalTimeMicros( totalTimeMicros ),
        opcounter( opcounter ) { }
    OperationStats() { }

    unsigned long long numEvents;
    unsigned long long totalTimeMicros;
    long long opcounter;
};

// ------Globals and typedefs----------
LoadGeneratorOptions globalLoadGenOption;
typedef std::map<std::string, OperationStats> OpStatsMap;

double randomBetweenRange(const int& min, const int& max) {
    return rand() % (max - min) + min;
}

mongo::DBClientBase *getDBConnection() {
    string errmsg;
    mongo::ConnectionString connectionString = mongo::ConnectionString::parse(
            globalLoadGenOption.hostname, errmsg );
    mongo::fassert( 16265, connectionString.isValid() );
    mongo::DBClientBase *connection = connectionString.connect( errmsg );
    mongo::fassert( 16266, connection != NULL );
    return connection;
}

void dropNS(const string ns) {
    boost::scoped_ptr<mongo::DBClientBase> connection( getDBConnection() );
    connection->dropCollection( ns );
}

void dropDB(const string db) {
    boost::scoped_ptr<mongo::DBClientBase> connection( getDBConnection() );
    connection->dropDatabase( db );
}

void writeToNS(const string ns, const mongo::BSONObj bs) {
    boost::scoped_ptr<mongo::DBClientBase> connection( getDBConnection() );
    connection->insert( ns, bs );
    mongo::fassert( 16267, connection->getLastError().empty() );
}

// find the number of documents in a namespace
void numDocsInNS(const string ns) {
    boost::scoped_ptr<mongo::DBClientBase> connection( getDBConnection() );
    globalLoadGenOption.docsPerDB =  connection->count( ns );
}


mongo::BSONArray generateInsertOps() {

    //create a document config object
    BSONObj args = BSONObjBuilder()
                      .append( "blob", "MongoDB is an open source document-oriented database system." )
                      .append( "md5seed", "newyork" )
                      .append( "counterUp", 0 )
                      .append( "counterDown", numeric_limits<long long>::max() ).obj();

    scoped_ptr<DocumentGenerator> docGen( DocumentGenerator::makeDocumentGenerator(args) );
    mongo::BSONArrayBuilder insertOps;
    for (int i = 0; i < globalLoadGenOption.numOps; ++i) {

        //insert into databases
        string insertNS = mongoutils::str::stream() << globalLoadGenOption.instanceSize << "DB" <<
                                                     i % globalLoadGenOption.numdbs << "I.sampledata";
        BSONObj doc = docGen->createDocument();
        insertOps.append( BSON( "ns" << insertNS <<
                                "op" << "insert" <<
                                 "doc" << doc <<
                                 "safe" << true ) );
    }
    return insertOps.arr();
}


mongo::BSONArray generateFindOneOps() {

    mongo::BSONArrayBuilder queryOps;

    // query a namespace and find the number of docs in that ns. All benchmark namespaces should have
    //  the same number of docs.
    string queryNS = mongoutils::str::stream() << globalLoadGenOption.instanceSize
                                               << "DB0.sampledata";
    numDocsInNS( queryNS );

    // Now fill the queryOps array. The findOne query operations will be evenly distributed
    // across all databases. Thus it tries to find a random document from db1, then db2,
    // and so on and so forth.
    for (int i = 0; i < globalLoadGenOption.numOps; ++i) {

        queryNS = mongoutils::str::stream() << globalLoadGenOption.instanceSize
                                            << "DB"
                                            << i % globalLoadGenOption.numdbs
                                            << ".sampledata";

        // select a random document among all the documents
        unsigned long long centerQueryKey =
                ( randomBetweenRange(0, 100 ) *  globalLoadGenOption.docsPerDB ) / 100;

        // cast to long long from unsigned long long as BSON didn't have the overloaded method
        mongo::BSONObj query =
                BSON( "counterUp" << static_cast<long long>( floor(centerQueryKey) ) );

        queryOps.append( BSON( "ns" << queryNS <<
                                "op" << "findOne" <<
                                "query" << query) );
    }

    return queryOps.arr();
}


mongo::BenchRunConfig *createBenchRunConfig() {

    BSONArray ops;

    if ( globalLoadGenOption.type == "findOne" )
        ops = generateFindOneOps();
    else if ( globalLoadGenOption.type == "insert" )
        ops = generateInsertOps();

    return mongo::BenchRunConfig::createFromBson(
            BSON( "ops" << ops <<
                  "parallel" << globalLoadGenOption.parallelThreads <<
                  "seconds" << globalLoadGenOption.durationSeconds <<
                  "host"<< globalLoadGenOption.hostname ) );
}

/*
 * The stats object from benchRun has two sub-objects : findOneCounter and opcounters
 * This function creates a single map  data structure to store all the info.
 */

void collectAllStats( const BenchRunStats& stats, OpStatsMap& allStats ) {

    allStats.clear();
    allStats.insert( std::make_pair("findOne",
                                    OperationStats(stats.findOneCounter.getNumEvents(),
                                                   stats.findOneCounter.getTotalTimeMicros(),
                                                   mapFindWithDefault<std::string, long long>
                                                   (stats.opcounters, "query", 0)
                                                   )) );
    allStats.insert( std::make_pair("insert",
                                    OperationStats(stats.insertCounter.getNumEvents(),
                                                   stats.insertCounter.getTotalTimeMicros(),
                                                   mapFindWithDefault<std::string, long long>
                                                   (stats.opcounters, "insert", 0)
                                                   )) );

}

// add the result of this trial to the trials array
BSONObj makeTrialDocument( const OpStatsMap& allStats ) {

    BSONObjBuilder outerBuilder;
    for (OpStatsMap::const_iterator it = allStats.begin(); it != allStats.end(); ++it) {

        unsigned long long numEvents = it->second.numEvents;
        unsigned long long totalTimeMicros = it->second.totalTimeMicros;

        BSONObjBuilder innerDocBuilder;
        innerDocBuilder.append("numEvents", static_cast<long long>(numEvents));
        innerDocBuilder.append("totalTimeMicros", static_cast<long long>(totalTimeMicros));

        if (numEvents)
            innerDocBuilder.append("latencyMicros", static_cast<double>(totalTimeMicros/numEvents));

        outerBuilder.append(it->first, innerDocBuilder.obj());
    }
    return outerBuilder.obj();
}

BSONObj buildInformation() {
    boost::scoped_ptr<mongo::DBClientBase> connection( getDBConnection() );
    BSONObj info;
    connection->simpleCommand("admin", &info, "buildinfo");
    return info;
}

BSONObj createResultDoc(const BSONArray& trialsArray) {

      return BSON( "name" << globalLoadGenOption.type <<
                 "config" << BSON ( "hostname" << globalLoadGenOption.hostname <<
                                    "instanceSize" <<  globalLoadGenOption.instanceSize <<
                                    "durationSeconds" <<  globalLoadGenOption.durationSeconds <<
                                    "parallelThreads" <<  globalLoadGenOption.parallelThreads <<
                                    "numOps" <<  globalLoadGenOption.numOps <<
                                    "Date" << 10 <<
                                    "buildInfo" << buildInformation()
                                   ) <<
                 "trials" << trialsArray );
}

void runTest() {
    stringstream oss;
    BSONArrayBuilder trialsBuilder;

    // drop any previous dbs with the same name
    for (int j=0; j < globalLoadGenOption.numdbs; ++j) {
        string insertTestDB = mongoutils::str::stream() << globalLoadGenOption.instanceSize <<
                                                       "DB" << j <<"I" ;
        dropDB(insertTestDB);
    }

    for (int i = 0; i<globalLoadGenOption.trials; ++i) {

        BenchRunner runner( createBenchRunConfig() );
        runner.start();
        sleepmillis( 1000 * globalLoadGenOption.durationSeconds );
        runner.stop();
        BenchRunStats stats;
        runner.populateStats(&stats);

        // collate all the stats (and save it in a local allstats map
        std::map<std::string, OperationStats> allStats;
        collectAllStats(stats, allStats);

        trialsBuilder.append(makeTrialDocument(allStats));

        // print for now -- this is temporary and will be removed
        oss << allStats.find("insert")->second.totalTimeMicros / allStats.find("insert")->second.numEvents <<
               "    " <<
               allStats.find("insert")->second.opcounter / globalLoadGenOption.durationSeconds <<
               "    ";

        //clean up the newly created dbs for next trial
        for (int j=0; j < globalLoadGenOption.numdbs; ++j) {
            string insertTestDB = mongoutils::str::stream() << globalLoadGenOption.instanceSize <<
                                                           "DB" << j <<"I" ;
            dropDB(insertTestDB);
        }
    }
    // Write the experiment document to the result NS. If the user did not pass a resultNS cmdline
    // parameter then we won't write the results to the database.
    //  This is useful in cases where we just want to drive a constant load from a client and are
    //  not really interested in the statistics from it and so don't really care to save the stats
    //  to a db.

    string resultNS = globalLoadGenOption.resultNS;
    if( !resultNS.empty() ) {
        BSONObj resultDoc = createResultDoc(trialsBuilder.arr());
        writeToNS( resultNS, resultDoc );
    }

    // temp line -- will be removed
    cout << oss.str() << endl;
}


int parseCmdLineOptions(int argc, char **argv) {

    try {

        po::options_description general_options("General options");

        general_options.add_options()
        ("help", "produce help message")
        ("hostname,H", po::value<string>() , "ip address of the host where mongod is running" )
        ("type", po::value<string>() , "findOne/insert" )
        ("instanceSize,I", po::value<string>(), "DB type (small/medium/large/vlarge)" )
        ("numdbs", po::value<int>(), " number of databases in this instance" )
        ("trials", po::value<int>(), "number of trials")
        ("durationSeconds,D", po::value<double>(), "how long should each trial run")
        ("parallelThreads,P",po::value<int>(), "number of threads")
        ("numOps", po::value<int>(), "number of ops per thread")
        ("resultNS", po::value<string>(), "result NS where you would like to save the results."
                "If this parameter is empty results will not be written")
        ;

        po::variables_map params;
        po::store(po::parse_command_line(argc, argv, general_options), params);
        po::notify(params);

        // Parse the values if supplied by the user. No data sanity check is performed
        // here so meaningless values (for eg. passing --numdbs 0) can crash the program.
        // TODO: Perform data sanity check

        if(params.count("help")) {
            cout << general_options << "\n";
            return 1;
        }
        if (params.count("hostname")) {
            globalLoadGenOption.hostname = params["hostname"].as<string>();
        }
        if (params.count("type")) {
            globalLoadGenOption.type = params["type"].as<string>();
        }
        if (params.count("instanceSize")) {
            globalLoadGenOption.instanceSize = params["instanceSize"].as<string>();
        }
        if (params.count("numdbs")) {
            globalLoadGenOption.numdbs = params["numdbs"].as<int>();
        }
        if (params.count("trials")) {
            globalLoadGenOption.trials = params["trials"].as<int>();
        }
        if (params.count("durationSeconds")) {
            globalLoadGenOption.durationSeconds = params["durationSeconds"].as<double>();
        }
        if (params.count("parallelThreads")) {
            globalLoadGenOption.parallelThreads = params["parallelThreads"].as<int>();
        }
        if (params.count("numOps")) {
            globalLoadGenOption.numOps = params["numOps"].as<int>();
        }
        if (params.count("resultNS")) {
           globalLoadGenOption.resultNS = params["resultNS"].as<string>();
       }
    }
    catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

} // namespace


int main(int argc, char **argv, char** envp) {
    mongo::runGlobalInitializersOrDie(argc, argv, envp);
    if( parseCmdLineOptions(argc, argv) )
        return 1;

    runTest();
    return 0;
}

