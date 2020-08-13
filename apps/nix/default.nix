with import (builtins.fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/5e6825612c9114c12eb9a99c0b42a5aba6289908.tar.gz";
  sha256 = "1mxs4z9jgcvi7gliqxc77k16fv22ry42rqgss5mnqyls4p2zk3c7";
}) {};

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
    src = ./fio-src;
    patches = (old.patches or []) ++ [
      ./fio-pool-size.patch
    ];
    postConfigure = ''
      sed -i '/#define CONFIG_TLS_THREAD/d' config-host.h
    '';
    configureFlags = [ "--disable-shm" ];
  });

  fio-graphene = pkgs.fio.overrideAttrs (old: {
    src = ./fio-src;
    patches = (old.patches or []) ++ [
      ./fio-pool-size.patch
    ];
    buildInputs = [];
    postConfigure = ''
      sed -i '/#define CONFIG_TLS_THREAD/d' config-host.h
    '';
    configureFlags = [ "--disable-shm" ];
  });

  #fio-scone = pkgsMusl.fio;
  fio-scone = fio.override {
    stdenv = sconeStdenv;
  };
  #iperf3-scone = pkgs.callPackage ./iperf {
  #  stdenv = sconeStdenv;
  #  enableStatic = true;
  #};

  python-scripts = pkgsMusl.callPackage ./python-scripts {};

  iperf2 = pkgsMusl.iperf2.overrideAttrs (old: {
    src = ./iperf-2.0.13;
    configureFlags = (old.configureFlags or []) ++ [ "--enable-ipv6" "--disable-threads" ];
  });

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

  iperf3 = pkgsMusl.callPackage ./iperf {};
  iperf3-scone = pkgs.callPackage ./iperf {
    stdenv = sconeStdenv;
    enableStatic = true;
  };
  iperf3-graphene = pkgs.callPackage ./iperf {};
  hello-graphene = pkgs.callPackage ./hello {};

  inherit (pkgs.callPackages ./scone {})
    scone-cc sconeStdenv sconeEnv scone-unwrapped;
  inherit (pkgs.callPackages ./graphene {}) runGraphene;
  sgx-lkl = pkgs.callPackage ./sgx-lkl {};

  pthread-socket = pkgsMusl.callPackage ./pthread-socket {};
  network-test = pkgsMusl.callPackage ./network-test {};
  latency-test = pkgsMusl.callPackage ./latency-test {};

  simpleio-musl = pkgsMusl.callPackage ./simpleio {};
  simpleio-scone = simpleio-musl.override {
    stdenv = sconeStdenv;
  };
  nginx = (pkgsMusl.nginx.override {
    gd = null;
    geoip = null;
    libxslt = null;
    withStream = false;
  }).overrideAttrs (old: {
    configureFlags = [
      "--with-file-aio" "--with-threads"
      "--http-log-path=/var/log/nginx/access.log"
      "--error-log-path=/var/log/nginx/error.log"
      "--pid-path=/var/log/nginx/nginx.pid"
      "--http-client-body-temp-path=/var/cache/nginx/"
      "--http-proxy-temp-path=/var/cache/nginx/proxy"
      "--http-fastcgi-temp-path=/var/cache/nginx/fastcgi"
      "--http-uwsgi-temp-path=/var/cache/nginx/uwsgi"
      "--http-scgi-temp-path=/var/cache/nginx/scgi"
    ];
  });
in {
  musl = pkgs.musl;

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

  simpleio-sgx-io = runImage {
    pkg = simpleio-musl;
    command = [ "bin/simpleio" ];
  };

  simpleio-sgx-lkl = runImage {
    pkg = simpleio-musl;
    #sgx-lkl-run = toString ../../../sgx-lkl-org/build/sgx-lkl-run;
    sgx-lkl-run = "${sgx-lkl}/bin/sgx-lkl-run";
    command = [ "bin/simpleio" ];
  };

  simpleio-scone = runImage {
    pkg = simpleio-scone;
    native = true;
    command = [ "bin/simpleio" ];
  };

  simpleio-native = runImage {
    pkg = simpleio-musl;
    native = true;
    command = [ "bin/simpleio" ];
  };

  iperf = runImage {
    pkg = iperf3;
    command = [ "bin/iperf" "4" ];
  };

  iperf3-sgx-lkl = runImage {
    pkg = iperf3;
    # debugging
    #sgx-lkl-run = toString ../../../sgx-lkl-org/build/sgx-lkl-run;
    sgx-lkl-run = "${sgx-lkl}/bin/sgx-lkl-run";
    command = [ "bin/iperf" "1" ];
  };

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

  inherit iperf3-scone sconeStdenv sconeEnv;

  # provides scone command
  scone = scone-unwrapped;

  iperf3-graphene = runGraphene {
    pkg = iperf3-graphene;
    command = ["bin/iperf3"];
    # our iperf binds each core to a different port
    ports = lib.range 5201 5299;
  };

  fio-graphene = runGraphene {
    pkg = fio-graphene;
    command = ["bin/fio"];
  };


  hello-graphene = runGraphene {
    pkg = hello-graphene;
    command = ["bin/hello"];
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

  dd = runImage {
    pkg = busybox;
    command = [ "bin/dd" "if=/dev/spdk0" "of=/dev/null" ];
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

  fio-sgx-io = runImage {
    pkg = fio;
    command = fioCommand;
  };

  fio-sgx-lkl = runImage {
    pkg = fio;
    sgx-lkl-run = "${sgx-lkl}/bin/sgx-lkl-run";
    command = [ "bin/fio" ];
  };

  fio-native = runImage {
    pkg = fio;
    native = true;
    command = fioCommand;
  };

  fio-scone = runImage {
    pkg = fio-scone;
    native = true;
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

  redis-native = runImage {
    pkg = pkgsMusl.redis.overrideAttrs (old: {
      makeFlags = old.makeFlags ++ [ "MALLOC=libc" ];
      srcs = "/home/harshanavkis/redis-6.0.6";
      #src = fetchurl {
      #  url    = "http://download.redis.io/releases/redis-6.0.6.tar.gz";
      #  sha256 = "12ad49b163af5ef39466e8d2f7d212a58172116e5b441eebecb4e6ca22363d94";
      #};
    });
    command = [ "bin/redis-server" ];
    native = true;
  };
  
  redis = runImage {
    pkg = pkgsMusl.redis.overrideAttrs (old: {
      name = "redis-6.0.6";
      buildInputs = [ ]; # no lua/systemd
      nativeBuildInputs = [ pkg-config ];
      makeFlags = [ "MALLOC=libc" "PREFIX=$(out)" ];
      src = fetchurl {
        url    = "http://download.redis.io/releases/redis-6.0.6.tar.gz";
        sha256 = "151x6qicmrmlxkmiwi2vdq8p50d52b9gglp8csag6pmgcfqlkb8j";
      };
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
      "/var/www/file-3mb".path = runCommand "file-3mb" {} ''
        yes "a" | head -c ${toString (3 * 1024 * 1024)} > $out || true
      '';
      "etc/nginx.conf" = ''
        master_process off;
        daemon off;
        error_log stderr;
        events {}
        http {
          access_log off;
          aio threads;
          server {
            listen 9000;
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

  nginx-native = runImage {
    pkg = nginx;
    command = [ "bin/nginx" "-c" "/tmp/mnt/etc/nginx.conf" ];
    extraFiles."/var/www/file-3mb".path = runCommand "file-3mb" {} ''
      yes "a" | head -c ${toString (3 * 1024 * 1024)} > $out || true
    '';
    native = true;
  };

  ycsb = pkgs.callPackage ./ycsb {};

  nginx-scone = runImage {
    pkg = (pkgsMusl.nginx.override {
      gd = null;
      geoip = null;
      libxslt = null;
      withStream = false;
      stdenv = sconeStdenv;
    }).overrideAttrs (old: {
      configureFlags = [
        "--with-file-aio" "--with-threads"
        "--http-log-path=/var/log/nginx/access.log"
        "--error-log-path=/var/log/nginx/error.log"
        "--pid-path=/var/log/nginx/nginx.pid"
        "--http-client-body-temp-path=/var/cache/nginx/client_body"
        "--http-proxy-temp-path=/var/cache/nginx/proxy"
        "--http-fastcgi-temp-path=/var/cache/nginx/fastcgi"
        "--http-uwsgi-temp-path=/var/cache/nginx/uwsgi"
        "--http-scgi-temp-path=/var/cache/nginx/scgi"
      ];
      buildInputs = [
        ((pkgsStatic.pcre.override {
          stdenv = sconeStdenv;
        }).overrideAttrs (old: {
          configureFlags = old.configureFlags ++ [
            "--disable-shared"
          ];
          # disable-shared breaks the tests
          doCheck = false;
        }))
        (pkgsStatic.zlib.override {
          stdenv = sconeStdenv;
        })
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
        access_log off;
        aio threads;
        server {
          listen 9000;
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
    native = true;
  };  

  nginx = runImage {
    pkg = nginx;
    command = [ "bin/nginx" "-c" "/etc/nginx.conf" ];
    # sgx-io: /mnt/spdk0 /mnt/spdk1
    # scone/native/sgx-lkl: /mnt/nvme
    extraFiles."/var/cache/nginx/.keep" = "";
    extraFiles."/var/log/nginx/.keep" = "";
    extraFiles."etc/nginx.conf" = ''
       master_process off;
       daemon off;
       error_log stderr;
       events {}
       http {
         access_log off;
         aio threads;
         server {
           listen 9000;
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

  wrk-bench = pkgsMusl.wrk;
}
