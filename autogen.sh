aclocal -I m4
autoheader
autoconf
libtoolize --force --copy
automake -a -c --foreign
