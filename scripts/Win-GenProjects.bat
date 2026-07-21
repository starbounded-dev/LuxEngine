@echo off
pushd %~dp0
python Win-GenProjects.py %*
popd
PAUSE
