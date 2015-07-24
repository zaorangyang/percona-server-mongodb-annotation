/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#pragma once

#include "mongo/base/disallow_copying.h"
#include "mongo/base/string_data.h"
#include "mongo/db/fts/fts_tokenizer.h"
#include "mongo/db/fts/stemmer.h"
#include "mongo/db/fts/tokenizer.h"

namespace mongo {
namespace fts {

class FTSLanguage;
class StopWords;

/**
 * BasicFTSTokenizer
 * A iterator of "documents" where a document contains ASCII space (U+0020) delimited words.
 * Uses
 * - Tokenizer for tokenizing words via ASCII space (ie, U+0020 space).
 * - tolower from the C standard libary to lower letters, ie, it only supports lower casing
 * -     ASCII letters (U+0000 - U+007F)
 * - Stemmer (ie, Snowball Stemmer) to stem words.
 * - Embeded stop word lists for each language in StopWord class
 *
 * For each word returns a stem version of a word optimized for full text indexing.
 * Optionally supports returning case sensitive search terms.
 *
 * BasicFTSTokenizer does not implement the kGenerateDiacriticSensitiveTokens option. All tokens
 * generated by the BasicFTSTokenizer are ineherently diacritic sensitive.
 */
class BasicFTSTokenizer final : public FTSTokenizer {
    MONGO_DISALLOW_COPYING(BasicFTSTokenizer);

public:
    BasicFTSTokenizer(const FTSLanguage* language);

    void reset(StringData document, Options options) override;

    bool moveNext() override;

    StringData get() const override;

private:
    const FTSLanguage* const _language;
    const Stemmer _stemmer;
    const StopWords* const _stopWords;

    std::string _document;
    std::unique_ptr<Tokenizer> _tokenizer;
    Options _options;

    std::string _stem;
};

}  // namespace fts
}  // namespace mongo
