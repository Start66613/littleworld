# lw_content: the V4.2 on-card content container (album index, memory
# timeline, letters, dialogue). Plain C, no ESP-IDF dependency, so the host
# tests compile the same source.
idf_component_register(SRCS "src/lw_content.c"
    INCLUDE_DIRS "include"
    REQUIRES lw_core)
