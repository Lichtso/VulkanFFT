#!/bin/bash

export VK_VERSION=1.2.141.2
if [[ $TRAVIS_OS_NAME == 'osx' ]]; then
    brew install libpng openexr
    curl -LGo vulkan-sdk.tar.gz https://sdk.lunarg.com/sdk/download/$VK_VERSION/mac/vulkan-sdk.tar.gz?human=true
    tar zxf vulkan-sdk.tar.gz
    export VULKAN_SDK=$TRAVIS_BUILD_DIR/vulkansdk-macos-$VK_VERSION/macOS
    sudo cp -R $VULKAN_SDK/Frameworks/* /Library/Frameworks/
    cat > environments.sh << EOF
export VULKAN_SDK=$VULKAN_SDK
export VK_TOOLS=$VULKAN_SDK/bin/
export VK_ICD_FILENAMES=$VULKAN_SDK/etc/vulkan/icd.d/MoltenVK_icd.json
export VK_LAYER_PATH=$VULKAN_SDK/etc/vulkan/explicit_layer.d
EOF
elif [[ $TRAVIS_OS_NAME == 'linux' ]]; then
    sudo apt-get -qq update
    sudo apt-get install -y cmake libpng16-dev libopenexr-dev
    curl -LGo vulkan-sdk.tar.gz https://sdk.lunarg.com/sdk/download/$VK_VERSION/linux/vulkan-sdk.tar.gz?human=true
    tar zxf vulkan-sdk.tar.gz
    export VULKAN_SDK=$TRAVIS_BUILD_DIR/$VK_VERSION/x86_64
    sudo cp -R $VULKAN_SDK/include/* /usr/local/include/
    sudo cp -R $VULKAN_SDK/lib/* /usr/local/lib/
    cat > environments.sh << EOF
export VULKAN_SDK=$VULKAN_SDK
export VK_TOOLS=$VULKAN_SDK/bin/
export VK_ICD_FILENAMES=~/dev/mesa/share/vulkan/icd.d/intel_icd.x86_64.json
export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d
EOF
elif [[ $TRAVIS_OS_NAME == 'windows' ]]; then
    curl -LGo vulkan-sdk.exe https://sdk.lunarg.com/sdk/download/$VK_VERSION/windows/vulkan-sdk.exe?human=true
    7z x vulkan-sdk.exe
    export VULKAN_SDK=$TRAVIS_BUILD_DIR
    cat > environments.sh << EOF
export VULKAN_SDK=$VULKAN_SDK
export VK_TOOLS=$VULKAN_SDK/Bin/
export VK_ICD_FILENAMES=/windows/system32/nv-vk64.json
export VK_LAYER_PATH=$VULKAN_SDK/Bin
EOF
fi
