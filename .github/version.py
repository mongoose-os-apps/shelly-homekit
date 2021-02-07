#!/usr/bin/python3
import argparse
import subprocess

def run(*args):
    return subprocess.check_output(args).decode('utf-8').strip()

def git_describe(*args):
    return run('git', 'describe', '--tags', *args)

def git_branch_name():
    return run('git', 'branch', '--show-current')

def commits_since_forkpoint():
    return int(run('git', 'rev-list', '--count',  '--no-merges', 'origin/master..'))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Get Shelly-HomeKit version name using Git')
    parser.add_argument('-s', '--suffix', action='store_true', help='output only the suffix after the version number')
    parser.add_argument('-r', '--release', action='store_true', help='just use tag number, for stable releases')
    args, extra_args = parser.parse_known_args()

    # all unknown command line arguments are passed directly to git-describe
    tag_name, commits_since_tag, extra = git_describe(*extra_args).split(sep='-', maxsplit=2)

    # usually this is just the sha of the git commit
    # however if e.g. --dirty is passed then, if working tree
    # dirty, it will be contained in the `info` list
    info = extra.split('-')

    version = "" if args.suffix else tag_name

    if args.release:
        print(version)
        exit()

    branch = git_branch_name()
    if branch == "master":
        version += f"-latest{commits_since_tag}"
    else:
        # strip dashes from the branch name so it doesn't mess up update
        version += f"-{branch.replace('-', '')}{commits_since_forkpoint()}"

    # append things like -dirty and -broken,
    # if passed, to the version name
    version += '-'.join(['', *info[1:]])

    print(version)
