// @file queryoptimizercursorimpl.h

/**
 *    Copyright (C) 2011 10gen Inc.
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

#pragma once

#include "queryoptimizercursor.h"

namespace mongo {
    
    /** Helper class for caching and counting matches during execution of a QueryPlan. */
    class CachedMatchCounter {
    public:
        /**
         * @param aggregateNscanned - shared count of nscanned for this and othe plans.
         * @param cumulativeCount - starting point for accumulated count over a series of plans.
         */
        CachedMatchCounter( long long &aggregateNscanned, int cumulativeCount ) : _aggregateNscanned( aggregateNscanned ), _nscanned(), _cumulativeCount( cumulativeCount ), _count(), _checkDups(), _match( Unknown ), _counted() {}
        
        /** Set whether dup checking is enabled when counting. */
        void setCheckDups( bool checkDups ) { _checkDups = checkDups; }
        
        /**
         * Usual sequence of events:
         * 1) resetMatch() - reset stored match value to Unkonwn.
         * 2) setMatch() - set match value to a definite true/false value.
         * 3) knowMatch() - check if setMatch() has been called.
         * 4) countMatch() - increment count if match is true.
         */
        
        void resetMatch() {
            _match = Unknown;
            _counted = false;
        }
        void setMatch( bool match ) { _match = match ? True : False; }
        bool knowMatch() const { return _match != Unknown; }
        void countMatch( const DiskLoc &loc ) {
            if ( !_counted && _match == True && !getsetdup( loc ) ) {
                ++_cumulativeCount;
                ++_count;
                _counted = true;
            }
        }

        bool enoughCumulativeMatchesToChooseAPlan() const {
            // This is equivalent to the default condition for switching from
            // a query to a getMore, which was the historical default match count for
            // choosing a plan.
            return _cumulativeCount >= 101;
        }
        bool enoughMatchesToRecordPlan() const {
            // Recording after 50 matches is a historical default (101 default limit / 2).
            return _count > 50;
        }

        int cumulativeCount() const { return _cumulativeCount; }
        int count() const { return _count; }
        
        /** Update local and aggregate nscanned counts. */
        void updateNscanned( long long nscanned ) {
            _aggregateNscanned += ( nscanned - _nscanned );
            _nscanned = nscanned;
        }
        long long nscanned() const { return _nscanned; }
        long long &aggregateNscanned() const { return _aggregateNscanned; }
    private:
        bool getsetdup( const DiskLoc &loc ) {
            if ( !_checkDups ) {
                return false;
            }
            pair<set<DiskLoc>::iterator, bool> p = _dups.insert( loc );
            return !p.second;
        }
        long long &_aggregateNscanned;
        long long _nscanned;
        int _cumulativeCount;
        int _count;
        bool _checkDups;
        enum MatchState { Unknown, False, True };
        MatchState _match;
        bool _counted;
        set<DiskLoc> _dups;
    };
    
    /** Dup tracking class, optimizing one common case with small set and few initial reads. */
    class SmallDupSet {
    public:
        SmallDupSet() : _accesses() {
            _vec.reserve( 250 );
        }
        /** @return true if @param 'loc' already added to the set, false if adding to the set in this call. */
        bool getsetdup( const DiskLoc &loc ) {
            access();
            return vec() ? getsetdupVec( loc ) : getsetdupSet( loc );
        }
        /** @return true when @param loc in the set. */
        bool getdup( const DiskLoc &loc ) {
            access();
            return vec() ? getdupVec( loc ) : getdupSet( loc );
        }            
    private:
        void access() {
            ++_accesses;
            mayUpgrade();
        }
        void mayUpgrade() {
            if ( vec() && _accesses > 500 ) {
                _set.insert( _vec.begin(), _vec.end() );
            }
        }
        bool vec() const {
            return _set.size() == 0;
        }
        bool getsetdupVec( const DiskLoc &loc ) {
            if ( getdupVec( loc ) ) {
                return true;
            }
            _vec.push_back( loc );
            return false;
        }
        bool getdupVec( const DiskLoc &loc ) const {
            for( vector<DiskLoc>::const_iterator i = _vec.begin(); i != _vec.end(); ++i ) {
                if ( *i == loc ) {
                    return true;
                }
            }
            return false;
        }
        bool getsetdupSet( const DiskLoc &loc ) {
            pair<set<DiskLoc>::iterator, bool> p = _set.insert(loc);
            return !p.second;
        }
        bool getdupSet( const DiskLoc &loc ) {
            return _set.count( loc ) > 0;
        }
        vector<DiskLoc> _vec;
        set<DiskLoc> _set;
        long long _accesses;
    };
} // namespace mongo
