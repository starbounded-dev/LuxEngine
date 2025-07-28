# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS build

RUN apt-get update

RUN mkdir -p /workspace
WORKDIR /workspace

### Fetch Remote Artifacts
	RUN apt-get install -y curl xz-utils

	# The Ubuntu premake package only provides premake4 for some reason
	ARG PREMAKE_TAG=5.0.0-beta2
	RUN curl -Lo premake.tar.gz https://github.com/premake/premake-core/releases/download/v$PREMAKE_TAG/premake-$PREMAKE_TAG-linux.tar.gz
	RUN tar xzf premake.tar.gz

	# `Linux-Build` wants premake on the path
	ENV PATH="/workspace:${PATH}"

	# Download and extract Vulkan SDK
	ARG VULKAN_VER=1.3.283.0
	RUN curl -Lo vulkan.tar.xz https://sdk.lunarg.com/sdk/download/$VULKAN_VER/linux/vulkansdk-linux-x86_64-$VULKAN_VER.tar.xz
	RUN tar xJf vulkan.tar.xz

	ENV VULKAN_SDK=/workspace/$VULKAN_VER/x86_64

### Install Build Environment
	# Install basic build tooling
	# `mold` is a faster linker than basic `ld` or llvm `lld`
	# The llvm package provides `llvm-ar` which is a faster archiver than GNU
	# binutils `ar`
	RUN apt-get install -y build-essential clang mold llvm libtbb-dev

	# Install dotnet
	RUN apt-get install -y dotnet-sdk-8.0

	## Install build dependencies
		# GLFW
		RUN apt-get install -y xorg-dev
		# NFD
		RUN apt-get install -y libgtk-3-dev
		# BackwardCPP
		RUN apt-get install -y libdw-dev libunwind-dev

### Build Source Tree
	# Copying subsets of the source tree can improve caching behaviour
	# These should be ordered from least to most frequently modified
	COPY vendor vendor
	COPY scripts/Linux-Build.sh scripts/
	COPY premake5.lua .
	COPY Dependencies.lua .
	#
	COPY StarEditorLauncher StarEditorLauncher
	COPY StarEngine-Launcher StarEngine-Launcher
	COPY StarEngine-ScriptCore StarEngine-ScriptCore
	COPY StarEngine-Runtime StarEngine-Runtime

	## StarEditor
		# We don't need Resources etc. for the build and it massively increases
		# image size and build time
		COPY StarEditor/src StarEditor/src
		COPY StarEditor/premake5.lua StarEditor/
		COPY StarEditor/Resources/LUA StarEditor/Resources/LUA
		COPY StarEditor/SandboxProject/premake5.lua StarEditor/SandboxProject/
		COPY StarEditor/SandboxProject/Assets/Scripts StarEditor/SandboxProject/Assets/Scripts

	COPY StarEngine StarEngine

## Execute The Build
	ARG NPROC=1
	ARG CXXFLAGS=
	ARG BUILD_CONFIG=Debug

	ENV STARENGINE_DIR=/workspace
	ENV BUILD_CONFIG=$BUILD_CONFIG

	# This script is separate for use in non-containerized builds
	RUN scripts/Linux-Build.sh AR=llvm-ar LDFLAGS=-fuse-ld=mold CXXFLAGS=$CXXFLAGS -j$NPROC
