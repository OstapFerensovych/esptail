set(COMPONENT_SRCS "main.c" "utils.c" "serial.c" "loki.c" "webconfig.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

set(COMPONENT_EMBED_FILES "esp-tail.png")
set(COMPONENT_EMBED_TXTFILES "index.html")

register_component()