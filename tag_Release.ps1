write-host "Tag Assistant: this script creates an empty tagged commit and applies a Git tag on it"
write-host "Tag Assistant: the commit message follows a special syntax that when pushed, publishes a Draft release to GitHub"

write-host "Gathering branch and commit details..."
$commitHash = git rev-parse --short HEAD
$branchName = git branch --show-current

# Format commit message and tag
$commitMesage = "(( Release_" + $branchName + "_" + $commitHash + " ))"
$gitTag = "Release_" + $branchName + "_" + $commitHash

write-host "Creating tagged commit..."
git commit --allow-empty -m $commitMesage
write-host "Creating Git tag..."
git tag $gitTag
write-host "Done"

write-host "Tag Assistant: please now push the changes with the '--tags' arg"
read-host Prompt "Please press Enter to exit"