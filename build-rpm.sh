#!/bin/bash

set -e

PKG=pulseaudio
VERSION=`date +'%Y%m%d'`
TMPDIR=/tmp/$USER
TMP=$TMPDIR/$PKG-$VERSION

# roll a tarball by hand
function manual_tarball() {
    echo "Building RPM from working copy..."
    rm -fr $TMP
    mkdir -p $TMP && \
        tar -cf - . | tar -C $TMP -xvf - && \
        tar -C $TMPDIR -cvzf $PKG-$VERSION.tar.gz $PKG-$VERSION && \
    mv $PKG-$VERSION.tar.gz ..
}

# roll a tarball by git
function git_tarball() {
    local _version="${1:-HEAD}"

    echo "Building RPM from git $_version..."
    git archive --prefix=$PKG-$VERSION/ $_version > ../$PKG-$VERSION.tar
    gzip ../$PKG-$VERSION.tar
}

# roll a tarball
function make_tarball() {
    if [ -d .git -a "$1" != "current" ]; then
        git_tarball $1
    else
        manual_tarball
    fi
}

# generate tarball
make_tarball ${1:-current}

# patch up spec file
sed "s/@VERSION@/$VERSION/g" $PKG.spec.in > ../$PKG.spec

# put it all in place and try to build rpm(s)
mv ../$PKG-$VERSION.tar.gz ~/rpmbuild/SOURCES
mv ../$PKG.spec ~/rpmbuild/SPECS

patches="`grep -i '^patch[0-9]*:' ~/rpmbuild/SPECS/$PKG.spec | \
              tr -s '\t' ' ' | cut -d ' ' -f 2`"
if [ -n "$patches" ]; then
    cp $patches ~/rpmbuild/SOURCES
fi

sources="`grep -i '^source[0-9]*:' ~/rpmbuild/SPECS/$PKG.spec | \
              grep -iv '^source0:' | tr -s '\t' ' ' | cut -d ' ' -f 2`"
if [ -n "$sources" ]; then
    cp $sources ~/rpmbuild/SOURCES
fi

rpmbuild -bb ~/rpmbuild/SPECS/$PKG.spec
