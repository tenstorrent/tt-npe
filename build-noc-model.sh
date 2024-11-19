ROOT=$(git rev-parse --show-toplevel)
BUILD_DIR=${ROOT}/build/

#if [ ! -d "${BUILD_DIR}" ]; then
#    mkdir ${BUILD_DIR}
#    cmake -G Ninja -B ${BUILD_DIR}
#fi

# need to run this unconditionally to regen compile commands!
cmake -G Ninja -B ${BUILD_DIR} > /dev/null
cmake --build ${BUILD_DIR} -- -j16  
