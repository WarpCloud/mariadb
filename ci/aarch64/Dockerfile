FROM 172.16.1.99/kundb/aarch64/mysql-compiler:v2 AS compile

COPY . /root/mariadb_src
RUN mkdir /root/mariadb_bld && \
    cd /root/mariadb_bld && \
    cmake ../mariadb_src -DCMAKE_INSTALL_PREFIX=/usr/local/mariadb -DDOWNLOAD_BOOST=1 -DWITH_BOOST=/root/boost/ -DBUILD_CONFIG=mysql_release -DWITH_EMBEDDED_SERVER=false -DWITH_EMBEDDED_SHARED_LIBRARY=false -DWITH_UNIT_TESTS=OFF -DWITHOUT_TOKUDB=1 -DWITH_JEMALLOC=no && \
    make -j 8 && \
    make install && \
    cd /root && rm -fr mariadb_bld mariadb_srcs

FROM 172.16.1.99/kundb/aarch64/mysql_base:v2

COPY --from=compile /usr/local/mariadb /usr/local/mariadb

WORKDIR /usr/local/mariadb
