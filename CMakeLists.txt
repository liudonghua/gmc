set(srcs
    "audiocontrol.c"
    "broadcast.c"
    "bt.c"
    "device.c"
    "email.c"
    "eq.c"
    "gmc.c"
    "network.c"
    "protocol.c"
    "uart.c"
    "user.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES json nvs_flash esp_wifi esp_netif bt efuse esp_driver_uart driver)
