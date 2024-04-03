#!/bin/sh

myname=libnpupnp
MYNAME=`echo $myname | tr a-z A-Z`
wherefile=inc/upnpdescription.h
islib=yes
builtlib=build/libnpupnp.so

# A shell-script to make a source distribution.

fatal() {
    echo $*
    exit 1
}
usage() {
    echo 'Usage: makescrdist.sh -t -s do_it'
    echo ' -t : no tagging'
    echo ' -s : snapshot release: use date instead of VERSION'
    echo ' -s implies -t'
    exit 1
}

create_tag() {
    git tag -f -a $1 -m "Release $1 tagged"  || fatal tag failed
}
test_tag() {
    git tag -l | egrep '^'$1'$'
}

#set -x

TAR=${TAR-/bin/tar}

if test ! -f $tfile;then
    echo "Should be executed in the top source directory"
    exit 1
fi
targetdir=${targetdir-/tmp}
dotag=${dotag-yes}
snap=${snap-no}

while getopts ts o
do	case "$o" in
	t)	dotag=no;;
	s)	snap=yes;dotag=no;;
	[?])	usage;;
	esac
done
shift `expr $OPTIND - 1`

test $dotag = "yes" -a $snap = "yes" && usage

test $# -eq 1 || usage

echo dotag $dotag snap $snap

if test $snap = yes ; then
  version=`date +%F_%H-%M-%S`
  TAG=""
else
    version=`grep "version:" meson.build | head -1 | awk '{print $2}' | tr -d "',"`
    # trim whitespace
    version=`echo $version | xargs`
    TAG="${myname}-v$version"
fi

if test "$dotag" = "yes" ; then
    echo Creating AND TAGGING version $version
    test_tag $TAG  && fatal "Tag $TAG already exists"
else
    echo Creating version $version, no tagging
fi
sleep 2
	

editedfiles="`git status -s |\
egrep -v 'makesrcdist.sh|excludefile|manifest.txt'`"
if test "$dotag" = "yes" -a ! -z "$editedfiles"; then
    fatal  "Edited files exist: " $editedfiles
fi


if test $dotag = yes ; then
    releasename=${myname}-$version
else
    releasename=beta${myname}-$version
fi

topdir=$targetdir/$releasename
if test ! -d $topdir ; then
    mkdir $topdir || exit 1
else 
    echo "Removing everything under $topdir Ok ? (y/n)"
    read rep 
    if test $rep = 'y';then
    	rm -rf $topdir/*
    fi
fi

# Check for symbol changes. The symbols-reference file should have
# been adjusted (and the soversion possibly changed) before running
# this.
if test "$islib" = "yes" ; then 
    if test "$dotag" = "yes" ; then
        meson setup build || exit 1
        meson configure build --buildtype release || exit 1
        (cd build;meson compile) || exit 1
        nm -g --defined-only --demangle $builtlib | grep ' T ' | \
            awk '{$1=$2="";print $0}' | diff symbols-reference - || exit 1
        rm -rf build
    fi
fi

$TAR chfX - excludefile .  | (cd $topdir;$TAR xf -)

out=$releasename.tar.gz
(cd $targetdir ; \
    $TAR chf - $releasename | \
    	gzip > $out)
echo "$targetdir/$out created"

# Check manifest against current reference
export LC_ALL=C
tar tzf $targetdir/$out | sort | cut -d / -f 2- | \
    diff manifest.txt - || fatal "Please fix file list manifest.txt"

# We tag .. as there is the 'packaging/' directory in there
[ $dotag = "yes" ] && create_tag $TAG
