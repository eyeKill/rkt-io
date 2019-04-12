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
in {
  iperf = runImage {
    pkg = pkgsMusl.iperf;
    command = [ "bin/iperf" "-s" ];
  };

  iperf-host = pkgs.writeScript "iperf" ''
    #! ${pkgs.runtimeShell} -e
    exec ${pkgsMusl.iperf}/bin/iperf -s
  '';

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
    command = [ "bin/ping" "10.0.2.1" ];
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

  fio = runImage {
    pkg = pkgsMusl.fio.overrideAttrs (old: {
      configureFlags = [ "--disable-shm" ];
    });
    diskSize = "10G";
    extraFiles = (partition: {
      "fio-rand-RW.job" = ''
        [global]
        name=fio-rand-RW
        filename=${partition}/fio-rand-RW
        rw=randrw
        rwmixread=60
        rwmixwrite=40
        bs=4K
        direct=0
        numjobs=4
        time_based=1
        runtime=900

        [file1]
        size=1G
        iodepth=16
      '';
      "fio-seq-RW.job" = ''
        [global]
        name=fio-seq-RW
        filename=${partition}/fio-seq-RW
        rw=rw
        rwmixread=60
        rwmixwrite=40
        bs=256K
        direct=0
        numjobs=4
        time_based=1
        runtime=900

        [file1]
        size=1G
        iodepth=16
      '';
      "fio-rand-read.job" = ''
        [global]
        name=fio-rand-RW
        filename=${partition}/fio-rand-RW
        rw=randrw
        rwmixread=60
        rwmixwrite=40
        bs=4K
        direct=0
        numjobs=4
        time_based=1
        runtime=900

        [file1]
        size=1G
        iodepth=16
      '';
      "fio-rand-write.job" = ''
        [global]
        name=fio-rand-write
        filename=${partition}/fio-rand-write
        rw=randwrite
        bs=4K
        direct=0
        numjobs=4
        time_based=1
        runtime=900

        [file1]
        size=1G
        iodepth=16
      '';}) "/mnt/vdb";
    command = [
      "bin/fio"
      "--output-format=json+"
      "fio-rand-write.job"
    ];
  };

  ioping = runImage {
    pkg = pkgsMusl.ioping;
    command = [ "bin/ioping" ];
  };

  hdparm = runImage {
    pkg = busybox;
    command = [ "bin/hdparm" "-Tt" "/dev/vdb" ];
  };

  redis = runImage {
    pkg = pkgsMusl.redis.overrideAttrs (old: {
      makeFlags = old.makeFlags + " MALLOC=libc";
    });
    command = [ "bin/redis-server" ];
  };

  mariadb = let
    datadir = "/var/lib/mysql";
    mysql = pkgsMusl.mysql55;
  in runImage {
    pkg = mysql.overrideAttrs (old: {
      patches = [ ./mysql.patch ];
    });
    command = [ "bin/mysqld" "--socket=/tmp/mysql.sock" ];
    extraFiles = {
      "/etc/my.cnf" = ''
        [mysqld]
        user=root
        datadir=${datadir}
      '';
      "/etc/resolv.conf" = "";
      "/etc/services" = "${iana-etc}/etc/services";
      "/var/lib/mysql/.keep" = "";
      "/run/mysqld/.keep" = "";
    };
    extraCommands = ''
      export PATH=$PATH:${lib.getBin nettools}/bin
      ${mysql}/bin/mysql_install_db --datadir=$(readlink -f root/${datadir}) --basedir=${mysql}
      ${mysql}/bin/mysqld_safe --datadir=$(readlink -f root/${datadir}) --socket=$TMPDIR/mysql.sock &
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
