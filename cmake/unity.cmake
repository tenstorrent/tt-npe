# SPDX-License-Identifier: Apache-2.0
#
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

function(TT_ENABLE_UNITY_BUILD TARGET)
    if(TT_UNITY_BUILDS)
        set_target_properties(
            ${TARGET}
            PROPERTIES
                UNITY_BUILD
                    ON
                UNITY_BUILD_UNIQUE_ID
                    "CMAKE_UNIQUE_NAMESPACE"
        )
    endif()
endfunction()
