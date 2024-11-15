ROOT=$(git rev-parse --show-toplevel)
BUILD_DIR=${ROOT}/build/

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir ${BUILD_DIR}
    cmake -G Ninja -B ${BUILD_DIR}
fi

cmake --build ${BUILD_DIR} -- -j16  
