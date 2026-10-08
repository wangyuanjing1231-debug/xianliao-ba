/*
 * ESP32-S3 低功耗蓝牙 USB 接收器（HCI over USB 串口，H4 格式）
 *
 * 电脑 → 板子：从 USB 串口收字节，按 H4 格式拼成完整的 HCI 包，交给蓝牙控制器。
 * 板子 → 电脑：蓝牙控制器吐出的 HCI 包（已带 H4 类型字节）原样写回 USB 串口。
 * 电脑端：sudo btattach -B /dev/ttyACM0 -P h4 -N
 */
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_bt.h"
#include "nvs_flash.h"
#include "driver/usb_serial_jtag.h"

#define H4_CMD 0x01
#define H4_ACL 0x02
#define H4_SCO 0x03
#define H4_ISO 0x05

#define PKT_MAX 1100

static RingbufHandle_t s_to_host;
static SemaphoreHandle_t s_ctrl_ready;

static void on_ctrl_ready(void)
{
    xSemaphoreGive(s_ctrl_ready);
}

static int on_ctrl_packet(uint8_t *data, uint16_t len)
{
    xRingbufferSend(s_to_host, data, len, pdMS_TO_TICKS(100));
    return 0;
}

static const esp_vhci_host_callback_t s_vhci_cb = {
    .notify_host_send_available = on_ctrl_ready,
    .notify_host_recv = on_ctrl_packet,
};

static void send_to_ctrl(uint8_t *pkt, uint16_t len)
{
    while (!esp_vhci_host_check_send_available()) {
        xSemaphoreTake(s_ctrl_ready, pdMS_TO_TICKS(10));
    }
    esp_vhci_host_send_packet(pkt, len);
}

static int header_len(uint8_t type)
{
    switch (type) {
    case H4_CMD: return 3;
    case H4_ACL: return 4;
    case H4_SCO: return 3;
    case H4_ISO: return 4;
    default:     return -1;
    }
}

static int payload_len(const uint8_t *p)
{
    switch (p[0]) {
    case H4_CMD: return p[3];
    case H4_ACL: return p[3] | (p[4] << 8);
    case H4_SCO: return p[3];
    case H4_ISO: return (p[3] | (p[4] << 8)) & 0x3fff;
    default:     return -1;
    }
}

/* 电脑 → 控制器 */
static void usb_to_ctrl_task(void *arg)
{
    static uint8_t pkt[PKT_MAX];
    uint8_t chunk[256];
    int pos = 0, need = 0;
    bool have_header = false;

    for (;;) {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), portMAX_DELAY);
        for (int i = 0; i < n; i++) {
            uint8_t b = chunk[i];
            if (pos == 0) {
                int h = header_len(b);
                if (h < 0) {
                    continue;   /* 不是包开头，丢掉这个字节，等下一个包开头 */
                }
                need = 1 + h;
                have_header = false;
            }
            pkt[pos++] = b;
            if (!have_header && pos == need) {
                have_header = true;
                need += payload_len(pkt);
                if (need > PKT_MAX) {
                    pos = 0;    /* 长度不合理，整包丢掉 */
                    continue;
                }
            }
            if (have_header && pos == need) {
                send_to_ctrl(pkt, (uint16_t)pos);
                pos = 0;
            }
        }
    }
}

/* 控制器 → 电脑 */
static void ctrl_to_usb_task(void *arg)
{
    for (;;) {
        size_t len = 0;
        uint8_t *item = xRingbufferReceive(s_to_host, &len, portMAX_DELAY);
        if (item) {
            usb_serial_jtag_write_bytes(item, len, pdMS_TO_TICKS(500));
            vRingbufferReturnItem(s_to_host, item);
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_cfg.rx_buffer_size = 4096;
    usb_cfg.tx_buffer_size = 4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));

    s_to_host = xRingbufferCreate(16384, RINGBUF_TYPE_NOSPLIT);
    s_ctrl_ready = xSemaphoreCreateBinary();
    configASSERT(s_to_host && s_ctrl_ready);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_vhci_host_register_callback(&s_vhci_cb));

    xTaskCreate(ctrl_to_usb_task, "ctrl_to_usb", 4096, NULL, 10, NULL);
    xTaskCreate(usb_to_ctrl_task, "usb_to_ctrl", 4096, NULL, 10, NULL);
}
