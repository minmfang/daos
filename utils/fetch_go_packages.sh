#!/bin/bash

function print_usage {
	echo "fetch_go_packages.sh [-i ipath][-g gopath][-v]" 1>&2
	echo "-i in-tree path to top level DAOS source tree" 1>&2
	echo "-g path to a user specified GOPATH" 1>&2
	echo "-v verify packages at the specified path" 1>&2
	echo "if no path is specified $HOME/go is the default GOPATH" 1>&2
}

#set the default values
GOPATH="$HOME/go"
VERIFY=false
packages=('github.com/satori/go.uuid'
'github.com/zpatrick/go-config'
'github.com/golang/protobuf/protoc-gen-go'
'google.golang.org/grpc')


while getopts ":i:g:vh" opt; do
	case "${opt}" in
		i)
			GOPATH="${OPTARG}/_build.external/go"
			;;
		g)
			GOPATH=${OPTARG}
			;;
		v)
			VERIFY=true
			;;
		h)
			print_usage
			exit 1
			;;
		*)
			print_usage
			exit 1
			;;
        esac
done

REALGOPATH=`realpath -ms $GOPATH`
GOPATH=$REALGOPATH
export GOPATH

echo "Using the GOPATH location of $GOPATH" 1>&2
if [[ $VERIFY = true ]] ; then
	for package in ${packages[@]}; do
		go list ... | grep ${package} > /dev/null
		if [[ $? -ne 0 ]]; then
			echo "Cannot find ${package} in GOPATH=${GOPATH}" 1>&2
			exit 1
		fi
		echo "Found package ${package} in GOPATH" 1>&2
	done
	exit 0
fi

if [[ ! -d $GOPATH ]] ; then
	mkdir -p $GOPATH
fi

for package in ${packages[@]}; do
	echo "Fetching Package: $package" 1>&2
	go get -u $package
done
