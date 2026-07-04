function(_aurora_resolve_target out_var target)
    if (NOT TARGET "${target}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif ()

    get_target_property(_aliased_target "${target}" ALIASED_TARGET)
    if (_aliased_target)
        set(${out_var} "${_aliased_target}" PARENT_SCOPE)
    else ()
        set(${out_var} "${target}" PARENT_SCOPE)
    endif ()
endfunction()

function(_aurora_strip_inactive_preprocessor_blocks out_var contents)
    string(REPLACE "\r\n" "\n" _contents "${contents}")
    string(REPLACE "\r" "\n" _contents "${_contents}")
    string(REPLACE ";" "\\;" _contents "${_contents}")
    string(REPLACE "\n" ";" _lines "${_contents}")

    set(_output)
    set(_skip_depth 0)
    foreach (_line IN LISTS _lines)
        if (_skip_depth GREATER 0)
            if (_skip_depth EQUAL 1 AND _line MATCHES "^[ \t]*#[ \t]*(else|elif)\\b")
                set(_skip_depth 0)
                continue()
            elseif (_line MATCHES "^[ \t]*#[ \t]*(if|ifdef|ifndef)\\b")
                math(EXPR _skip_depth "${_skip_depth} + 1")
            elseif (_line MATCHES "^[ \t]*#[ \t]*endif\\b")
                math(EXPR _skip_depth "${_skip_depth} - 1")
            endif ()
            continue()
        endif ()

        if (_line MATCHES "^[ \t]*#[ \t]*if[ \t]+0([ \t]|$)")
            set(_skip_depth 1)
            continue()
        endif ()

        string(APPEND _output "${_line}\n")
    endforeach ()

    set(${out_var} "${_output}" PARENT_SCOPE)
endfunction()

function(_aurora_collect_symbol_references out_var)
    set(_symbols)
    foreach (_file IN LISTS ARGN)
        if (NOT EXISTS "${_file}")
            continue()
        endif ()

        file(READ "${_file}" _contents)
        _aurora_strip_inactive_preprocessor_blocks(_contents "${_contents}")
        string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _contents "${_contents}")
        string(REGEX REPLACE "//[^\n\r]*" " " _contents "${_contents}")
        string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*[ \t\r\n]*\\(" _matches "${_contents}")
        foreach (_match IN LISTS _matches)
            string(REGEX REPLACE "[ \t\r\n]*\\($" "" _symbol "${_match}")
            if (NOT _symbol MATCHES "^(defined|for|if|return|sizeof|switch|while)$")
                list(APPEND _symbols "${_symbol}")
            endif ()
        endforeach ()
    endforeach ()

    list(REMOVE_DUPLICATES _symbols)
    set(${out_var} ${_symbols} PARENT_SCOPE)
endfunction()

function(_aurora_collect_defined_symbols out_var)
    set(_symbols)
    foreach (_file IN LISTS ARGN)
        if (NOT EXISTS "${_file}")
            continue()
        endif ()

        file(READ "${_file}" _contents)
        _aurora_strip_inactive_preprocessor_blocks(_contents "${_contents}")
        string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _contents "${_contents}")
        string(REGEX REPLACE "//[^\n\r]*" " " _contents "${_contents}")
        string(REGEX REPLACE "(^|\n)[ \t]*#[^\n\r]*" "\n" _contents "${_contents}")
        string(REGEX REPLACE "[\n\r]+" " " _contents "${_contents}")
        string(REGEX MATCHALL
                "(^|[;{}])[ \t]*((extern[ \t]+\"C\"[ \t]+)?([A-Za-z_][A-Za-z0-9_:<>]*|const|volatile)[ \t*&]+)+[A-Za-z_][A-Za-z0-9_]*[ \t]*\\([^;{}]*\\)[ \t]*(const[ \t]*)?\\{"
                _matches "${_contents}")
        foreach (_match IN LISTS _matches)
            string(REGEX REPLACE
                    ".*[ \t*&]([A-Za-z_][A-Za-z0-9_]*)[ \t]*\\([^()]*\\)[ \t]*(const[ \t]*)?\\{.*"
                    "\\1" _symbol "${_match}")
            list(APPEND _symbols "${_symbol}")
        endforeach ()
    endforeach ()

    list(REMOVE_DUPLICATES _symbols)
    set(${out_var} ${_symbols} PARENT_SCOPE)
endfunction()

function(_aurora_collect_target_sources out_var)
    get_filename_component(_aurora_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)

    set(_source_files)
    foreach (_target IN LISTS ARGN)
        _aurora_resolve_target(_real_target "${_target}")
        if (NOT _real_target MATCHES "^aurora_")
            continue()
        endif ()

        get_target_property(_sources "${_real_target}" SOURCES)
        if (NOT _sources)
            continue()
        endif ()

        foreach (_source IN LISTS _sources)
            if (NOT _source MATCHES "\\.(c|cc|cpp|cxx|m|mm)$")
                continue()
            endif ()
            if (IS_ABSOLUTE "${_source}")
                set(_source_file "${_source}")
            else ()
                set(_source_file "${_aurora_root}/${_source}")
            endif ()
            if (EXISTS "${_source_file}")
                list(APPEND _source_files "${_source_file}")
            endif ()
        endforeach ()
    endforeach ()

    list(REMOVE_DUPLICATES _source_files)
    set(${out_var} ${_source_files} PARENT_SCOPE)
endfunction()

function(aurora_collect_sdk_export_symbols out_var)
    get_filename_component(_aurora_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)

    set(_targets ${ARGN})
    if (NOT _targets)
        set(_targets
                aurora::card
                aurora::core
                aurora::dvd
                aurora::gd
                aurora::gx
                aurora::ms
                aurora::mtx
                aurora::os
                aurora::pad
                aurora::si
                aurora::vi
        )
    endif ()

    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS "${_aurora_root}/include/dolphin/*.h")
    _aurora_collect_symbol_references(_public_symbols ${_headers})
    _aurora_collect_target_sources(_sources ${_targets})
    _aurora_collect_defined_symbols(_defined_symbols ${_sources})

    set(_sdk_symbols)
    foreach (_symbol IN LISTS _defined_symbols)
        if (_symbol IN_LIST _public_symbols)
            list(APPEND _sdk_symbols "${_symbol}")
        endif ()
    endforeach ()

    list(REMOVE_DUPLICATES _sdk_symbols)
    list(SORT _sdk_symbols)
    set(${out_var} ${_sdk_symbols} PARENT_SCOPE)
endfunction()

function(aurora_target_export_sdk_symbols target)
    cmake_parse_arguments(ARG "" "OUTPUT" "TARGETS" ${ARGN})
    if (NOT TARGET "${target}")
        message(FATAL_ERROR "aurora_target_export_sdk_symbols: target does not exist: ${target}")
    endif ()

    if (ARG_TARGETS)
        set(_targets ${ARG_TARGETS})
    else ()
        get_target_property(_targets "${target}" LINK_LIBRARIES)
        if (NOT _targets)
            set(_targets)
        endif ()
    endif ()

    aurora_collect_sdk_export_symbols(_sdk_symbols ${_targets})
    if (NOT WIN32)
        return()
    endif ()

    if (NOT _sdk_symbols)
        message(WARNING "aurora_target_export_sdk_symbols: no SDK symbols found for ${target}")
        return()
    endif ()

    if (ARG_OUTPUT)
        set(_exports_def "${ARG_OUTPUT}")
    else ()
        set(_exports_def "${CMAKE_CURRENT_BINARY_DIR}/${target}_aurora_sdk_exports.def")
    endif ()

    file(WRITE "${_exports_def}" "EXPORTS\n")
    foreach (_symbol IN LISTS _sdk_symbols)
        file(APPEND "${_exports_def}" "    ${_symbol}\n")
    endforeach ()

    set_source_files_properties("${_exports_def}" PROPERTIES GENERATED TRUE)
    target_sources("${target}" PRIVATE "${_exports_def}")
endfunction()
