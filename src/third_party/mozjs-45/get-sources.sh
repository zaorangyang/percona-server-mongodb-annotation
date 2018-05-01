#!/bin/sh

# how we got the last firefox sources

VERSION=45.9.0esr
TARBALL=firefox-$VERSION.source.tar.xz
if [ ! -f $TARBALL ]; then
    curl -O "https://ftp.mozilla.org/pub/mozilla.org/firefox/releases/$VERSION/source/$TARBALL"
fi

xzcat $TARBALL | tar -xf-

mv firefox-$VERSION mozilla-release
