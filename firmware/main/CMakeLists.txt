idf_component_register(
    SRCS
        "main.c"
        "cmd_handler.c"
        "wifi_mgr.c"
        "http_client.c"
        "ws_client.c"
        "html_parser.c"
        "web_config.c"
        "dns_resolver.c"
        "nv_config.c"
        "time_sync.c"
    INCLUDE_DIRS "."
)
