idf_component_register(SRCS "fml_wifisniffer.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi nvs_flash fml_sharedata)
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
