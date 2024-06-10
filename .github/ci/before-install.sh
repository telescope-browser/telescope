#!/bin/sh

set -e

case "$(uname)" in
*Linux*)
	set +e
	ID="$(. /etc/os-release && echo $ID)"
	set -e

	case "$ID" in
	alpine)
		apk update
		apk add alpine-sdk autoconf automake pkgconf make byacc \
			libbsd-dev ncurses-dev libretls-dev
		;;
	*)
		# assume debian-derived.
		apt-get update -qq
		apt-get -y install bison autoconf \
				autotools-dev \
				libncurses5-dev \
				pkg-config \
				build-essential \
				libssl-dev \
				libbsd-dev \
				git \
				libtls-dev
		;;
	esac
	;;

*FreeBSD*)
	pkg install -y \
		automake \
		pkgconf \
		git \
		libretls
	;;

*Darwin*)
	brew install autoconf \
		automake \
		bison \
		pkg-config \
		ncurses \
		git \
		libressl \
		libretls
	;;

*)
	echo "unknown operating system $(uname)" >&2
	exit 1
	;;
esac
