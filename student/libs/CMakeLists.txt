# Component libraries are registered explicitly so the dependency graph stays auditable.
# Do not glob directories or silently add every child: explicit registration is
# part of the dependency graph contract.

add_subdirectory(base)
add_subdirectory(types)
add_subdirectory(io)
add_subdirectory(protocol)
add_subdirectory(storage)
add_subdirectory(wal)
add_subdirectory(transaction)
add_subdirectory(catalog)
add_subdirectory(access)
add_subdirectory(sql)
add_subdirectory(planner)
add_subdirectory(execution)
add_subdirectory(recovery)
add_subdirectory(engine)
add_subdirectory(api)
add_subdirectory(course)
