#!/usr/bin/env bash

set -e

pushd ptrace
make
popd

SGX_LIBRARY_PATH=$(readlink -f ./ptrace)
GDB_SGX_PLUGIN_PATH=$(readlink -f ./gdb-sgx-plugin)
GDB_PLUGIN=$(readlink -f ./gdb.py)

cat > sgx-lkl-gdb <<EOF
#!/usr/bin/env bash

shopt -s expand_aliases

GDB_SGX_PLUGIN_PATH=$GDB_SGX_PLUGIN_PATH
SGX_LIBRARY_PATH=$SGX_LIBRARY_PATH
GDB_PLUGIN=$GDB_PLUGIN

if command -v gdb
then
    GDB=gdb
elif [ -f /usr/bin/gdb ]
then
    GDB=/usr/bin/gdb
elif [ -f /usr/local/bin/gdb ]
then
    GDB=/usr/local/bin/gdb
else
    echo "No gdb found!"
    exit 1
fi

for arg in "\$@"
do
    if [[ \$arg = *"sgx-lkl-run" ]]; then
        SGX_LKL_RUN=\$arg
        break
    fi
done

if [[ ! -z "\$SGX_LKL_RUN" ]]; then
    SGX_LKL_VERSION=\$(\$SGX_LKL_RUN --version)
    if [[ ! \$SGX_LKL_VERSION = *"DEBUG"* ]]; then
        echo "Warning: \$SGX_LKL_RUN not compiled with DEBUG=true. Debug symbols might be missing."
    fi
fi
export GDB_TCS_PATH=\$(mktemp)
trap "[ -f \$GDB_TCS_PATH ] && rm -f \$GDB_TCS_PATH" EXIT INT TERM
export PYTHONPATH=\$GDB_SGX_PLUGIN_PATH
LD_PRELOAD=\$SGX_LIBRARY_PATH/libsgx_ptrace.so \$GDB -iex "directory \$GDB_SGX_PLUGIN_PATH" -iex "source \$GDB_SGX_PLUGIN_PATH/gdb_sgx_plugin.py" -iex "set environment LD_PRELOAD" -iex "add-auto-load-safe-path /usr/lib" -iex "source \$GDB_PLUGIN" "\$@"
EOF

chmod +x sgx-lkl-gdb
