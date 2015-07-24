// slice.h

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

#pragma once

#include <algorithm>
#include <string>

#include "mongo/bson/bsonobj.h"
#include "mongo/db/storage/key_string.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/shared_buffer.h"

namespace mongo {

    /**
     * Slice is similar to a std::string that may or may not own its own
     * memory.  It is similar to BSONObj in that you can get a copy that
     * does own its own memory, and if the original already owned it, the
     * copy will share ownership of the owned buffer.
     *
     * Some sensible default constructors are provided, as well as copy
     * and move constructors/assignment operators.
     *
     * To convert between a POD object and a Slice referencing such an
     * object, use Slice::of(val) and slice.as<T>().
     *
     * To write a fresh slice, construct a Slice with just a size argument
     * to reserve some space, then write into mutableData().
     *
     * You can use algorithms that use RandomAccessIterators, if you want
     * this for some reason.
     */
    class Slice {
    public:
        Slice()
            : _data(NULL),
              _size(0)
        {}

        explicit Slice(size_t sz)
            : _buf(SharedBuffer::allocate(sz)),
              _data(_buf.get()),
              _size(sz)
        {}

        Slice(const char *p, size_t sz)
            : _data(p),
              _size(sz)
        {}

        explicit Slice(const std::string &str)
            : _data(str.c_str()),
              _size(str.size())
        {}

        Slice(const Slice &other)
            : _buf(other._buf),
              _data(other._data),
              _size(other._size)
        {}

        Slice& operator=(const Slice &other) {
            _buf = other._buf;
            _data = other._data;
            _size = other._size;
            return *this;
        }

#if __cplusplus >= 201103L
        Slice(Slice&& other)
            : _buf(),
              _data(NULL),
              _size(0)
        {
            swap(_buf, other._buf);
            std::swap(_data, other._data);
            std::swap(_size, other._size);
        }

        Slice& operator=(Slice&& other) {
            swap(_buf, other._buf);
            std::swap(_data, other._data);
            std::swap(_size, other._size);
            return *this;
        }
#endif

        template<typename T>
        static Slice of(const T &v) {
            return Slice(reinterpret_cast<const char *>(&v), sizeof v);
        }

        template<typename T>
        T as() const {
            invariant(size() == sizeof(T));
            const T *p = reinterpret_cast<const T *>(data());
            return *p;
        }

        const char *data() const { return _data; }

        char *mutableData() const {
            return _buf.get();
        }

        SharedBuffer& ownedBuf() {
            return _buf;
        }

        size_t size() const { return _size; }

        bool empty() const { return size() == 0; }

        Slice copy() const {
            Slice s(size());
            std::copy(begin(), end(), s.begin());
            return s;
        }

        Slice owned() const {
            if (_buf.get()) {
                return *this;
            } else {
                return copy();
            }
        }

        bool operator==(const Slice &o) const {
            return data() == o.data() && size() == o.size();
        }

        char *begin() { return mutableData(); }
        char *end() { return mutableData() + size(); }
        char *rbegin() { return end(); }
        char *rend() { return begin(); }
        const char *begin() const { return data(); }
        const char *end() const { return data() + size(); }
        const char *rbegin() const { return end(); }
        const char *rend() const { return begin(); }
        const char *cbegin() const { return data(); }
        const char *cend() const { return data() + size(); }
        const char *crbegin() const { return end(); }
        const char *crend() const { return begin(); }

    private:
        SharedBuffer _buf;
        const char *_data;
        size_t _size;
    };

    template<>
    inline Slice Slice::of<BSONObj>(const BSONObj &obj) {
        return Slice(obj.objdata(), obj.objsize());
    }

    template<>
    inline Slice Slice::of<KeyString>(const KeyString &ks) {
        return Slice(ks.getBuffer(), ks.getSize());
    }

    template<>
    inline BSONObj Slice::as<BSONObj>() const {
        return BSONObj(data());
    }

} // namespace mongo

namespace std {

    template<>
    class iterator_traits<mongo::Slice> {
        typedef typename std::iterator_traits<const char *>::difference_type difference_type;
        typedef typename std::iterator_traits<const char *>::value_type value_type;
        typedef typename std::iterator_traits<const char *>::pointer pointer;
        typedef typename std::iterator_traits<const char *>::reference reference;
        typedef typename std::iterator_traits<const char *>::iterator_category iterator_category;
    };

} // namespace std
