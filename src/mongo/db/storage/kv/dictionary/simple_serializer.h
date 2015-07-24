// simple_serializer.h

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

#if __cplusplus >= 201103L
#include <type_traits>
#endif

// TODO: This probably belongs in mongo/util.

namespace mongo {

    /**
     * Convenience class for writing object data to a char buffer.
     */
    class BufferWriter {
    public:
        explicit BufferWriter(char *dest)
            : _dest(dest)
        {}

        char *get() const { return _dest; }

        /**
         * Write a `T' into the buffer and advance the internal buffer
         * position by sizeof(T) bytes.
         *
         * Requires: the `dest' buf has enough space for writing.
         */
        template<typename T>
        BufferWriter& write(const T &val) {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "Type for BufferWriter::write must be trivially copyable");
#endif
            T *tdest = reinterpret_cast<T *>(_dest);
            _dest += sizeof *tdest;
            *tdest = val;
            return *this;
        }

    private:
        char *_dest;
    };

    /**
     * Convenience class for reading object data from a char buffer.
     */
    class BufferReader {
    public:
        explicit BufferReader(const char *src)
            : _src(src)
        {}

        const char *get() const { return _src; }

        /**
         * Read and return a `T' from the buffer and advance the internal
         * buffer position by sizeof(T);
         *
         * Requires: the `src' buf has more data for reading.
         */
        template<typename T>
        T read() {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "Type for BufferReader::read must be trivially copyable");
#endif
            const T *tsrc = reinterpret_cast<const T *>(_src);
            _src += sizeof *tsrc;
            return *tsrc;
        }

    private:
        const char *_src;
    };

} // namespace mongo
