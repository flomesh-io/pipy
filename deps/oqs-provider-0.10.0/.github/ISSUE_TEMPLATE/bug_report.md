---
name: Bug report
about: Create a report to help us improve
title: ''
labels: 'bug'
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior:
1. Go to '...'
2. Click on '....'
3. Scroll down to '....'
4. See error

**Expected behavior**
A clear and concise description of what you expected to happen.

**Screenshots**
If applicable, add screenshots to help explain your problem.

**Environment (please complete the following information):**
 - OS: [e.g. Ubuntu 20]
 - OpenSSL version [e.g., 3.2.0-dev]
 - oqsprovider version [e.g. 0.4.0]

Please run the following commands to obtain the version information:
 - For OpenSSL: `openssl version` 
 - For oqsprovider: `openssl list -providers`

If `oqsprovider` is not listed as active, be sure to first follow all
[USAGE guidance](https://github.com/open-quantum-safe/oqs-provider/blob/main/USAGE.md).

If reporting bugs triggered by OpenSSL API integrations, e.g. running
a provider build [statically](https://github.com/open-quantum-safe/oqs-provider/blob/main/CONFIGURE.md#oqs_provider_build_static)
or directly invoking any OpenSSL API, be sure to retrieve and report all errors
reported by using the OpenSSL [ERR_get_error_all](https://www.openssl.org/docs/man3.1/man3/ERR_get_error_all.html)
function.

Bug reports generated from [Debug builds](https://github.com/open-quantum-safe/oqs-provider/wiki/Debugging)
wth the debug environment variable "OQSPROV=1" set will be particularly helpful to find underlying
problems.

**Additional context**
Add any other context about the problem here.

**Hints**
To exclude a build/setup error, please consider running your test
commands to reproduce the problem in our [pre-build docker image](https://hub.docker.com/repository/docker/openquantumsafe/oqs-ossl3/general),
e.g. as such: `docker run -it openquantumsafe/oqs-ossl3` and
provide full command input and output traces in the bug report.

