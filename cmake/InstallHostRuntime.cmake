include_guard(GLOBAL)
function(tinyengine_install_host_runtime target component)
    install(TARGETS ${target} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT ${component})
    # Imported shared SDL targets and other modeled shared dependencies belong
    # beside each host, including when SDL was found rather than built static.
    install(FILES $<TARGET_RUNTIME_DLLS:${target}>
        DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT ${component})
endfunction()
