#!/bin/sh

# how we got the last firefox sources

wget "https://ftp.mozilla.org/pub/mozilla.org/firefox/releases/38.6.1esr/source/firefox-38.6.1esr.source.tar.bz2"

tar -jxf firefox-38.6.1esr.source.tar.bz2

mv mozilla-esr38 mozilla-release
