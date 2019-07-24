{ stdenv, fetchpatch, fetchurl, cmake, bison, ncurses, openssl
, readline, zlib, perl }:

# Note: zlib is not required; MySQL can use an internal zlib.

stdenv.mkDerivation rec {
  name = "mysql-${version}";
  version = "5.5.62";

  src = fetchurl {
    url = "mirror://mysql/MySQL-5.5/${name}.tar.gz";
    sha256 = "1mwrzwk9ap09s430fpdkyhvx5j2syd3xj2hyfzvanjphq4xqbrxi";
  };

  patches =
    # Minor type error that is a build failure as of clang 6.
    stdenv.lib.optional stdenv.cc.isClang (fetchpatch {
      url = "https://svn.freebsd.org/ports/head/databases/mysql55-server/files/patch-sql_sql_partition.cc?rev=469888";
      extraPrefix = "";
      sha256 = "09sya27z3ir3xy5mrv3x68hm274594y381n0i6r5s627x71jyszf";
    });

  buildInputs = [ cmake bison ncurses openssl readline zlib ];

  enableParallelBuilding = true;

  cmakeFlags = [
    "-DWITH_SSL=yes"
    "-DWITH_READLINE=yes"
    "-DWITH_EMBEDDED_SERVER=yes"
    "-DWITH_ZLIB=yes"
    "-DHAVE_IPV6=yes"
    "-DMYSQL_UNIX_ADDR=/run/mysqld/mysqld.sock"
    "-DMYSQL_DATADIR=/var/lib/mysql"
    "-DINSTALL_SYSCONFDIR=etc/mysql"
    "-DINSTALL_INFODIR=share/mysql/docs"
    "-DINSTALL_MANDIR=share/man"
    "-DINSTALL_PLUGINDIR=lib/mysql/plugin"
    "-DINSTALL_SCRIPTDIR=bin"
    "-DINSTALL_INCLUDEDIR=include/mysql"
    "-DINSTALL_DOCREADMEDIR=share/mysql"
    "-DINSTALL_SUPPORTFILESDIR=share/mysql"
    "-DINSTALL_MYSQLSHAREDIR=share/mysql"
    "-DINSTALL_DOCDIR=share/mysql/docs"
    "-DINSTALL_SHAREDIR=share/mysql"
    "-DINSTALL_MYSQLTESTDIR="
    "-DINSTALL_SQLBENCHDIR="
  ];

  NIX_CFLAGS_COMPILE =
    stdenv.lib.optionals stdenv.cc.isGNU [ "-fpermissive" ] # since gcc-7
    ++ stdenv.lib.optionals stdenv.cc.isClang [ "-Wno-c++11-narrowing" ]; # since clang 6

  NIX_LDFLAGS = stdenv.lib.optionalString stdenv.isLinux "-lgcc_s";

  prePatch = ''
    sed -i -e "s|/usr/bin/libtool|libtool|" cmake/libutils.cmake
  '';
  postInstall = ''
    sed -i -e "s|basedir=\"\"|basedir=\"$out\"|" $out/bin/mysql_install_db
    rm -r $out/data "$out"/lib/*.a
  '';
}
