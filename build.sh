#!/bin/bash

CMD="$1"

if [[ -z "${CMD}" ]]; then
    export CMD="default"
fi

if [ -x "/usr/bin/mariadb_config" ]; then
    export SQL=" --with-mysql=/usr/bin/mariadb_config"
elif [ -x "/usr/bin/mysql_config" ]; then
    export SQL=" --with-mysql=/usr/bin/mysql_config"
else
    export SQL=""
fi

source src/evol/tools/vars.sh

export COMMON="--enable-packetver=20150000 --enable-debug=gdb${SQL} --enable-epoll"
export CORES=$(cat /proc/cpuinfo|grep processor|wc -l)

autoreconf
if [[ "${CMD}" == "default" || "${CMD}" == "all" ]]; then
    export CC=gcc
    ./configure --enable-manager=no --enable-sanitize=full ${COMMON} CPPFLAGS="${VARS}"
    make -j${CORES}
    make install
    cd src/evol
    ./build.sh
elif [[ "${CMD}" == "old" ]]; then
    ./configure --disable-lto ${COMMON} CPPFLAGS="${VARS}"
    make -j${CORES}
    make install
    cd src/evol
    ./build.sh old
elif [[ "${CMD}" == "valgrind" ]]; then
    ./configure --enable-manager=no ${COMMON} CPPFLAGS="${VARS}"
    make -j${CORES}
    make install
    cd src/evol
    ./build.sh old
elif [[ "${CMD}" == "gprof" ]]; then
    ./configure --enable-manager=no --enable-profiler=gprof ${COMMON} CPPFLAGS="${VARS}"
    make -j${CORES}
    make install
    cd src/evol
    ./build.sh gprof
elif [[ "${CMD}" == "server" ]]; then
    ./configure --enable-sanitize ${COMMON} CPPFLAGS="${VARS}"
    make -j${CORES}
    make install
elif [[ "${CMD}" == "static" ]]; then
    ./configure LIBS="-lmysqlclient -lssl -lcrypto -pthread -lm -lz" --disable-64bit --enable-static ${COMMON}
    make -j${CORES}
elif [[ "${CMD}" == "static64" ]]; then
    ./configure --enable-static ${COMMON} CPPFLAGS="${VARS}"
    make -j${CORES}
fi
exit $?
