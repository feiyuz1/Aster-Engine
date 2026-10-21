include_guard(GLOBAL)
function(tinyengine_install_host_runtime target component)
    install(TARGETS ${target} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT ${component})
    # Imported shared SDL targets and other modeled shared dependencies belong
    # beside each host, including when SDL was found rather than built static.
    install(FILES $<TARGET_RUNTIME_DLLS:${target}>
        DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT ${component})
    if(MSVC)
        # Native debugging needs the PDB from the exact same link as the host.
        install(FILES "$<TARGET_PDB_FILE:${target}>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" OPTIONAL COMPONENT ${component})
    endif()
endfunction()
