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
*/

#include "mongo/pch.h"

#include "mongo/db/initialize_server_global_state.h"

#include <boost/filesystem/operations.hpp>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "mongo/db/cmdline.h"
#include "mongo/db/security_common.h"
#include "mongo/util/log.h"
#include "mongo/util/net/listen.h"
#include "mongo/util/processinfo.h"

namespace fs = boost::filesystem;

namespace mongo {

    void setupSignals( bool inFork );

#ifndef _WIN32
    // support for exit value propagation with fork
    void launchSignal( int sig ) {
        if ( sig == SIGUSR2 ) {
            pid_t cur = getpid();
            
            if ( cur == cmdLine.parentProc || cur == cmdLine.leaderProc ) {
                // signal indicates successful start allowing us to exit
                _exit(0);
            } 
        }
    }

    static void setupLaunchSignals() {
        verify( signal(SIGUSR2 , launchSignal ) != SIG_ERR );
    }

    void CmdLine::launchOk() {
        if ( cmdLine.doFork ) {
            // killing leader will propagate to parent
            verify( kill( cmdLine.leaderProc, SIGUSR2 ) == 0 );
        }
    }
#endif

    bool initializeServerGlobalState(bool isMongodShutdownSpecialCase) {

        Listener::globalTicketHolder.resize( cmdLine.maxConns );

#ifndef _WIN32
        if (!fs::is_directory(cmdLine.socket)) {
            cout << cmdLine.socket << " must be a directory" << endl;
            return false;
        }

        if (cmdLine.doFork) {
            fassert(16447, !cmdLine.logpath.empty() || cmdLine.logWithSyslog);

            cout.flush();
            cerr.flush();

            cmdLine.parentProc = getpid();

            // facilitate clean exit when child starts successfully
            setupLaunchSignals();

            pid_t c = fork();
            if ( c ) {
                int pstat;
                waitpid(c, &pstat, 0);

                if ( WIFEXITED(pstat) ) {
                    if ( ! WEXITSTATUS(pstat) ) {
                        cout << "child process started successfully, parent exiting" << endl;
                    }

                    _exit( WEXITSTATUS(pstat) );
                }

                _exit(50);
            }

            if ( chdir("/") < 0 ) {
                cout << "Cant chdir() while forking server process: " << strerror(errno) << endl;
                ::_exit(-1);
            }
            setsid();

            cmdLine.leaderProc = getpid();

            pid_t c2 = fork();
            if ( c2 ) {
                int pstat;
                cout << "forked process: " << c2 << endl;
                waitpid(c2, &pstat, 0);

                if ( WIFEXITED(pstat) ) {
                    _exit( WEXITSTATUS(pstat) );
                }

                _exit(51);
            }

            // stdout handled in initLogging
            //fclose(stdout);
            //freopen("/dev/null", "w", stdout);

            fclose(stderr);
            fclose(stdin);

            FILE* f = freopen("/dev/null", "w", stderr);
            if ( f == NULL ) {
                cout << "Cant reassign stderr while forking server process: " << strerror(errno) << endl;
                return false;
            }

            f = freopen("/dev/null", "r", stdin);
            if ( f == NULL ) {
                cout << "Cant reassign stdin while forking server process: " << strerror(errno) << endl;
                return false;
            }

            setupCoreSignals();
            setupSignals( true );
        }

        if (cmdLine.logWithSyslog) {
            StringBuilder sb;
            sb << cmdLine.binaryName << "." << cmdLine.port;
            Logstream::useSyslog( sb.str().c_str() );
        }
#endif
        if (!cmdLine.logpath.empty() && !isMongodShutdownSpecialCase) {
            fassert(16448, !cmdLine.logWithSyslog);
            string absoluteLogpath = boost::filesystem::complete(
                    cmdLine.logpath, cmdLine.cwd).native_file_string();
            if (!initLogging(absoluteLogpath, cmdLine.logAppend)) {
                cout << "Bad logpath value: \"" << absoluteLogpath << "\"; terminating." << endl;
                return false;
            }
        }

        if (!cmdLine.pidFile.empty()) {
            writePidFile(cmdLine.pidFile);
        }

        if (!cmdLine.keyFile.empty()) {

            if (!setUpSecurityKey(cmdLine.keyFile)) {
                // error message printed in setUpPrivateKey
                return false;
            }

            noauth = false;
        }

#ifdef MONGO_SSL
        if (cmdLine.sslOnNormalPorts) {

            if ( cmdLine.sslPEMKeyPassword.size() == 0 ) {
                log() << "need sslPEMKeyPassword" << endl;
                return false;
            }

            if ( cmdLine.sslPEMKeyFile.size() == 0 ) {
                log() << "need sslPEMKeyFile" << endl;
                return false;
            }

            cmdLine.sslServerManager = new SSLManager( false );
            if ( ! cmdLine.sslServerManager->setupPEM( cmdLine.sslPEMKeyFile , cmdLine.sslPEMKeyPassword ) ) {
                return false;
            }
        }
        else if ( cmdLine.sslPEMKeyFile.size() || cmdLine.sslPEMKeyPassword.size() ) {
            log() << "need to enable sslOnNormalPorts" << endl;
            return false;
        }
#endif

        return true;
    }

    static void ignoreSignal( int sig ) {}

    static void rotateLogsOrDie(int sig) {
        fassert(16176, rotateLogs());
    }

    void setupCoreSignals() {
#if !defined(_WIN32)
        verify( signal(SIGUSR1 , rotateLogsOrDie ) != SIG_ERR );
        verify( signal(SIGHUP , ignoreSignal ) != SIG_ERR );
#endif
    }

}  // namespace mongo
