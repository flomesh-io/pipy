# Contributing

The OQS core team welcomes all proposals to improve this project. This may take 
the form of [a discussion](https://github.com/open-quantum-safe/oqs-provider/discussions)
for input or feedback, possible bug reports or feature requests via [issues](https://github.com/open-quantum-safe/oqs-provider/issues)
as well as new code and documentation via a [pull request (PR)](https://github.com/open-quantum-safe/oqs-provider/pulls).

## Review and Feedback

We aim to provide timely feedback to any input. If you are uncertain as to whether
a particular contribution is welcome, needed or timely, please first open an [issue](https://github.com/open-quantum-safe/oqs-provider/issues)
particularly in case of possible bugs or new feature requests or create a
[discussion](https://github.com/open-quantum-safe/oqs-provider/discussions).

## Pull requests

Pull requests should clearly state their purpose, possibly referencing an existing
[issue](https://github.com/open-quantum-safe/oqs-provider/issues) when resolving it.

All PRs should move to "Ready for Review" stage only if all CI tests pass (are green).

The OQS core team is happy to provide feedback also to Draft PRs in order to improve
them before the final "Review" stage.

### CODEOWNERS

This file is used to track which contributors are most well suited for reviewing
changes to specific sections, typically because they authored part or all of them.
Thus, any PR should revisit and possibly update this file suitably.

### Coding style

This project has adopted the LLVM coding style.
To check adherence of any new code to this, it therefore is highly recommended to
run the following commands in the project main directory prior to finishing a PR:

    ./scripts/do_code_format.sh

If the github CI reports style errors/deviations, review the code or consider running
the utility script `scripts/format_code.sh` if you'd like to get the code changed to
use the exact same code style check used in CI.

### Running CI locally

#### CircleCI

If encountering CI errors in CircleCI, it may be helpful to execute the test jobs
locally to debug. This can be facilitated by executing the command

   circleci local execute [--job] some-test-job

assuming "some-test-job" is the name of the test to be executed and the CircleCI
[command line tools have been installed](https://circleci.com/docs/local-cli).

#### Github CI

[Act](https://github.com/nektos/act) is a tool facilitating local execution of
github CI jobs. When executed in the main `oqsprovider` directory, 

    act -l Displays all github CI jobs
    act -j some-job Executes "some-job"

When installing `act` as a github extension, prefix the commands with `gh `.

### New features

Any PR introducing a new feature is expected to contain a test of this feature
and this test should be part of the CI pipeline, preferably using Github CI.

## Background knowledge

New contributors are recommended to first check out documentation of the 
[OpenSSL provider concept](https://www.openssl.org/docs/man3.0/man7/provider.html)
as well as the baseline API of [liboqs](https://github.com/open-quantum-safe/liboqs)
which are the two core foundations for this project.

## Failsafe

If you feel your contribution is not getting proper attention, please be sure to
add a tag to one or more of our [most active contributors](https://github.com/open-quantum-safe/oqs-provider/graphs/contributors).

## Issues to start working on

If you feel like contributing but don't know what specific topic to work on,
please check the [open issues tagged "good first issue" or "help wanted"](https://github.com/open-quantum-safe/oqs-provider/issues).

## Resource-efficiency

This project aims to be efficient and responsible with regard to resources consumed
also during CI. Therefore, all commits changing only documentation should contain the
commit message tag "[skip ci]" to avoid unnecessary test runs.

