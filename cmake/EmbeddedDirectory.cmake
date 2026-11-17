# Track directory membership as well as the contents of each embedded file.
# Removing a dependency alone does not make Ninja relink an existing target.
function(dynlex_embedded_directory_dependencies directory manifest result)
    file(GLOB_RECURSE embedded_files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${directory}/*")
    list(JOIN embedded_files "\n" embedded_membership)
    file(GENERATE OUTPUT "${manifest}" CONTENT "${embedded_membership}\n")
    set(${result} ${embedded_files} "${manifest}" PARENT_SCOPE)
endfunction()
