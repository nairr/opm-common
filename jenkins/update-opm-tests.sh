#!/bin/bash

MAIN_REPO=$1 # The repo the update was triggered from

source $WORKSPACE/deps/opm-common/jenkins/build-opm-module.sh

declare -a upstreams # Everything is considered an upstream to aid code reuse
upstreams=(libecl
           opm-common
           opm-material
           opm-grid
           ewoms
           opm-simulators
           opm-upscaling
          )

declare -A upstreamRev
upstreamRev[libecl]=master
upstreamRev[opm-common]=master
upstreamRev[opm-material]=master
upstreamRev[opm-grid]=master
upstreamRev[ewoms]=master
upstreamRev[opm-simulators]=master
upstreamRev[opm-upscaling]=master

# Setup revision tables
parseRevisions
upstreamRev[$MAIN_REPO]=$sha1

# Create branch name
BRANCH_NAME="update"
for repo in ${upstreams[*]}
do
  if [ "${upstreamRev[$repo]}" != "master" ]
  then
    rev=${upstreamRev[$repo]}
    prnumber=${rev//[!0-9]/}
    BRANCH_NAME="${BRANCH_NAME}_${repo}_$prnumber"
    test -n "$REASON" && REASON+="        "
    REASON+="https://github.com/OPM/$repo/pull/$prnumber\n"
  fi
done

# Do the commit
export REASON
export BRANCH_NAME
$WORKSPACE/deps/opm-simulators/tests/update_reference_data.sh $OPM_DATA_ROOT

# Finally open the pull request
cd $OPM_DATA_ROOT
git remote add jenkins4opm git@github.com:jenkins4opm/opm-tests

# Do some cleaning of old remote branches
# Easier code with git 2.7+
#BRANCHES=`git branch --sort=committerdate -r | grep jenkins4opm`
#NBRANCHES=`git branch --sort=committerdate -r | grep jenkins4opm | wc -l`
git fetch jenkins4opm # Sadly necessary with older git
BRANCHES=`git for-each-ref --sort=committerdate refs/remotes/jenkins4opm/ --format='%(refname:short)'`
NBRANCHES=`git for-each-ref --sort=committerdate refs/remotes/jenkins4opm/ --format='%(refname:short)' | wc -l`
if test $NBRANCHES -gt 30
then
  for BRANCH in $BRANCHES
  do
    BNAME=`echo $BRANCH | cut -f1 -d '/' --complement`
    if [ "$BNAME" != "HEAD" ]
    then
      git push jenkins4opm :$BNAME
    fi
    NBRANCHES=$((NBRANCHES-1))
    if test $NBRANCHES -lt 30
    then
      break
    fi
  done
fi

if [ -n "`echo $BRANCHES | tr ' ' '\n' | grep ^jenkins4opm/$BRANCH_NAME$`" ]
then
  GH_TOKEN=`git config --get gitOpenPull.Token`
  REV=${upstreamRev[$MAIN_REPO]}
  PRNUMBER=${rev//[!0-9]/}
  DATA_PR=`curl -X GET https://api.github.com/repos/OPM/opm-tests/pulls?head=jenkins4opm:$BRANCH_NAME | grep '"number":' | awk -F ':' '{print $2}' | sed -e 's/,//' -e 's/ //'`
  git push -u jenkins4opm $BRANCH_NAME -f
  curl -d "{ \"body\": \"Existing PR https://github.com/OPM/opm-data/pull/$DATA_PR was updated\" }" -X POST https://api.github.com/repos/OPM/$MAIN_REPO/issues/$PRNUMBER/comments?access_token=$GH_TOKEN
else
  git-open-pull -u jenkins4opm --base-account OPM --base-repo opm-tests -r /tmp/cmsg $BRANCH_NAME
fi
