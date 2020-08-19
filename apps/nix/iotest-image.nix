{ stdenv
, buildImage
, runCommand
, iana-etc
, mysql
, mysqlDatadir
, callPackage
, sconeEncryptedDir ? null
}:
buildImage {
  pkg = callPackage ./dummy.nix {};
  inherit sconeEncryptedDir;
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
      numjobs=8
      time_based=1
      runtime=300
      thread

      [file1]
      size=2G
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
      runtime=300
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
}
