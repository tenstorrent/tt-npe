ROOT=$(git rev-parse --show-toplevel)
BUILD_DIR=${ROOT}/build/

cd $ROOT 

if [ "$1" = "debug" ]; then
    shift
    BUILD_TYPE=Debug
else
    BUILD_TYPE=Release
fi

if [ "$1" = "install" ]; then
    INSTALL_NPE=1
    shift
fi

INSTALL_PATH=$(readlink -f ./install)
export NINJA_STATUS="[%e: %f/%t] "
_NINJA_FLAGS_='-j16'

set -x
cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -G Ninja -B ${BUILD_DIR} > /dev/null
cmake --build ${BUILD_DIR} -- ${_NINJA_FLAGS_}
cmake --install ${BUILD_DIR} --prefix "$INSTALL_PATH" > /dev/null
