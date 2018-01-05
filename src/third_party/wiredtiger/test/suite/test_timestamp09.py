#!/usr/bin/env python
#
# Public Domain 2014-2018 MongoDB, Inc.
# Public Domain 2008-2014 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# test_timestamp09.py
#   Timestamps: API
#

from suite_subprocess import suite_subprocess
import wiredtiger, wttest

def timestamp_str(t):
    return '%x' % t

class test_timestamp09(wttest.WiredTigerTestCase, suite_subprocess):
    tablename = 'test_timestamp08'
    uri = 'table:' + tablename

    def test_timestamp_api(self):
        if not wiredtiger.timestamp_build():
            self.skipTest('requires a timestamp build')

        self.session.create(self.uri, 'key_format=i,value_format=i')
        c = self.session.open_cursor(self.uri)

        # Begin by adding some data.
        self.session.begin_transaction()
        self.session.commit_transaction(
            'commit_timestamp=' + timestamp_str(1))
        c[1] = 1

        # In a single transaction it is illegal to set a commit timestamp
        # older than the first commit timestamp used for this transaction.
        # Check both timestamp_transaction and commit_transaction API.
        self.session.begin_transaction()
        self.session.timestamp_transaction(
            'commit_timestamp=' + timestamp_str(3))
        c[3] = 3
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.timestamp_transaction(
                'commit_timestamp=' + timestamp_str(2)),
                '/older than the first commit timestamp/')
        c[2] = 2
        self.session.rollback_transaction()

        self.session.begin_transaction()
        self.session.timestamp_transaction(
            'commit_timestamp=' + timestamp_str(4))
        c[4] = 4
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.commit_transaction(
                'commit_timestamp=' + timestamp_str(3)),
                '/older than the first commit timestamp/')

        # Commit timestamp >= Oldest timestamp
        # Check both timestamp_transaction and commit_transaction API.
        self.session.begin_transaction()
        c[3] = 3
        self.session.commit_transaction(
            'commit_timestamp=' + timestamp_str(3))
        self.conn.set_timestamp('oldest_timestamp=' + timestamp_str(3))

        self.session.begin_transaction()
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.timestamp_transaction(
                'commit_timestamp=' + timestamp_str(2)),
                '/older than oldest timestamp/')
        c[2] = 2
        self.session.rollback_transaction()

        self.session.begin_transaction()
        c[2] = 2
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.commit_transaction(
                'commit_timestamp=' + timestamp_str(2)),
                '/older than oldest timestamp/')

        self.session.begin_transaction()
        c[4] = 4
        self.session.commit_transaction(
            'commit_timestamp=' + timestamp_str(4))

        # Oldest timestamp <= Stable timestamp and both oldest and stable
        # timestamp should proceed forward.
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.conn.set_timestamp('oldest_timestamp=' +
                timestamp_str(3) + ',stable_timestamp=' + timestamp_str(1)),
                '/oldest timestamp must not be later than stable timestamp/')

        # Oldest timestamp is 3 at the moment, trying to set it to an earlier
        # timestamp is a no-op.
        self.conn.set_timestamp('oldest_timestamp=' + timestamp_str(1))
        self.assertTimestampsEqual(self.conn.query_timestamp('get=oldest'), timestamp_str(3))

        self.conn.set_timestamp('oldest_timestamp=' + timestamp_str(3) +
            ',stable_timestamp=' + timestamp_str(3))
        self.conn.set_timestamp('stable_timestamp=' + timestamp_str(5))
        # Stable timestamp is 5 at the moment, trying to set it to an earlier
        # timestamp is a no-op.
        self.conn.set_timestamp('stable_timestamp=' + timestamp_str(4))
        self.assertTimestampsEqual(self.conn.query_timestamp('get=stable'), timestamp_str(5))

        self.conn.set_timestamp('oldest_timestamp=' + timestamp_str(5))
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.conn.set_timestamp('oldest_timestamp=' +
                timestamp_str(6)),
                '/oldest timestamp must not be later than stable timestamp/')

        # Commit timestamp >= Stable timestamp.
        # Check both timestamp_transaction and commit_transaction API.
        # Oldest and stable timestamp are set to 5 at the moment.
        self.conn.set_timestamp('stable_timestamp=' + timestamp_str(6))
        self.session.begin_transaction()
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.timestamp_transaction(
                'commit_timestamp=' + timestamp_str(5)),
                '/older than stable timestamp/')
        c[5] = 5
        self.session.rollback_transaction()

        self.session.begin_transaction()
        c[5] = 5
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.commit_transaction(
                'commit_timestamp=' + timestamp_str(5)),
                '/older than stable timestamp/')

        # When explicitly set, commit timestamp for a transaction can be earlier
        # than the commit timestamp of an earlier transaction.
        self.session.begin_transaction()
        c[6] = 6
        self.session.commit_transaction(
            'commit_timestamp=' + timestamp_str(6))
        self.session.begin_transaction()
        c[8] = 8
        self.session.commit_transaction(
            'commit_timestamp=' + timestamp_str(8))
        self.session.begin_transaction()
        c[7] = 7
        self.session.commit_transaction(
            'commit_timestamp=' + timestamp_str(7))

        # Read timestamp >= Oldest timestamp
        self.conn.set_timestamp('oldest_timestamp=' + timestamp_str(7) +
            ',stable_timestamp=' + timestamp_str(7))
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.begin_transaction('read_timestamp=' +
                timestamp_str(6)),
                '/older than oldest timestamp/')

        # c[8] is not visible at read_timestamp < 8
        self.session.begin_transaction('read_timestamp=' + timestamp_str(7))
        self.assertEqual(c[6], 6)
        self.assertEqual(c[7], 7)
        c.set_key(8)
        self.assertEqual(c.search(), wiredtiger.WT_NOTFOUND)
        self.session.commit_transaction()

        self.session.begin_transaction('read_timestamp=' + timestamp_str(8))
        self.assertEqual(c[6], 6)
        self.assertEqual(c[7], 7)
        self.assertEqual(c[8], 8)
        self.session.commit_transaction()

        # We can move the oldest timestamp backwards with "force"
        self.conn.set_timestamp(
            'oldest_timestamp=' + timestamp_str(5) + ',force')
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.begin_transaction('read_timestamp=' +
                timestamp_str(4)),
                '/older than oldest timestamp/')
        self.session.begin_transaction('read_timestamp=' + timestamp_str(6))
        self.session.commit_transaction()

if __name__ == '__main__':
    wttest.run()
