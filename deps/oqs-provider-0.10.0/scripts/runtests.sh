#!/bin/sh

set -e

rv=0

provider2openssl() {
    echo
    echo "Testing oqsprovider->oqs-openssl interop for $1:"
    "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-certgen.sh" "$1" && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-cmssign.sh" "$1" sha3-384 && "${OQS_PROVIDER_TESTSCRIPTS}/oqs-openssl-certverify.sh" "$1" && "${OQS_PROVIDER_TESTSCRIPTS}/oqs-openssl-cmsverify.sh" "$1"
}

openssl2provider() {
    echo
    echo "Testing oqs-openssl->oqsprovider interop for $1:"
    "${OQS_PROVIDER_TESTSCRIPTS}/oqs-openssl-certgen.sh" "$1" && "${OQS_PROVIDER_TESTSCRIPTS}/oqs-openssl-cmssign.sh" "$1" && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-certverify.sh" "$1" && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-cmsverify.sh" "$1"
}

localalgtest() {
    if ! ( "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-certgen.sh" "$1" >> interop.log 2>&1 && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-certverify.sh" "$1" >> interop.log 2>&1 && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-cmssign.sh" "$1" >> interop.log 2>&1 && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-ca.sh" "$1" >> interop.log 2>&1 && "${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-pkcs12gen.sh" "$1" >> interop.log 2>&1 ); then
        echo "localalgtest $1 failed. Exiting.".
        cat interop.log
        exit 1
    fi
}

interop() {
    echo ".\c"
    # check if we want to run this algorithm:
    if [ -n "${OQS_SKIP_TESTS}" ]; then
        GREPTEST=$(echo "${OQS_SKIP_TESTS}" | sed "s/\,/\\\|/g")
        if echo "$1" | grep -q "${GREPTEST}"; then
            echo "Not testing $1" >> interop.log
            return
        fi
    fi

    # Check whether algorithm is supported at all:
    retcode=0
    "${OPENSSL_APP}" list -signature-algorithms | grep -q "$1" || retcode=$?
    if [ "${retcode}" -ne 1 ]; then
	if [ -z "${LOCALTESTONLY}" ]; then
            provider2openssl "$1" >> interop.log 2>&1 && openssl2provider "$1" >> interop.log 2>&1
	else
            localalgtest "$1"
        fi
    else
        echo "Algorithm $1 not enabled. Exit testing."
        exit 1
    fi

    if [ "${retcode}" -ne 0 ]; then
        echo "Test for $1 failed. Terminating testing."
        cat interop.log
        exit 1
    fi
}

if [ -z "${OQS_PROVIDER_TESTSCRIPTS}" ]; then
    export OQS_PROVIDER_TESTSCRIPTS="$(pwd)/scripts"
fi

if [ -n "${OPENSSL_INSTALL}" ]; then
    # trying to set config variables suitably for pre-existing OpenSSL installation
    if [ -f "${OPENSSL_INSTALL}/bin/openssl" ]; then
        export OPENSSL_APP="${OPENSSL_INSTALL}/bin/openssl"
    fi
    if [ -z "${LD_LIBRARY_PATH}" ]; then
        if [ -d "${OPENSSL_INSTALL}/lib64" ]; then
            export LD_LIBRARY_PATH="${OPENSSL_INSTALL}/lib64"
        elif [ -d "${OPENSSL_INSTALL}/lib" ]; then
            export LD_LIBRARY_PATH="${OPENSSL_INSTALL}/lib"
        fi
    fi
    if [ -f "${OPENSSL_INSTALL}/ssl/openssl.cnf" ]; then
        export OPENSSL_CONF="${OPENSSL_INSTALL}/ssl/openssl.cnf"
    fi
fi

if [ -z "${OPENSSL_CONF}" ]; then
    export OPENSSL_CONF="$(pwd)/scripts/openssl-ca.cnf"
fi

if [ -z "${OPENSSL_APP}" ]; then
    if [ -f "$(pwd)/openssl/apps/openssl" ]; then
        export OPENSSL_APP="$(pwd)/openssl/apps/openssl"
    else # if no local openssl src directory is found, rely on PATH...
        export OPENSSL_APP=openssl
    fi
fi

if [ -z "${OPENSSL_MODULES}" ]; then
    export OPENSSL_MODULES="$(pwd)/_build/lib"
fi

if [ -z "${LD_LIBRARY_PATH}" ]; then
    if [ -d "$(pwd)/.local/lib64" ]; then
        export LD_LIBRARY_PATH="$(pwd)/.local/lib64"
    else
        if [ -d "$(pwd)/.local/lib" ]; then
            export LD_LIBRARY_PATH="$(pwd)/.local/lib"
        fi
    fi
fi

if [ -n "${OQS_SKIP_TESTS}" ]; then
   echo "Skipping algs ${OQS_SKIP_TESTS}"
fi

# Set OSX DYLD_LIBRARY_PATH if not already externally set
if [ -z "${DYLD_LIBRARY_PATH}" ]; then
    export DYLD_LIBRARY_PATH="${LD_LIBRARY_PATH}"
fi

echo "Test setup:"
echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
echo "OPENSSL_APP=${OPENSSL_APP}"
echo "OPENSSL_CONF=${OPENSSL_CONF}"
echo "OPENSSL_MODULES=${OPENSSL_MODULES}"
if uname -s | grep -q "^Darwin"; then
echo "DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}"
fi

# check if we can use docker or not:
if ! docker info 2>&1 | grep -q Server; then
   echo "No OQS-OpenSSL111 interop test because of absence of docker"
   export LOCALTESTONLY="Yes"
fi

# by default, do not run interop tests as per
# https://github.com/open-quantum-safe/oqs-provider/issues/32
# comment the following line if they should be run; be sure to
# have alignment in algorithms supported in that case
export LOCALTESTONLY="Yes"

echo "Version information:"
"${OPENSSL_APP}" version

# Disable testing for a few versions: Buggy as hell:
if "${OPENSSL_APP}" version | grep -qE 'OpenSSL (3\.0\.(0|1|4)) '; then
   echo "Skipping testing of buggy OpenSSL versions 3.0.0, 3.0.1 and 3.0.4"
   exit 0
fi

if ! "${OPENSSL_APP}" list -providers -verbose; then
   echo "Baseline openssl invocation failed. Exiting test."
   exit 1
fi

# Ensure "oqsprovider" is registered:
if ! "${OPENSSL_APP}" list -providers -verbose | grep -q oqsprovider; then
   echo "oqsprovider not registered. Exit test."
   exit 1
fi

"${OPENSSL_APP}" list -signature-algorithms
"${OPENSSL_APP}" list -kem-algorithms


# Run interop-tests:
# cleanup log from previous runs:
rm -f interop.log

echo "Cert gen/verify, CMS sign/verify, CA tests for all enabled OQS signature algorithms commencing: "

# auto-detect all available signature algorithms:
for alg in $("${OPENSSL_APP}" list -signature-algorithms | grep oqsprovider | sed -e "s/ @ .*//g" | sed -e "s/^  //g")
do
   if [ "$1" = "-V" ]; then
      echo "Testing $alg"
   fi
   interop "${alg}"
   certsgenerated=1
done

if [ -z "${certsgenerated}" ]; then
   echo "No OQS signature algorithms found in provider 'oqsprovider'. No certs generated. Exiting."
   exit 1
else
   if [ "$1" = "-V" ]; then
      echo "Certificates successfully generated in $(pwd)/tmp"
   fi
fi

echo

# Run interop tests with external sites
echo "External interop tests commencing"
${OQS_PROVIDER_TESTSCRIPTS}/oqsprovider-externalinterop.sh

# Run built-in tests:
# Without removing OPENSSL_CONF ctest hangs... ???
unset OPENSSL_CONF
rv=0
if ! ( cd _build && ctest $@ ); then
   rv=1
fi

# cleanup: TBC:
# decide for testing strategy when integrating to OpenSSL test harness:
# Keep scripts generating certs (testing more code paths) or use API?
#rm -rf tmp
echo

if [ "${rv}" -ne 0 ]; then
   echo "Tests failed."
else
   echo "All oqsprovider tests passed."
fi
exit "${rv}"

