with import <nixpkgs> {};
let
  buildImage = callPackage ./build-image.nix {};
  runImage = callPackage ./run-image.nix { inherit buildImage; };

  busybox = pkgsMusl.busybox.overrideAttrs (old: {
    CFLAGS = "-pie";
    patches = (old.patches or []) ++ [ ./busybox-mlock.patch ];
  });

  # Runs out-of-memory wit
  samba = (pkgsMusl.samba.override {
    enableRegedit = true;
  }).overrideAttrs (old: {
    patches = old.patches ++ [
      ./musl_uintptr.patch
      ./netdb-defines.patch
    ];
    buildInputs = with pkgsMusl; [
      readline popt iniparser libtirpc
      libbsd libarchive zlib libiconv libunwind
      ncurses
    ];

    nativeBuildInputs = with pkgsMusl; [
      python2 pkgconfig perl gettext
      libxslt docbook_xsl docbook_xml_dtd_42
    ];

    configureFlags = [
      "--with-shared-modules=ALL"
      "--enable-fhs"
      "--sysconfdir=/etc"
      "--localstatedir=/var"
      "--without-ad-dc"
      "--without-ads"
      "--without-systemd"
      "--without-ldap"
      "--without-pam"
    ];
    debugSymbols = false;
  });

  mysql = (pkgsMusl.callPackage ./mysql-5.5.x.nix {}).overrideAttrs (old: {
    patches = [ ./mysql.patch ];
  });
  mysqlDatadir = "/var/lib/mysql";
  fio = pkgsMusl.fio.overrideAttrs (old: {
    patches = (old.patches or []) ++ [
      ./fio-pool-size.patch
    ];
    postConfigure = ''
      sed -i '/#define CONFIG_TLS_THREAD/d' config-host.h
    '';
    configureFlags = [ "--disable-shm" ];
  });

  python-scripts = pkgsMusl.callPackage ./python-scripts {};

  fioCommand = [
    "bin/fio"
    "--output-format=json+"
    "fio-rand-RW.job"
  ];
  iozone = pkgsMusl.iozone.overrideAttrs (attr: {
    # disable gnuplot
    preFixup = "";
    patches = [ ./iozone-max-buffer-size.patch ];
    NIX_CFLAGS_COMPILE = [ "-USHARED_MEM" "-DNO_FORK" ];
  });
  pthread-socket = pkgsMusl.callPackage ./pthread-socket {};
  network-test = pkgsMusl.callPackage ./network-test {};
  latency-test = pkgsMusl.callPackage ./latency-test {};
  write-test = pkgsMusl.callPackage ./write-test {};
in {
  iozone = runImage {
    pkg = iozone;
    command = [ "bin/iozone" ];
  };

  pthread-socket = runImage {
    pkg = pthread-socket;
    command = [ "bin/pthread-socket" ];
  };

  network-test = runImage {
    pkg = network-test;
    command = [ "bin/network-test" ];
  };

  latency-test = runImage {
    pkg = latency-test;
    command = [ "bin/latency-test" ];
  };

  write-test = runImage {
    pkg = write-test;
    command = [ "bin/write-test" ];
  };

  iperf = runImage {
    pkg = pkgsMusl.iperf;
  iperf2 = runImage {
    pkg = iperf2;
    command = [ "bin/iperf" "-s" ];
  };

  iperf2-client = runImage {
    pkg = iperf2;
    command = [ "bin/iperf" "-c" "10.0.2.2" ];
  };

  iperf-native = runImage {
    pkg = pkgsMusl.iperf;
    native = true;
    command = [ "bin/iperf" "-s" ];
  };

  curl-remote = pkgsMusl.curl;

  parallel-iperf = stdenv.mkDerivation {
    name = "parallel-iperf";
    src = ./parallel-iperf.py;
    dontUnpack = true;
    buildInputs = [ python3 ];
    nativeBuildInputs = [ makeWrapper python3.pkgs.wrapPython ];
    makeWrapperArgs = [
      "--prefix" "PATH" ":" "${lib.makeBinPath [ pkgsMusl.iperf] }"
    ];
    installPhase = ''
      install -D -m755 $src $out/bin/parallel-iperf
      ln -s ${pkgsMusl.iperf}/bin/iperf $out/bin/iperf
      patchPythonScript $out/bin/parallel-iperf
    '';
  };

  iperf-client = runImage {
    pkg = pkgsMusl.iperf;
    command = [ "bin/iperf" "-c" ];
  };

  iproute = runImage {
    pkg = pkgsMusl.iproute;
    command = [ "bin/ip" "a" ];
  };

  ping = runImage {
    pkg = busybox;
    command = [ "bin/ping" "10.0.42.1" ];
  };

  ls = runImage {
    pkg = busybox;
    command = [ "bin/ls" "/dev/" ];
  };

  touch = runImage {
    pkg = busybox;
    command = [ "bin/touch" "/mnt/vdb/foobar" ];
  };

  arping = runImage {
    pkg = busybox;
    command = [ "bin/arping" "-I" "eth0" "10.0.2.2" ];
  };

  fio-native = runImage {
    pkg = fio;
    native = true;
    command = fioCommand;
  };

  fio-scone = runImage {
    pkg = lib.makeOverridable ({ stdenv }: stdenv.mkDerivation {
      name = "fio-scone";
      src = null;
      unpackPhase = ":";
      installPhase = ''
        install -D ${./fio-scone-bin} $out/bin/fio
      '';
    }) { inherit stdenv; };
    native = true;
    command = fioCommand;
  };

  fio = runImage {
    pkg = fio;
    command = fioCommand;
  };

  ioping = runImage {
    pkg = pkgsMusl.ioping;
    command = [ "bin/ioping" ];
  };

  hdparm = runImage {
    pkg = busybox;
    command = [ "bin/hdparm" "-Tt" "/dev/spdk0" ];
  };

  hdparm-native = runImage {
    pkg = busybox;
    native = true;
    command = [ "bin/hdparm" "-Tt" "/dev/spdk0" ];
  };

  redis = runImage {
    pkg = pkgsMusl.redis.overrideAttrs (old: {
      makeFlags = old.makeFlags + " MALLOC=libc";
    });
    command = [ "bin/redis-server" ];
  };

  mariadb = runImage {
    pkg = mysql;
    command = [ "bin/mysqld" "--socket=/tmp/mysql.sock" ];
    extraFiles = {
      "/etc/my.cnf" = ''
        [mysqld]
        user=root
        datadir=${mysqlDatadir}
      '';
      "/etc/resolv.conf" = "";
      "/etc/services" = "${iana-etc}/etc/services";
      "/var/lib/mysql/.keep" = "";
      "/run/mysqld/.keep" = "";
    };
    extraCommands = ''
      export PATH=$PATH:${lib.getBin nettools}/bin
      ${mysql}/bin/mysql_install_db --datadir=$(readlink -f root/${mysqlDatadir}) --basedir=${mysql}
      ${mysql}/bin/mysqld_safe --datadir=$(readlink -f root/${mysqlDatadir}) --socket=$TMPDIR/mysql.sock &
      while [[ ! -e $TMPDIR/mysql.sock ]]; do
        sleep 1
      done
      ${mysql}/bin/mysql -u root --socket=$TMPDIR/mysql.sock <<EOF
      GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root' WITH GRANT OPTION;
      CREATE DATABASE root;
      FLUSH PRIVILEGES;
      EOF
   '';
  };

  mariadbPkg = mysql;

  perl = runImage {
    pkg = pkgsMusl.perl;
    command = [ "bin/perl" "-e" "print 'foo\n';" ];
  };

  mariadb-native = runImage {
    pkg = mysql;
    native = true;
    command = [ "bin/mysqld" ];
  };

  samba = runImage {
    pkg = samba;
    command = [ "bin/smbd" "--interactive" "--configfile=/etc/smb.conf" ];
    extraFiles = {
      "/etc/smb.conf" = ''
        registry shares = no

        [Anonymous]
        path = /
        browsable = yes
        writable = yes
        read only = no
        force user = nobody
      '';
      "/var/log/samba/.keep" = "";
      "/var/lock/samba/.keep" = "";
      "/var/lib/samba/private/.keep" = "";
      "/var/run/samba/.keep" = "";
    };
  };

  sysbench = pkgs.sysbench;

  netcat = runImage {
    pkg = pkgsMusl.busybox;
    command = [ "bin/nc" "10.0.42.2" ];
  };

  python-scripts = runImage {
    pkg = pkgsMusl.python3Minimal;
    extraFiles = {
      "/introspect-blocks.py" = builtins.readFile ./python-scripts/introspect-blocks.py;
    };
    command = [ "bin/python3" "introspect-blocks.py" ];
  };

  iotest-image = buildImage {
    pkg = pkgs.callPackage ./dummy.nix {};
    extraFiles = {
      "fio-rand-RW.job" = ''
        [global]
        name=fio-rand-RW
        filename=fio-rand-RW
        rw=randrw
        rwmixread=60
        rwmixwrite=40
        bs=4K
        direct=0
        numjobs=4
        time_based=1
        runtime=10
        thread

        [file1]
        size=15G
        iodepth=16
      '';
      "fio-seq-RW.job" = ''
        [global]
        name=fio-seq-RW
        filename=fio-seq-RW
        rw=rw
        rwmixread=60
        rwmixwrite=40
        bs=256K
        direct=0
        numjobs=4
        time_based=1
        runtime=60
        thread

        [file1]
        size=10G
        iodepth=16
      '';
      "fio-rand-read.job" = ''
        [global]
        name=fio-rand-read
        filename=fio-rand-read
        rw=randrw
        rwmixread=60
        rwmixwrite=40
        bs=4K
        direct=0
        numjobs=4
        time_based=1
        runtime=900
        thread

        [file1]
        size=1G
        iodepth=16
      '';
      "fio-rand-write.job" = ''
        [global]
        name=fio-rand-write
        filename=fio-rand-write
        rw=randwrite
        bs=4K
        direct=0
        numjobs=4
        time_based=1
        runtime=10

        [file1]
        size=1G
        iodepth=16
      '';
      "/etc/my.cnf" = ''
        [mysqld]
        user=root
        datadir=${mysqlDatadir}
      '';
      "/etc/resolv.conf" = "";
      "/etc/services" = "${iana-etc}/etc/services";
      "/var/lib/mysql/.keep" = "";
      "/run/mysqld/.keep" = "";
    };
    extraCommands = ''
      ${mysql}/bin/mysql_install_db --datadir=$(readlink -f root/${mysqlDatadir}) --basedir=${mysql}
      ${mysql}/bin/mysqld_safe --datadir=$(readlink -f root/${mysqlDatadir}) --socket=$TMPDIR/mysql.sock &
      while [[ ! -e $TMPDIR/mysql.sock ]]; do
      sleep 1
      done
      ${mysql}/bin/mysql -u root --socket=$TMPDIR/mysql.sock <<EOF
      GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root' WITH GRANT OPTION;
      CREATE DATABASE root;
      FLUSH PRIVILEGES;
      EOF
    '';
  };

  nginx = runImage {
    pkg = (pkgsMusl.nginx.override {
      gd = null;
      geoip = null;
      libxslt = null;
      withStream = false;
    }).overrideAttrs (old: {
      configureFlags = [
        "--with-file-aio" "--with-threads"
      ];
    });
    command = [ "bin/nginx" "-c" "/etc/nginx.conf" ];
    extraFiles."/var/www/file-3mb".path = runCommand "file-3mb" {} ''
      yes "a" | head -c ${toString (3 * 1024 * 1024)} > $out || true
    '';
    extraFiles."etc/nginx.conf" = ''
      master_process off;
      daemon off;
      error_log stderr;
      events {}
      http {
        aio threads;
        server {
          listen 80;
          default_type text/plain;
          location / {
            return 200 "$remote_addr\n";
          }
          location /test {
            alias /var/www;
          }
        }
      }
    '';
  };
}
