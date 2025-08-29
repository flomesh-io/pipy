# oqs-provider testing

The tests in this folder are running separately from the OpenSSL test framework, but some tests utilize some of its plumbing. Therefore the OpenSSL code base, incl. its "test" directory must be locally available for all tests to be executed. The script `../scripts/fullbuild.sh` ensures this if no explicit hint to an OpenSSL binary installation is given (via the environment variable "OPENSSL_INSTALL").

