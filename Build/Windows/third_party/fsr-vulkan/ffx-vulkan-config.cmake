
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ffx-vulkan-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)
find_dependency(Vulkan)
include("${CMAKE_CURRENT_LIST_DIR}/ffx-vulkan-targets.cmake")

# Defined only when the package was explicitly installed with a complete,
# caller-supplied source-v07 bundle. Applications still decide whether and how
# to load it; this is only a discoverable data location for the asset helper.
if("OFF" STREQUAL "ON")
    set(FFX_VK_FSR4_V07_ASSET_DIR
        "${PACKAGE_PREFIX_DIR}/share/ffx-vulkan/fsr4-v07")
endif()
