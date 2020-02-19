#!/bin/bash

export VK_VERSION=1.2.131.2
if [[ $TRAVIS_OS_NAME == 'osx' ]]; then
    brew install libpng openexr
    curl -GO https://sdk.lunarg.com/sdk/download/$VK_VERSION/mac/vulkan-sdk.tar.gz
    tar zxf vulkan-sdk.tar.gz
    export VULKAN_SDK=$TRAVIS_BUILD_DIR/vulkansdk-macos-$VK_VERSION/macOS
cat > environments.sh << EOF
export VULKAN_SDK=$VULKAN_SDK
export VK_TOOLS=$VULKAN_SDK/bin
export VK_ICD_FILENAMES=$VULKAN_SDK/etc/vulkan/icd.d/MoltenVK_icd.json
export VK_LAYER_PATH=$VULKAN_SDK/etc/vulkan/explicit_layer.d
EOF
    sudo cp -R $VULKAN_SDK/Frameworks/* /Library/Frameworks/
elif [[ $TRAVIS_OS_NAME == 'linux' ]]; then
    sudo apt-get -qq update
    sudo apt-get install -y cmake libpng16-dev libopenexr-dev
    curl -GO https://sdk.lunarg.com/sdk/download/$VK_VERSION/linux/vulkan-sdk.tar.gz
    tar zxf vulkan-sdk.tar.gz
    export VULKAN_SDK=$TRAVIS_BUILD_DIR/$VK_VERSION/x86_64
cat > environments.sh << EOF
export VULKAN_SDK=$VULKAN_SDK
export VK_TOOLS=$VULKAN_SDK/bin
export VK_ICD_FILENAMES=~/dev/mesa/share/vulkan/icd.d/intel_icd.x86_64.json
export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d
EOF
    sudo cp -R $VULKAN_SDK/include/* /usr/local/include/
    sudo cp -R $VULKAN_SDK/lib/* /usr/local/lib/
elif [[ $TRAVIS_OS_NAME == 'windows' ]]; then
    curl -GO https://sdk.lunarg.com/sdk/download/$VK_VERSION/windows/vulkan-sdk.exe
    start /wait vulkan-sdk.exe /S
type > environments.sh << EOF
export VULKAN_SDK=C:\VulkanSDK\$VK_VERSION
EOF
fi
