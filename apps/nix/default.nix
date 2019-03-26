with import <nixpkgs> {};
let
  buildImage = callPackage ./build-image.nix {};
  runImage = callPackage ./run-image.nix { inherit buildImage; };

  busybox = pkgsMusl.busybox.overrideAttrs (old: {
    CFLAGS = "-pie";
    patches = (old.patches or []) ++ [ ./busybox-mlock.patch ];
  });

in {
  iperf = runImage {
    pkg = pkgsMusl.iperf;
    command = [ "bin/iperf" "-s" ];
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
        runtime=900

        [file1]
        size=1G
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
        runtime=900

        [file1]
        size=1G
        iodepth=16
      '';
      "fio-rand-read.job" = ''
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
        runtime=900

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
        runtime=900

        [file1]
        size=1G
        iodepth=16
      '';
    };
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

  nginx = runImage {
    pkg = pkgsMusl.nginx.override {
      gd = null;
    };
    command = [ "bin/nginx" "-c" "/etc/nginx.conf" ];
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
          return 200 "$remote_addr\n";
        }
      }
    '';
  };
}
