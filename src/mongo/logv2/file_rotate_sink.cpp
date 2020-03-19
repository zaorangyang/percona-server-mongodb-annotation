/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/logv2/file_rotate_sink.h"

#include <boost/filesystem/operations.hpp>
#include <boost/make_shared.hpp>
#include <fmt/format.h>
#include <fstream>

#include "mongo/logv2/shared_access_fstream.h"
#include "mongo/util/string_map.h"


namespace mongo::logv2 {
namespace {
#if _WIN32
using stream_t = Win32SharedAccessOfstream;
#else
using stream_t = std::ofstream;
#endif

StatusWith<boost::shared_ptr<stream_t>> openFile(const std::string& filename, bool append) {
    std::ios_base::openmode mode = std::ios_base::out;
    bool exists = false;
    if (append) {
        mode |= std::ios_base::app;
        exists = boost::filesystem::exists(filename);
    } else
        mode |= std::ios_base::trunc;
    auto file = boost::make_shared<stream_t>(filename, mode);
    if (file->fail())
        return Status(ErrorCodes::FileNotOpen, fmt::format("Failed to open {}", filename));
    if (append && exists)
        file->put('\n');
    return file;
}
}  // namespace

struct FileRotateSink::Impl {
    StringMap<boost::shared_ptr<stream_t>> files;
};

FileRotateSink::FileRotateSink() : _impl(std::make_unique<Impl>()) {}
FileRotateSink::~FileRotateSink() {}

Status FileRotateSink::addFile(const std::string& filename, bool append) {
    auto statusWithFile = openFile(filename, append);
    if (statusWithFile.isOK()) {
        add_stream(statusWithFile.getValue());
        _impl->files[filename] = statusWithFile.getValue();
    }

    return statusWithFile.getStatus();
}
void FileRotateSink::removeFile(const std::string& filename) {
    auto it = _impl->files.find(filename);
    if (it != _impl->files.cend()) {
        remove_stream(it->second);
        _impl->files.erase(it);
    }
}

Status FileRotateSink::rotate(bool rename, StringData renameSuffix) {
    for (auto& file : _impl->files) {
        const std::string& filename = file.first;
        if (rename) {
            std::string renameTarget = filename + renameSuffix;
            try {
                if (boost::filesystem::exists(renameTarget)) {
                    return Status(
                        ErrorCodes::FileRenameFailed,
                        fmt::format("Renaming file {} to {} failed; destination already exists",
                                    filename,
                                    renameTarget));
                }
            } catch (const std::exception& e) {
                return Status(ErrorCodes::FileRenameFailed,
                              fmt::format("Renaming file {} to {} failed; Cannot verify whether "
                                          "destination already exists: {}",
                                          filename,
                                          renameTarget,
                                          e.what()));
            }

            boost::system::error_code ec;
            boost::filesystem::rename(filename, renameTarget, ec);
            if (ec) {
                return Status(
                    ErrorCodes::FileRenameFailed,
                    fmt::format(
                        "Failed  to rename {} to {}: {}", filename, renameTarget, ec.message()));
            }
        }

        auto newFile = openFile(filename, false);
        if (newFile.isOK()) {
            remove_stream(file.second);
            file.second = newFile.getValue();
            add_stream(file.second);
        }
        return newFile.getStatus();
    }

    return Status::OK();
}

}  // namespace mongo::logv2
