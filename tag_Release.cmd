@echo off
echo Tag Assistant: This script creates an empty tag commit and applies a git tag on it
echo Tag Release...

FOR /F "tokens=* USEBACKQ" %%F IN (`git rev-parse --short HEAD`) DO (
SET commithash=%%F
)

FOR /F "tokens=* USEBACKQ" %%F IN (`git branch --show-current`) DO (
SET branchname=%%F
)

echo Create tag commit..
git commit --allow-empty -m "(( Release_%branchname%_%commithash% ))"
echo Create tag..
git tag "Release_%branchname%_%commithash%"
echo Done.

pause