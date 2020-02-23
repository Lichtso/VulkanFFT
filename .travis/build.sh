#!/bin/bash

. ./environments.sh
if [[ $TRAVIS_OS_NAME == 'windows' ]]; then
    setx -m VULKAN_SDK $VULKAN_SDK
fi
mkdir -p build && cd build
cmake .. -DVK_TOOLS=$VK_TOOLS
cmake --build .
