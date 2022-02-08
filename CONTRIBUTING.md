# Contributing to Pipy

We welcome contributions from the community. Please read the following guidelines carefully to maximize the chances of your PR being merged.

## Using the issue tracker

The [issue tracker](https://github.com/flomesh-io/pipy/issues) is the heart of Pipy's work. Use it for bugs, questions, proposals and feature requests.

Please always **open a new issue before sending a pull request** if you want to add a new feature to Pipy, unless it is a minor fix, and wait until someone from the core team approves it before you actually start working on it. Otherwise, you risk having the pull request rejected, and the effort implementing it goes to waste. And if you start working on an implementation for an issue, please **let everyone know in the comments** so someone else does not start working on the same thing.

Regardless of the kind of issue, please make sure to look for similar existing issues before posting; otherwise, your issue may be flagged as `duplicated` and closed in favour of the original one. Also, once you open a new issue, please make sure to honour the items listed in the issue template.

If you open a question, remember to close the issue once you are satisfied with the answer and you think
there's no more room for discussion. We'll anyway close the issue after some days.

If something is missing from Pipy it might be that it's not yet implemented or that it was purposely left out. If in doubt, just ask.

### What's needed right now

These are the most important general topics in need right now, so if you are interested open an issue to start working on it:

* Documenting the Reference API
* Add missing tutortials
* Add missing how-tos

### Labels

Issue tracker labels are sorted by category: kind, pr, status and topic.

#### Kind

The most basic category is the kind of the issue: `bug`, `feature` and `question` speak for themselves, while `refactor` is left for changes that do not actually introduce a new a feature, and are not fixing something that is broken, but rather clean up the code (or documentation!).

#### PR

Pull-request only labels, used to signal that a pull request `needs-review` by a core team member, or that is still `wip` (work in progress).

#### Topic

Topic encompasses the broad aspect of Pipy that the issue refers to: could be performance, PipyJS, JavaScript API, and quite a large etc.

#### Status

Status labels attempt to capture the lifecycle of an issue:

* A detailed proposal on a feature is marked as `draft`, while a more general argument is usually labelled as `discussion` until a consensus is achieved.

* An issue is `accepted` when it describes a feature or bugfix that a core team member has agreed to have added to Pipy codebase, so as soon as a design is discussed (if needed), it's safe to start working on a pull request.

* Bug reports are marked as `needs-more-info`, where the author is requested to provide the info required; note that the issue may be closed after some time if it is not supplied.

* Issues that are batched in an epic to be worked on as a group are typically marked as `deferred`, while low-prio issues or tasks far away in the roadmap are marked as `someday`.

* Closed issues are marked as `implemented`, `invalid`, `duplicate` or `wontfix`, depending on their resolution.

## Contributing to...

### The documentation

Pipy reference is available at https://github.com/flomesh-io/pipy/tree/main/docs/reference.

### The Pipy codebase

1. Fork it ( https://github.com/flomesh-io/pipy/fork )
2. Clone it

Make sure that your changes follow the recommended **Coding Style**.

Then push your changes and create a pull request.


## This guide

If this guide is not clear and it needs improvements, please send pull requests against it. Thanks! :-)

## Making good pull requests

The commit history should consist of commits that transform the codebase from one state into another one, motivated by something that
should change, be it a bugfix, a new feature or some ground work to support a new feature, like changing an existing API or introducing
a new isolated class that is later used in the same pull request.

Review history should be preserved in a pull request. If you need to push a change to an open pull request (for example
applying a review suggestion) these changes should be added as individual fixup commits. Please do not amend previous commits and force push to the PR branch. This makes reviews much harder because reference to previous state is hidden.

If changes introduced to `main` branch result in conflicts, it should be merged with a merge commit (`git fetch upstream/main; git merge upstream/main`).

### Minimum requirements

1. Describe reasons and result of the change in the pull request comment.
2. Do not force push to a pull request. The development history should be easily traceable.
3. Any change to a public API requires appropriate documentation: params (and particularly interesting combinations of them if the method is complex), results, interesting, self-contained examples.


## Code of Conduct

Please note that this project is released with a [Contributor Code of Conduct][ccoc].
By participating in this project you agree to abide by its terms.

[ccoc]: https://github.com/flomesh-io/pipy/blob/main/CODE_OF_CONDUCT.md
