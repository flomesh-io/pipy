#!/bin/bash

# The following variables influence the operation of this build script:
# Argument -f: Soft clean, ensuring re-build of oqs-provider binary
# Argument -F: Hard clean, ensuring checkout and build of all dependencies
# EnvVar CMAKE_PARAMS: passed to cmake
# EnvVar MAKE_PARAMS: passed to invocations of make; sample value: "-j"
# EnvVar OQSPROV_CMAKE_PARAMS: passed to invocations of oqsprovider cmake
# EnvVar LIBOQS_BRANCH: Defines branch/release of liboqs; default value "main"
# EnvVar OQS_ALGS_ENABLED: If set, defines OQS algs to be enabled, e.g., "STD"
# EnvVar OPENSSL_INSTALL: If set, defines (binary) OpenSSL installation to use
# EnvVar OPENSSL_BRANCH: Defines branch/release of openssl; if set, forces source-build of OpenSSL3
#        Setting this to feature/dtls-1.3 enables build&test of all PQ algs using DTLS1.3 feature branch
# EnvVar OSSL_CONFIG: If set, passes arguments to the "./config" command that precedes the "make" of the OpenSSL
# EnvVar liboqs_DIR: If set, needs to point to a directory where liboqs has been installed to

if [[ "$OSTYPE" == "darwin"* ]]; then
   SHLIBEXT="dylib"
   STATLIBEXT="dylib"
else
   SHLIBEXT="so"
   STATLIBEXT="a"
fi

if [ $# -gt 0 ]; then
   if [ "$1" == "-f" ]; then
      rm -rf _build
   fi
   if [ "$1" == "-F" ]; then
      rm -rf _build openssl liboqs .local
   fi
fi

if [ -z "$LIBOQS_BRANCH" ]; then
   export LIBOQS_BRANCH=main
fi

if [ -z "$OQS_ALGS_ENABLED" ]; then
   export DOQS_ALGS_ENABLED=""
else
   export DOQS_ALGS_ENABLED="-DOQS_ALGS_ENABLED=$OQS_ALGS_ENABLED"
fi

if [ -z "$OQS_LIBJADE_BUILD" ]; then
   export DOQS_LIBJADE_BUILD="-DOQS_LIBJADE_BUILD=OFF"
else
   export DOQS_LIBJADE_BUILD="-DOQS_LIBJADE_BUILD=$OQS_LIBJADE_BUILD"
fi

if [ -z "$OPENSSL_INSTALL" ]; then
 openssl version | grep "OpenSSL 3" > /dev/null 2>&1
 #if [ \($? -ne 0 \) -o \( ! -z "$OPENSSL_BRANCH" \) ]; then
 if [ $? -ne 0 ] || [ ! -z "$OPENSSL_BRANCH" ]; then
   if [ -z "$OPENSSL_BRANCH" ]; then
      export OPENSSL_BRANCH="master"
   fi
   # No OSSL3 installation given/found, or specific branch build requested
   echo "OpenSSL3 to be built from source at branch $OPENSSL_BRANCH."

   if [ ! -d "openssl" ]; then
      echo "openssl not specified and doesn't reside where expected: Cloning and building..."
      # for full debug build add: enable-trace enable-fips --debug
      export OSSL_PREFIX=`pwd`/.local && git clone --depth 1 --branch $OPENSSL_BRANCH https://github.com/openssl/openssl.git && cd openssl && LDFLAGS="-Wl,-rpath -Wl,${OSSL_PREFIX}/lib64" ./config $OSSL_CONFIG --prefix=$OSSL_PREFIX && make $MAKE_PARAMS && make install_sw install_ssldirs && cd ..
      if [ $? -ne 0 ]; then
        echo "openssl build failed. Exiting."
        exit -1
      else
         # some cmake versions don't look in "lib64", so aid their search with this softlink
         cd $OSSL_PREFIX && if [ -d "lib64" ]; then ln -s lib64 lib; fi && cd ..
         export OPENSSL_INSTALL=$OSSL_PREFIX
      fi
   else
      if [ -d ".local" ]; then
          export OPENSSL_INSTALL=`pwd`/.local
      fi
   fi
 fi
fi

# Check whether liboqs is built or has been configured:
if [ -z $liboqs_DIR ]; then
 if [ ! -f ".local/lib/liboqs.$STATLIBEXT" ]; then
  echo "need to re-build static liboqs..."
  if [ ! -d liboqs ]; then
    echo "cloning liboqs $LIBOQS_BRANCH..."
    git clone --depth 1 --branch $LIBOQS_BRANCH https://github.com/open-quantum-safe/liboqs.git
    if [ $? -ne 0 ]; then
      echo "liboqs clone failure for branch $LIBOQS_BRANCH. Exiting."
      exit -1
    fi
    if [ "$LIBOQS_BRANCH" != "main" ]; then
      # check for presence of backwards-compatibility generator file
      if [ -f oqs-template/generate.yml-$LIBOQS_BRANCH ]; then
        pip install -r oqs-template/requirements.txt
        echo "generating code for $LIBOQS_BRANCH"
        mv oqs-template/generate.yml oqs-template/generate.yml-main
        cp oqs-template/generate.yml-$LIBOQS_BRANCH oqs-template/generate.yml
        LIBOQS_SRC_DIR=`pwd`/liboqs python3 oqs-template/generate.py
        if [ $? -ne 0 ]; then
           echo "Code generation failure for $LIBOQS_BRANCH. Exiting."
           exit -1
        fi
      fi
    fi
  fi

  # Ensure liboqs is built against OpenSSL3, not a possibly still system-
  # installed OpenSSL111: We otherwise have mismatching symbols at runtime
  # (detected particularly late when building shared)
  if [ ! -z $OPENSSL_INSTALL ]; then
    export CMAKE_OPENSSL_LOCATION="-DOPENSSL_ROOT_DIR=$OPENSSL_INSTALL"
  else
    # work around for cmake 3.23.3 regression not finding OpenSSL:
    export CMAKE_OPENSSL_LOCATION="-DOPENSSL_ROOT_DIR="
  fi
  # for full debug build add: -DCMAKE_BUILD_TYPE=Debug
  # to optimize for size add -DOQS_ALGS_ENABLED= suitably to one of these values:
  #    STD: only include NIST standardized algorithms
  #    NIST_R4: only include algorithms in round 4 of the NIST competition
  #    All: include all algorithms supported by liboqs (default)
  cd liboqs && cmake -GNinja $CMAKE_PARAMS $DOQS_ALGS_ENABLED $CMAKE_OPENSSL_LOCATION $DOQS_LIBJADE_BUILD -DCMAKE_INSTALL_PREFIX=$(pwd)/../.local -S . -B _build && cd _build && ninja && ninja install && cd ../..
  if [ $? -ne 0 ]; then
      echo "liboqs build failed. Exiting."
      exit -1
  fi
 fi
 export liboqs_DIR=$(pwd)/.local
fi

# Check whether provider is built:
if [ ! -f "_build/lib/oqsprovider.$SHLIBEXT" ]; then
   echo "oqsprovider (_build/lib/oqsprovider.$SHLIBEXT) not built: Building..."
   # for full debug build add: -DCMAKE_BUILD_TYPE=Debug
   #BUILD_TYPE="-DCMAKE_BUILD_TYPE=Debug"
   BUILD_TYPE=""
   # for omitting public key in private keys add -DNOPUBKEY_IN_PRIVKEY=ON
   if [ -z "$OPENSSL_INSTALL" ]; then
       cmake $CMAKE_PARAMS $CMAKE_OPENSSL_LOCATION $BUILD_TYPE $OQSPROV_CMAKE_PARAMS -S . -B _build && cmake --build _build
   else
       cmake $CMAKE_PARAMS -DOPENSSL_ROOT_DIR=$OPENSSL_INSTALL $BUILD_TYPE $OQSPROV_CMAKE_PARAMS -S . -B _build && cmake --build _build
   fi
   if [ $? -ne 0 ]; then
     echo "provider build failed. Exiting."
     exit -1
   fi
fi

