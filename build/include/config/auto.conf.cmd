deps_config := \
	/home/paul/Development/esp32_v3/esp-idf/components/app_trace/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/aws_iot/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/bt/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/driver/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/esp32/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/esp_adc_cal/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/esp_event/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/esp_http_client/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/esp_http_server/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/esp_https_ota/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/espcoredump/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/ethernet/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/fatfs/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/freemodbus/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/freertos/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/heap/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/libsodium/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/log/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/lwip/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/mbedtls/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/mdns/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/mqtt/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/nvs_flash/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/openssl/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/pthread/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/spi_flash/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/spiffs/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/tcpip_adapter/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/unity/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/vfs/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/wear_levelling/Kconfig \
	/home/paul/Development/esp32_v3/esp-idf/components/app_update/Kconfig.projbuild \
	/home/paul/Development/esp32_v3/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/paul/Development/esp32_v3/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/paul/Development/esp32_v3/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/paul/Development/esp32_v3/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(IDF_TARGET)" "esp32"
include/config/auto.conf: FORCE
endif
ifneq "$(IDF_CMAKE)" "n"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
