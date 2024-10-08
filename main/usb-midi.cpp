#include <stdio.h>
#include <iostream>
#include "string.h"
#include <array>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "usb/usb_host.h"

#include "host.h"
#include "device.h"

#include "usb_midi.h"
#include "launchpad-mini-mk3.h"

void open_session();
void close_session();

using namespace usb;
static Host host;
static Device device;

MIDI *midi;

static void host_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *arg);
void lvgl_open_midi();

static bool ready = false;

static void task(void *)
{
    auto buf = (uint8_t *)malloc(512);
    while (1)
    {
        if (ready)
        {
            auto len = midi->read(buf, 512);
            if (len)
            {
                auto arr = std::array<uint8_t, 512>();
                memcpy(arr.data(), buf, len);
                auto tpl = std::tuple<int, std::array<uint8_t, 512>>(len, arr);
                auto err = esp_event_post_to(usb::event_loop, USB_MIDI_BASE_EVENT, 0xffff, &tpl, sizeof(tpl), 0);
            }
        }
        else
        {
            vTaskDelete(NULL);
        }
    }
}

void init_midi_host(void)
{
    host.init();

    esp_event_handler_instance_register_with(usb::event_loop, USB_HOST_BASE_EVENT, ESP_EVENT_ANY_ID, host_event_handler, NULL, NULL);

    host.addDevice(device);

    vTaskPrioritySet(NULL, 1);
    while (1)
    {
        device.event();
    }
}
static bool daw_resp = false;

static void host_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *arg)
{
    if (event_base == USB_HOST_BASE_EVENT)
    {
        switch (event_id)
        {
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
        {
            close_session();
            ready = false;
            if (midi)
            {
                midi->release();
                delete midi;
            }
            device.close();
            break;
        }
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
        {
            midi = new MIDI(device);
            midi->claim();
            if (midi->connected())
            {
                open_session();
                vTaskDelay(10);
                lvgl_open_midi();
                ready = true;
                xTaskCreate(task, "midi", 4000, NULL, 1, NULL);
            }
            else
            {
                midi->release();
                delete midi;
                midi = nullptr;
            }
        }
        break;
        default:
            ESP_LOGW("main", "event: 0x%lx", event_id);
            break;
        }
    }
}
