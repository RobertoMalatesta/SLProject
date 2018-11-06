#!/bin/sh

# ####################################################
# Build script for g2o for Linux
# ####################################################

ARCH=linux
ZIPFILE="$ARCH"_g2o
ZIPFOLDER=build/$ZIPFILE
BUILD_D=build/"$ARCH"_debug
BUILD_R=build/"$ARCH"_release

clear
echo "Building g2o"

# Cloning g2o
if [ ! -d "g2o/.git" ]; then
    git clone https://github.com/RainerKuemmerle/g2o.git
fi

# Make build folder for debug version
cd g2o
mkdir build
rm -rf $BUILD_D
mkdir $BUILD_D
cd $BUILD_D

# Run cmake to configure and generate the make files
cmake \
    -DCMAKE_INSTALL_PREFIX=install \
    -DG2O_BUILD_APPS=off \
    -DG2O_BUILD_EXAMPLES=off \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_DEBUG_POSTFIX="" \
    -DEIGEN3_INCLUDE_DIR=../../lib-SLExternal/eigen \
    -DEIGEN3_VERSION_OK=ON \
    -DBUILD_CSPARSE=off \
    -DG2O_USE_OPENGL=off \
    -DG2O_USE_CSPARSE=off \
    -DG2O_USE_CHOLMOD=off \
    ../..

# finally build it
make -j8

# copy all into install folder
make install
cd ../.. # back to g2o

# Make build folder for release version
rm -rf $BUILD_R
mkdir $BUILD_R
cd $BUILD_R

# Run cmake to configure and generate the make files
cmake \
    -DCMAKE_INSTALL_PREFIX=install \
    -DG2O_BUILD_APPS=off \
    -DG2O_BUILD_EXAMPLES=off \
    -DCMAKE_BUILD_TYPE=Release \
    -DEIGEN3_INCLUDE_DIR=../../lib-SLExternal/eigen \
    -DEIGEN3_VERSION_OK=ON \
    -DBUILD_CSPARSE=off \
    -DG2O_USE_OPENGL=off \
    -DG2O_USE_CSPARSE=off \
    -DG2O_USE_CHOLMOD=off \
    ../..

# finally build it
make -j8

# copy all into install folder
make install
cd ../.. # back to g2o

# Create zip folder for debug and release version
rm -rf $ZIPFOLDER
mkdir $ZIPFOLDER
cp -R $BUILD_R/install/include   $ZIPFOLDER/include
cp -R $BUILD_R/install/lib       $ZIPFOLDER/Release
cp -R $BUILD_D/install/lib       $ZIPFOLDER/Debug
cp doc/license* $ZIPFOLDER
cp README.md $ZIPFOLDER

if [ -d "../../prebuilt/$ZIPFILE" ]; then
    rm -rf ../../prebuilt/$ZIPFILE
fi

mv $ZIPFOLDER ../../prebuilt
