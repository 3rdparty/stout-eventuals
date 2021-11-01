"""Adds repositories/archives."""

########################################################################
# DO NOT EDIT THIS FILE unless you are inside the
# https://github.com/3rdparty/eventuals-grpc repository. If you
# encounter it anywhere else it is because it has been copied there in
# order to simplify adding transitive dependencies. If you want a
# different version of eventuals-grpc follow the Bazel build
# instructions at https://github.com/3rdparty/eventuals-grpc.
########################################################################

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("//3rdparty/eventuals:repos.bzl", eventuals_repos = "repos")
load("//3rdparty/stout-borrowed-ptr:repos.bzl", stout_borrowed_ptr_repos = "repos")
load("//3rdparty/stout-notification:repos.bzl", stout_notification_repos = "repos")

def repos(external = True, repo_mapping = {}):
    eventuals_repos(
        repo_mapping = repo_mapping,
    )

    stout_borrowed_ptr_repos(
        repo_mapping = repo_mapping,
    )

    stout_notification_repos(
        repo_mapping = repo_mapping,
    )

    if "com_github_grpc_grpc" not in native.existing_rules():
        http_archive(
            name = "com_github_grpc_grpc",
            urls = ["https://github.com/grpc/grpc/archive/v1.40.0.tar.gz"],
            strip_prefix = "grpc-1.40.0",
            sha256 = "13e7c6460cd979726e5b3b129bb01c34532f115883ac696a75eb7f1d6a9765ed",
        )

    if external and "com_github_3rdparty_eventuals_grpc" not in native.existing_rules():
        git_repository(
            name = "com_github_3rdparty_eventuals_grpc",
            remote = "https://github.com/3rdparty/eventuals-grpc",
            commit = "d8f00664682e9ff6e0e29038203ab770c8815505",
            shallow_since = "1635736585 -0600",
            repo_mapping = repo_mapping,
        )
