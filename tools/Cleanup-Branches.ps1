# SPDX-License-Identifier: MIT
# v2 branch layout: main (Release Candidates), beta (Betas), test (private Test).
# Every other remote branch is first saved as an archive/<name> tag, so no work
# is ever lost, then deleted. Fully merged branches are removed by default;
# unmerged branches only with -IncludeUnmerged. Run with -WhatIf first.
param([switch]$IncludeUnmerged,[switch]$WhatIf,[string]$Remote='origin')
$ErrorActionPreference='Stop'
git fetch $Remote --prune --tags | Out-Null
$keep=@('main','beta','test','HEAD')
foreach($name in @('beta','test')){
  git show-ref --verify --quiet "refs/remotes/$Remote/$name"
  if($LASTEXITCODE -ne 0){
    if($WhatIf){Write-Output "Would create $name from main"}else{git push $Remote "refs/remotes/$Remote/main:refs/heads/$name";if($LASTEXITCODE){throw "Could not create $name"}}
  }
}
$merged=@(git branch -r --merged "$Remote/main" --format='%(refname:lstrip=3)')
$all=@(git branch -r --format='%(refname:lstrip=3)')
foreach($branch in $all){
  if($keep -contains $branch -or -not $branch){continue}
  $isMerged=$merged -contains $branch
  if(-not $isMerged -and -not $IncludeUnmerged){Write-Output "KEEP (unmerged work): $branch";continue}
  $tag="archive/$branch"
  if($WhatIf){Write-Output ("Would archive as $tag and delete: $branch"+$(if($isMerged){''}else{' (UNMERGED)'}));continue}
  git tag -f $tag "refs/remotes/$Remote/$branch" | Out-Null
  git push $Remote "refs/tags/$tag" ; if($LASTEXITCODE){throw "Archive tag push failed for $branch"}
  git push $Remote --delete $branch ; if($LASTEXITCODE){throw "Delete failed for $branch"}
  Write-Output "Archived $branch as $tag and deleted the branch."
}
Write-Output 'Done. Branches now: main (Release Candidates), beta (Betas), test (private Test) plus any unmerged work kept above.'
