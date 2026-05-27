include_guard(GLOBAL)

function(aurora_get_target_arch out_var)
  set(_arch "${CMAKE_SYSTEM_PROCESSOR}")

  if (APPLE AND DEFINED CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "")
    set(_osx_archs ${CMAKE_OSX_ARCHITECTURES})
    list(REMOVE_DUPLICATES _osx_archs)
    list(LENGTH _osx_archs _osx_arch_count)
    if (_osx_arch_count EQUAL 1)
      list(GET _osx_archs 0 _arch)
    else ()
      set(_arch "")
    endif ()
  endif ()

  set(${out_var} "${_arch}" PARENT_SCOPE)
endfunction()

