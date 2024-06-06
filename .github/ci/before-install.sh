#!/bin/sh

if [ "$CIRRUS_OS" = "linux" ]; then
	apt-get update -qq && \
	apt-get -y install bison autoconf \
				autotools-dev \
				libncurses5-dev \
				pkg-config \
				build-essential \
				libssl-dev \
				libbsd-dev \
				git \
				libtls-dev
fi

if [ "$CIRRUS_OS" = "freebsd" ]; then
	pkg install -y \
		automake \
		pkgconf \
		git \
		libretls
fi

if [ "$CIRRUS_OS" = "darwin" ]; then
	brew install autoconf \
		automake \
		bison \
		pkg-config \
		ncurses \
		git \
		libressl \
		libretls
fi
