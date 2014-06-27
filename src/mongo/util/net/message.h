// message.h

/*    Copyright 2009 10gen Inc.
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

#pragma once

#include <vector>

#include "mongo/platform/atomic_word.h"
#include "mongo/platform/cstdint.h"
#include "mongo/util/goodies.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/net/sock.h"

namespace mongo {

    /**
     * Maximum accepted message size on the wire protocol.
     */
    const size_t MaxMessageSizeBytes = 48 * 1000 * 1000;

    class Message;
    class MessagingPort;
    class PiggyBackData;

    typedef uint32_t MSGID;

    enum Operations {
        opReply = 1,     /* reply. responseTo is set. */
        dbMsg = 1000,    /* generic msg command followed by a std::string */
        dbUpdate = 2001, /* update object */
        dbInsert = 2002,
        //dbGetByOID = 2003,
        dbQuery = 2004,
        dbGetMore = 2005,
        dbDelete = 2006,
        dbKillCursors = 2007
    };

    bool doesOpGetAResponse( int op );

    inline const char * opToString( int op ) {
        switch ( op ) {
        case 0: return "none";
        case opReply: return "reply";
        case dbMsg: return "msg";
        case dbUpdate: return "update";
        case dbInsert: return "insert";
        case dbQuery: return "query";
        case dbGetMore: return "getmore";
        case dbDelete: return "remove";
        case dbKillCursors: return "killcursors";
        default:
            massert( 16141, str::stream() << "cannot translate opcode " << op, !op );
            return "";
        }
    }

    inline bool opIsWrite( int op ) {
        switch ( op ) {

        case 0:
        case opReply:
        case dbMsg:
        case dbQuery:
        case dbGetMore:
        case dbKillCursors:
            return false;

        case dbUpdate:
        case dbInsert:
        case dbDelete:
            return true;

        default:
            PRINT(op);
            verify(0);
            return "";
        }

    }

#pragma pack(1)
    /* see http://dochub.mongodb.org/core/mongowireprotocol
    */
    struct MSGHEADER {
        int messageLength; // total message size, including this
        int requestID;     // identifier for this message
        int responseTo;    // requestID from the original request
        //   (used in responses from db)
        int opCode;
    };
#pragma pack()

#pragma pack(1)
    /* todo merge this with MSGHEADER (or inherit from it). */
    struct MsgData {
        int len; /* len of the msg, including this field */
        MSGID id; /* request/reply id's match... */
        MSGID responseTo; /* id of the message we are responding to */
        short _operation;
        char _flags;
        char _version;
        int operation() const {
            return _operation;
        }
        void setOperation(int o) {
            _flags = 0;
            _version = 0;
            _operation = o;
        }
        char _data[4];

        int& dataAsInt() {
            return *((int *) _data);
        }

        bool valid() {
            if ( len <= 0 || len > ( 4 * BSONObjMaxInternalSize ) )
                return false;
            if ( _operation < 0 || _operation > 30000 )
                return false;
            return true;
        }

        long long getCursor() {
            verify( responseTo > 0 );
            verify( _operation == opReply );
            long long * l = (long long *)(_data + 4);
            return l[0];
        }

        int dataLen(); // len without header
    };
    const int MsgDataHeaderSize = sizeof(MsgData) - 4;
    inline int MsgData::dataLen() {
        return len - MsgDataHeaderSize;
    }
#pragma pack()

    class Message {
    public:
        // we assume here that a vector with initial size 0 does no allocation (0 is the default, but wanted to make it explicit).
        Message() : _buf( 0 ), _data( 0 ), _freeIt( false ) {}
        Message( void * data , bool freeIt ) :
            _buf( 0 ), _data( 0 ), _freeIt( false ) {
            _setData( reinterpret_cast< MsgData* >( data ), freeIt );
        };
        Message(Message& r) : _buf( 0 ), _data( 0 ), _freeIt( false ) {
            *this = r;
        }
        ~Message() {
            reset();
        }

        SockAddr _from;

        MsgData *header() const {
            verify( !empty() );
            return _buf ? _buf : reinterpret_cast< MsgData* > ( _data[ 0 ].first );
        }
        int operation() const { return header()->operation(); }

        MsgData *singleData() const {
            massert( 13273, "single data buffer expected", _buf );
            return header();
        }

        bool empty() const { return !_buf && _data.empty(); }

        int size() const {
            int res = 0;
            if ( _buf ) {
                res =  _buf->len;
            }
            else {
                for (MsgVec::const_iterator it = _data.begin(); it != _data.end(); ++it) {
                    res += it->second;
                }
            }
            return res;
        }

        int dataSize() const { return size() - sizeof(MSGHEADER); }

        // concat multiple buffers - noop if <2 buffers already, otherwise can be expensive copy
        // can get rid of this if we make response handling smarter
        void concat() {
            if ( _buf || empty() ) {
                return;
            }

            verify( _freeIt );
            int totalSize = 0;
            for (std::vector< std::pair< char *, int > >::const_iterator i = _data.begin();
                 i != _data.end(); ++i) {
                totalSize += i->second;
            }
            char *buf = (char*)malloc( totalSize );
            char *p = buf;
            for (std::vector< std::pair< char *, int > >::const_iterator i = _data.begin();
                 i != _data.end(); ++i) {
                memcpy( p, i->first, i->second );
                p += i->second;
            }
            reset();
            _setData( (MsgData*)buf, true );
        }

        // vector swap() so this is fast
        Message& operator=(Message& r) {
            verify( empty() );
            verify( r._freeIt );
            _buf = r._buf;
            r._buf = 0;
            if ( r._data.size() > 0 ) {
                _data.swap( r._data );
            }
            r._freeIt = false;
            _freeIt = true;
            return *this;
        }

        void reset() {
            if ( _freeIt ) {
                if ( _buf ) {
                    free( _buf );
                }
                for (std::vector< std::pair< char *, int > >::const_iterator i = _data.begin();
                     i != _data.end(); ++i) {
                    free(i->first);
                }
            }
            _buf = 0;
            _data.clear();
            _freeIt = false;
        }

        // use to add a buffer
        // assumes message will free everything
        void appendData(char *d, int size) {
            if ( size <= 0 ) {
                return;
            }
            if ( empty() ) {
                MsgData *md = (MsgData*)d;
                md->len = size; // can be updated later if more buffers added
                _setData( md, true );
                return;
            }
            verify( _freeIt );
            if ( _buf ) {
                _data.push_back(std::make_pair((char*)_buf, _buf->len));
                _buf = 0;
            }
            _data.push_back(std::make_pair(d, size));
            header()->len += size;
        }

        // use to set first buffer if empty
        void setData(MsgData *d, bool freeIt) {
            verify( empty() );
            _setData( d, freeIt );
        }
        void setData(int operation, const char *msgtxt) {
            setData(operation, msgtxt, strlen(msgtxt)+1);
        }
        void setData(int operation, const char *msgdata, size_t len) {
            verify( empty() );
            size_t dataLen = len + sizeof(MsgData) - 4;
            MsgData *d = (MsgData *) malloc(dataLen);
            memcpy(d->_data, msgdata, len);
            d->len = fixEndian(dataLen);
            d->setOperation(operation);
            _setData( d, true );
        }

        bool doIFreeIt() {
            return _freeIt;
        }

        void send( MessagingPort &p, const char *context );
        
        std::string toString() const;

    private:
        void _setData( MsgData *d, bool freeIt ) {
            _freeIt = freeIt;
            _buf = d;
        }
        // if just one buffer, keep it in _buf, otherwise keep a sequence of buffers in _data
        MsgData * _buf;
        // byte buffer(s) - the first must contain at least a full MsgData unless using _buf for storage instead
        typedef std::vector< std::pair< char*, int > > MsgVec;
        MsgVec _data;
        bool _freeIt;
    };


    MSGID nextMessageId();


} // namespace mongo
