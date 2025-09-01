/*
 * Copyright (c) 2019 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * Based on ST7789V sample:
 * Copyright (c) 2019 Marc Reilly
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>

// IBM like terminal font 8x16px stored in src/x11font_8x16.c
extern const unsigned char ibm_like816[];

// Image struct, exported with GIMP
typedef struct {
  unsigned int   width;
  unsigned int   height;
  unsigned int   bytes_per_pixel; /* 2:RGB16, 3:RGB, 4:RGBA */
  char          *comment;
  unsigned char  pixel_data[240 * 320 * 2 + 1];
} c_image;

// Images from src/picture_silabs_*.c
extern const c_image silabs_logo;
extern const c_image silabs_gecko;
extern const c_image silabs_efr32;

// Macro to convert RGB888 to RGB565 format
// Use input values 0..255 for r, g, b:
#define RGB_TO_RGB565(r, g, b)  ((uint16_t)             \
                                 ((((r) & 0xF8) << 8)   \
                                  | (((g) & 0xFC) << 3) \
                                  | (((b) & 0xF8) >> 3)))

#define COLOR_RGB_565_WHITE     (uint16_t) 0xffff
#define COLOR_RGB_565_BLACK     (uint16_t) 0x0000
#define COLOR_RGB_565_RED       RGB_TO_RGB565(255, 0, 0)
#define COLOR_RGB_565_GREEN     RGB_TO_RGB565(0, 255, 0)
#define COLOR_RGB_565_BLUE      RGB_TO_RGB565(0, 0, 255)

// Each pixel is defined in 2 bytes
#define BYTES_PER_PIXEL         2

// Font definitions to make life easier
#define FONT_ARRAY              ibm_like816
#define FONT_WIDTH              8
#define FONT_HEIGHT             16
// Charecter buffer size
#define CHAR_BUF_SIZE           (FONT_WIDTH * FONT_HEIGHT * BYTES_PER_PIXEL)

// How many characters can be displayed (rows and cols)
#define DISPLAY_FONT_MAX_CHAR_X 30
#define DISPLAY_FONT_MAX_CHAR_Y 20

void display_write_char(const struct device *dev,
                        char ch,
                        uint16_t color,
                        uint16_t bg_color)
{
  static int16_t pos_x = 0, pos_y = 0;
  static uint16_t char_buf[CHAR_BUF_SIZE];
  static struct display_buffer_descriptor char_buf_desc = {
    .buf_size = CHAR_BUF_SIZE,
    .pitch = FONT_WIDTH,
    .width = FONT_WIDTH,
    .height = FONT_HEIGHT,
    .frame_incomplete = true,
  };
  uint16_t pix;

  if (ch == '\0') {
    // In this case set the cursor to start
    pos_x = 0;
    pos_y = 0;
  }

  if ((pos_x == 0) && (pos_y == 0)) {
    // Clean the display if the cursor is top left
    for (uint16_t i = 0; i < char_buf_desc.buf_size; i++) {
      char_buf[i] = bg_color;
    }
    // and cover the display with background
    for (uint16_t j = 0; j < DISPLAY_FONT_MAX_CHAR_Y; j++) {
      for (uint16_t i = 0; i < DISPLAY_FONT_MAX_CHAR_X; i++) {
        // It would be more efficient with larger buffer
        display_write(dev,
                      i * FONT_WIDTH,
                      j * FONT_HEIGHT,
                      &char_buf_desc,
                      char_buf);
      }
    }
  }

  if (ch == '\0') {
    return;
  }

  if (ch == '\r') {
    // Case of new line
    pos_x = 0;
    pos_y++;
  } else {
    // Draw into the buffer the current character font
    // The font has 1 bit per pixel, the buffer has 2 bytes per pixel
    for (uint8_t j = 0; j < FONT_HEIGHT; j++) {
      for (uint8_t i = 0; i < FONT_WIDTH; i++) {
        if (FONT_ARRAY[((uint32_t)ch << 4) + j] & (0x80 >> i)) {
          pix = color;
        } else {
          pix = bg_color;
        }
        char_buf[j * FONT_WIDTH + i] = pix;
      }
    }

    // Send the buffer to the display
    display_write(dev,
                  pos_x * FONT_WIDTH,
                  pos_y * FONT_HEIGHT,
                  &char_buf_desc,
                  char_buf);

    // Step the position for next character
    pos_x++;

    // In case of reaching the end of line:
    if (pos_x == DISPLAY_FONT_MAX_CHAR_X) {
      // Step to he new line
      pos_x = 0;
      pos_y++;
    }
  }

  // In che case of reaching the end of display:
  if (pos_y == DISPLAY_FONT_MAX_CHAR_Y) {
    pos_y = 0;
  }
}

void display_clean_set_bg(uint16_t bg_color,
                          const struct device *dev,
                          const struct display_capabilities *display_cap,
                          uint16_t *buf,
                          const struct display_buffer_descriptor *buf_desc)
{
  // Fill up the buffer with background color
  for (uint16_t i = 0; i < buf_desc->buf_size; i++) {
    buf[i] = bg_color;
  }

  // Cover the entry display with buffer
  for (int y = 0; y < display_cap->y_resolution; y += buf_desc->height) {
    // y is the position where we currently update
    // the display with the partial buffer:
    display_write(dev, 0, y, buf_desc, buf);
  }
}

// Display and serial devices from DTS
static const struct device *const display_dev =
  DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const uart_dev =
  DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

volatile char uart_new_ch;

// Keywords for easter egg function (to show pictures)
char keyword_gecko[] = "gecko";
char keyword_silabs[] = "silabs";
char keyword_efr32[] = "efr32";

enum states_t {
  show_welcome,
  waiting_for_uart_input,
  new_char_arrived,
  enter_keyword,
  finished_keyword_silabs,
  finished_keyword_gecko,
  finished_keyword_efr32,
  wait
};

volatile enum states_t demo_app_state = show_welcome;

char welcome_string1[] = "Hi!\r";
char welcome_string2[] =
  "Send something on UART and it will displayed here\r(or try to type gecko;)\r";

// UART callback function
void uart_cb(const struct device *dev, void *user_data)
{
  uint8_t c;

  if (!uart_irq_update(uart_dev)) {
    return;
  }

  if (!uart_irq_rx_ready(uart_dev)) {
    return;
  }

  while (uart_fifo_read(uart_dev, &c, 1) == 1) {
    uart_new_ch = c;
    demo_app_state = new_char_arrived;
  }
}

int uart_init()
{
  int ret;

  if (!device_is_ready(uart_dev)) {
    LOG_ERR("Device %s not found. Exiting.",
            uart_dev->name);
    return -1;
  }

  // Set UART interrupt callback
  ret = uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
  if (ret < 0) {
    if (ret == -ENOTSUP) {
      printk("Interrupt-driven UART API support not enabled\n");
    } else if (ret == -ENOSYS) {
      printk("UART device does not support interrupt-driven API\n");
    } else {
      printk("Error setting UART callback: %d\n", ret);
    }
    return -1;
  }

  /* Enable rx interrupts */
  uart_irq_rx_enable(uart_dev);

  return 0;
}

int main(void)
{
  // It will be allocated now on Zephyr heap:
  uint16_t *graphical_buf;
  struct display_buffer_descriptor graphical_buf_desc;

  struct display_capabilities display_capabilities;
  struct display_buffer_descriptor image_buf_desc;

  // Position in the keywords, it will be updated when a new character is
  //   received on UART
  uint16_t keyword_pos = 0;

  // Check and init UART
  if (uart_init() < 0) {
    return 0;
  }

  // Check and init SPI display
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Device %s not found. Exiting.", display_dev->name);
    return 0;
  }

  display_blanking_on(display_dev);

  LOG_INF("Display example for %s", display_dev->name);

  display_get_capabilities(display_dev, &display_capabilities);

  // This example application only supports RGB565 pixel format
  if (display_capabilities.current_pixel_format != PIXEL_FORMAT_RGB_565) {
    LOG_ERR("Unsupported pixel format. Exiting.");
    return 0;
  }

  graphical_buf_desc.buf_size = display_capabilities.x_resolution
                                * display_capabilities.y_resolution
                                * BYTES_PER_PIXEL;
  graphical_buf_desc.pitch = display_capabilities.x_resolution;
  graphical_buf_desc.width = display_capabilities.x_resolution;
  graphical_buf_desc.height = display_capabilities.y_resolution;
  graphical_buf_desc.frame_incomplete = false;

  // The frame buffer now allocated on kernel heap
  // Not forget to increase the heap size (eg in prj.conf)
  // CONFIG_HEAP_MEM_POOL_SIZE=192000
  // (and in some cases it would be better to move to globals)
  graphical_buf = k_malloc(graphical_buf_desc.buf_size);
  if (graphical_buf == NULL) {
    LOG_ERR("Could not allocate memory. Exiting.");
    return 0;
  }

  // Fill the buffer with background color
  for (int i = 0; i < 320 * 240; i++) {
    graphical_buf[i] = RGB_TO_RGB565(46, 52, 64);
  }

  // These image buffers are stored in flash
  // here just needs to set the buffer descriptor
  // all images are the same size as the display: 240x320
  image_buf_desc.buf_size = sizeof(silabs_logo.pixel_data);
  image_buf_desc.pitch = silabs_logo.width;
  image_buf_desc.width = silabs_logo.width;
  image_buf_desc.height = silabs_logo.height;
  image_buf_desc.frame_incomplete = false;

  display_write(display_dev, 0, 0, &graphical_buf_desc, graphical_buf);

  display_blanking_off(display_dev);

  LOG_INF("Display starts");

  demo_app_state = show_welcome;

  while (true) {
    switch (demo_app_state)
    {
      case show_welcome:
        // Write welcome to display

        // Set the character position to 0,0
        // It will clean the display
        display_write_char(display_dev,
                           '\0',
                           RGB_TO_RGB565(236, 239, 244),
                           RGB_TO_RGB565(46, 52, 64));

        // Show the greeting text
        for (int i = 0; i < sizeof(welcome_string1) - 1; i++) {
          display_write_char(display_dev, welcome_string1[i],
                             RGB_TO_RGB565(236, 239, 244),
                             RGB_TO_RGB565(46, 52, 64));
        }
        for (int i = 0; i < sizeof(welcome_string2) - 1; i++) {
          display_write_char(display_dev, welcome_string2[i],
                             RGB_TO_RGB565(130, 162, 192),
                             RGB_TO_RGB565(46, 52, 64));
        }

        // This variable tracks the silabs, gecko, efr32 keywords position
        keyword_pos = 0;

        // Init is finished, jump to wait
        demo_app_state = waiting_for_uart_input;
        break;

      case waiting_for_uart_input:
        // do nothing here, just wait
        // demo_app_state will be changed in serial_cb callback function
        break;

      case new_char_arrived:
        // show on display:
        display_write_char(display_dev,
                           uart_new_ch,
                           RGB_TO_RGB565(164, 189, 142),
                           RGB_TO_RGB565(46, 52, 64));

        // echo back:
        uart_poll_out(uart_dev, uart_new_ch);

        // decoding the input if matches any keyword
        if (uart_new_ch == keyword_gecko[keyword_pos]) {
          if (keyword_pos == sizeof(keyword_gecko) - 2) {
            // keyword finished
            keyword_pos = 0;
            demo_app_state = finished_keyword_gecko;
          } else {
            // keyword not yet finished
            keyword_pos++;
            demo_app_state = waiting_for_uart_input;
          }
        } else if (uart_new_ch == keyword_silabs[keyword_pos]) {
          if (keyword_pos == sizeof(keyword_silabs) - 2) {
            // keyword finished
            keyword_pos = 0;
            demo_app_state = finished_keyword_silabs;
          } else {
            // keyword not yet finished
            keyword_pos++;
            demo_app_state = waiting_for_uart_input;
          }
        } else if (uart_new_ch == keyword_efr32[keyword_pos]) {
          if (keyword_pos == sizeof(keyword_efr32) - 2) {
            // keyword finished
            keyword_pos = 0;
            demo_app_state = finished_keyword_efr32;
          } else {
            // keyword not yet finished
            keyword_pos++;
            demo_app_state = waiting_for_uart_input;
          }
        } else {
          // keyword not finished
          // or random character arrived
          keyword_pos = 0;
          demo_app_state = waiting_for_uart_input;
        }
        break;

      case finished_keyword_gecko:
        // show gecko logo
        display_write(display_dev,
                      0,
                      0,
                      &image_buf_desc,
                      silabs_gecko.pixel_data);

        demo_app_state = wait;
        break;

      case finished_keyword_silabs:
        // show silabs logo
        display_write(display_dev, 0, 0, &image_buf_desc,
                      silabs_logo.pixel_data);

        demo_app_state = wait;
        break;

      case finished_keyword_efr32:
        // show efr32 logo
        display_write(display_dev,
                      0,
                      0,
                      &image_buf_desc,
                      silabs_efr32.pixel_data);

        demo_app_state = wait;
        break;

      case wait:
        // wait some time to show the image
        k_msleep(2000);

        // return to start
        demo_app_state = show_welcome;
        break;

      default:
        break;
    }
  }

  return 0;
}
