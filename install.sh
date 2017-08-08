#!/bin/sh

apt-get -qq update && \
apt-get install -y -qq curl ca-certificates bash && \
apt-get install -y -qq --reinstall lsb-base lsb-release && \
apt-get install -y -qq \
uuid-dev \
build-essential autoconf libtool \
git \
pkg-config \
automake \
flex \
bison \
sudo && \
apt-get clean && \
rm -rf /var/lib/apt/lists/*

git clone -b v1.0.0 https://github.com/grpc/grpc /var/local/git/grpc

cd /var/local/git/grpc && \
git submodule update --init && \
make && \
make install && make clean

cd /var/local/git/grpc/third_party/protobuf && \
make && make install && make clean

git clone -b write_gsc https://github.com/cramertj/collectd.git /var/local/git/collectd

cd /var/local/git/collectd && \
./build.sh && \
./configure && \
make && \
sudo make install
