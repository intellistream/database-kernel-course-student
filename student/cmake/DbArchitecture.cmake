include_guard(GLOBAL)

set(
    DB_COMPONENT_NAMES
    base
    types
    io
    protocol
    storage
    wal
    transaction
    catalog
    access
    sql
    planner
    execution
    recovery
    engine
    api
    course
)

set(DB_COURSE_TOPIC_NAMES access foundations query storage transaction wal)

# Approved direct component edges. This is intentionally not a transitive
# closure: a new direct link is an architecture change even when it would keep
# the target graph acyclic.
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_base)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_types base)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_io base)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_protocol base io types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_storage base io)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_wal base io)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_transaction base io types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_catalog base io types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_access base storage transaction types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_sql base types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_planner base sql types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_execution base planner types)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_recovery access base catalog storage transaction wal)
set(
    DB_ALLOWED_COMPONENT_DEPENDENCIES_engine
    access
    base
    catalog
    execution
    io
    planner
    recovery
    sql
    storage
    transaction
    types
    wal
)
set(DB_ALLOWED_COMPONENT_DEPENDENCIES_api base engine types)
set(
    DB_ALLOWED_COMPONENT_DEPENDENCIES_course
    access
    api
    base
    catalog
    execution
    planner
    sql
    storage
    transaction
    types
    wal
)

function(_db_link_item_names_component link_item component output_variable)
    set(matches FALSE)
    foreach(candidate IN ITEMS "db::${component}" "db_${component}")
        # INTERFACE_LINK_LIBRARIES represents a PRIVATE dependency as
        # $<LINK_ONLY:...>. Exact matching intentionally does not count that
        # entry as sufficient for a dependency exposed by a public header.
        if(link_item STREQUAL candidate)
            set(matches TRUE)
            break()
        endif()
    endforeach()
    set(${output_variable} ${matches} PARENT_SCOPE)
endfunction()

function(_db_target_has_dependency target component visibility output_variable)
    if(visibility STREQUAL "PUBLIC")
        get_target_property(link_items ${target} INTERFACE_LINK_LIBRARIES)
    else()
        get_target_property(link_items ${target} LINK_LIBRARIES)
    endif()
    if(NOT link_items)
        set(link_items)
    endif()

    set(found FALSE)
    foreach(link_item IN LISTS link_items)
        _db_link_item_names_component("${link_item}" "${component}" matches)
        if(matches)
            set(found TRUE)
            break()
        endif()
    endforeach()
    set(${output_variable} ${found} PARENT_SCOPE)
endfunction()

function(_db_validate_component_file target component visibility source_file)
    file(READ "${source_file}" contents)

    string(
        REGEX MATCHALL
        "#[ \t]*include[ \t]*[<\"]db/[A-Za-z0-9_+-]+/[^>\"\r\n]+[>\"]"
        db_includes
        "${contents}"
    )
    foreach(include_line IN LISTS db_includes)
        string(
            REGEX REPLACE
            ".*db/([A-Za-z0-9_+-]+)/.*"
            "\\1"
            dependency
            "${include_line}"
        )
        if(dependency STREQUAL component)
            continue()
        endif()
        if(NOT dependency IN_LIST DB_COMPONENT_NAMES)
            continue()
        endif()

        _db_target_has_dependency(${target} "${dependency}" "${visibility}" declared)
        if(NOT declared)
            file(RELATIVE_PATH relative_source "${PROJECT_SOURCE_DIR}" "${source_file}")
            message(
                SEND_ERROR
                "${relative_source} directly includes db/${dependency}, but ${target} has no "
                "direct ${visibility} dependency on db::${dependency}"
            )
        endif()
    endforeach()

    string(
        REGEX MATCH
        "#[ \t]*include[ \t]*[<\"][^>\"\r\n]*/src/[^>\"\r\n]+[>\"]"
        private_include
        "${contents}"
    )
    if(private_include)
        file(RELATIVE_PATH relative_source "${PROJECT_SOURCE_DIR}" "${source_file}")
        message(
            SEND_ERROR
            "${relative_source} includes a component-private src/ path: ${private_include}"
        )
    endif()
endfunction()

function(_db_validate_component component)
    set(target "db_${component}")
    if(NOT TARGET ${target})
        message(SEND_ERROR "Architecture validation cannot find component target ${target}")
        return()
    endif()

    set(component_root "${PROJECT_SOURCE_DIR}/libs/${component}")
    file(
        GLOB_RECURSE public_files
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        "${component_root}/include/*.h"
        "${component_root}/include/*.hpp"
    )
    foreach(public_file IN LISTS public_files)
        set(public_target ${target})
        if(component STREQUAL "course")
            get_filename_component(public_name "${public_file}" NAME_WE)
            if(public_name IN_LIST DB_COURSE_TOPIC_NAMES)
                set(public_target "db_course_${public_name}")
            endif()
        endif()
        _db_validate_component_file(
            ${public_target}
            "${component}"
            PUBLIC
            "${public_file}"
        )
    endforeach()

    file(
        GLOB_RECURSE private_files
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        "${component_root}/src/*.cc"
        "${component_root}/src/*.cpp"
        "${component_root}/src/*.h"
        "${component_root}/src/*.hpp"
    )
    foreach(private_file IN LISTS private_files)
        _db_validate_component_file(${target} "${component}" PRIVATE "${private_file}")
    endforeach()
endfunction()

function(_db_direct_target_component_dependencies target output_variable)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        get_target_property(link_items ${target} INTERFACE_LINK_LIBRARIES)
    else()
        get_target_property(link_items ${target} LINK_LIBRARIES)
    endif()
    if(NOT link_items)
        set(link_items)
    endif()

    set(dependencies)
    foreach(candidate_component IN LISTS DB_COMPONENT_NAMES)
        foreach(link_item IN LISTS link_items)
            _db_link_item_names_component(
                "${link_item}"
                "${candidate_component}"
                matches
            )
            if(matches)
                list(APPEND dependencies "${candidate_component}")
                break()
            endif()
        endforeach()
    endforeach()
    set(${output_variable} "${dependencies}" PARENT_SCOPE)
endfunction()

function(_db_direct_component_dependencies component output_variable)
    _db_direct_target_component_dependencies("db_${component}" dependencies)
    if(component STREQUAL "course")
        foreach(topic IN LISTS DB_COURSE_TOPIC_NAMES)
            _db_direct_target_component_dependencies(
                "db_course_${topic}"
                topic_dependencies
            )
            list(APPEND dependencies ${topic_dependencies})
        endforeach()
        list(REMOVE_DUPLICATES dependencies)
    endif()
    set(${output_variable} "${dependencies}" PARENT_SCOPE)
endfunction()

function(_db_validate_component_dag)
    set(remaining ${DB_COMPONENT_NAMES})
    while(remaining)
        set(progress FALSE)
        set(next_remaining ${remaining})
        foreach(component IN LISTS remaining)
            _db_direct_component_dependencies("${component}" dependencies)
            set(depends_on_remaining FALSE)
            foreach(dependency IN LISTS dependencies)
                if(dependency IN_LIST remaining)
                    set(depends_on_remaining TRUE)
                    break()
                endif()
            endforeach()
            if(NOT depends_on_remaining)
                list(REMOVE_ITEM next_remaining "${component}")
                set(progress TRUE)
            endif()
        endforeach()
        if(NOT progress)
            string(JOIN ", " cycle_candidates ${remaining})
            message(
                SEND_ERROR
                "DB component target dependencies contain a cycle among: ${cycle_candidates}"
            )
            return()
        endif()
        set(remaining ${next_remaining})
    endwhile()
endfunction()

function(_db_validate_component_dependency_ceiling)
    foreach(component IN LISTS DB_COMPONENT_NAMES)
        _db_direct_component_dependencies("${component}" dependencies)
        set(allowed_variable "DB_ALLOWED_COMPONENT_DEPENDENCIES_${component}")
        set(allowed_dependencies ${${allowed_variable}})
        foreach(dependency IN LISTS dependencies)
            if(NOT dependency IN_LIST allowed_dependencies)
                message(
                    SEND_ERROR
                    "db_${component} directly links db::${dependency}, but that edge is "
                    "not approved by the component dependency ceiling"
                )
            endif()
        endforeach()
    endforeach()
endfunction()

function(db_validate_architecture)
    foreach(component IN LISTS DB_COMPONENT_NAMES)
        _db_validate_component("${component}")
    endforeach()
    _db_validate_component_dependency_ceiling()
    _db_validate_component_dag()
endfunction()
