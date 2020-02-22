/*--------------------------------------------------------------------
Copyright 2020 fukuen

Audio streaming demo is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This software is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with This software.  If not, see
<http://www.gnu.org/licenses/>.
--------------------------------------------------------------------*/

#include <Arduino.h>
#include <Sipeed_ST7789.h>
#include <stdio.h>
#include <WiFiEsp32.h>
#include <IPAddress.h>
#include <Print.h>
#include <SPI.h>
//#include "base64.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sysctl.h"
#include "plic.h"
#include "uarths.h"
#include "util/g_def.h"
#include "i2s.h"
#include "fpioa.h"

#include <exception>
#include "sha1.hpp"
#include "Base64.h"

#define FRAME_LEN 512

uint16_t rx_buf[FRAME_LEN * 2];
uint32_t g_rx_dma_buf[FRAME_LEN * 4 + 4];

volatile uint32_t g_index;
volatile uint8_t uart_rec_flag;
volatile uint32_t receive_char;
volatile uint8_t i2s_rec_flag;
volatile uint8_t i2s_start_flag = 0;

SPIClass spi_(SPI0); // MUST be SPI0 for Maix series on board LCD
Sipeed_ST7789 lcd(320, 240, spi_);

char SSID[] = "<ssid>";            // your network SSID (name)
char pass[] = "<password>";        // your network password
int status = WL_IDLE_STATUS;

WiFiEspServer server = WiFiEspServer(80);
// use a ring buffer to increase speed and reduce memory allocation
EspRingBuffer buf(64);

WiFiEspClient client;

#define BODY1 \
  "<!DOCTYPE html>\n"\
  "<html>\n"\
  "<head> <meta content=\"text/html\" charset=\"UTF-8\"> </head>\n"\
  "<body>\n"\
  "<script type=\"text/javascript\">\n"\
  "\n"\
  "console.log(\"request ws\");\n"\
  "var ws = new WebSocket('ws://"

#define BODY2 \
  "/ws'),\n"\
  "  ctx = new (window.AudioContext||window.webkitAudioContext),\n"\
  "  initial_delay_sec = 0,\n"\
  "  scheduled_time = 0;\n"\
  "\n"\
  "function playChunk(audio_src, scheduled_time) {\n"\
  "  if (audio_src.start) {\n"\
  "    audio_src.start(scheduled_time);\n"\
  "  } else {\n"\
  "    audio_src.noteOn(scheduled_time);\n"\
  "  }\n"\
  "}\n"\
  "\n"\
  "function playAudioStream(audio_f32) {\n"\
  "  var audio_buf = ctx.createBuffer(1, audio_f32.length, 8000),\n"\
  "  audio_src = ctx.createBufferSource(),\n"\
  "    current_time = ctx.currentTime;\n"\
  "\n"\
  "  var buf = audio_buf.getChannelData(0);\n"\
  "  for (var i = 0; i < audio_f32.length; i++) {\n"\
  "    buf[i] = audio_f32[i] / 32768 - 1;\n"\
  "  }\n"\
  "\n"\
  "  audio_src.buffer = audio_buf;\n"\
  "  audio_src.connect(ctx.destination);\n"\
  "\n"\
  "  if (current_time < scheduled_time) {\n"\
  "    playChunk(audio_src, scheduled_time);\n"\
  "    scheduled_time += audio_buf.duration;\n"\
  "  } else {\n"\
  "    playChunk(audio_src, current_time);\n"\
  "    scheduled_time = current_time + audio_buf.duration + initial_delay_sec;\n"\
  "  }\n"\
  "}\n"\
  "\n"\
  "ws.binaryType = 'arraybuffer';\n"\
  "\n"\
  "ws.onopen = function() {\n"\
  "  console.log('open');\n"\
  "};\n"\
  "\n"\
  "ws.onerror = function(e) {\n"\
  "  //console.log(String(e));\n"\
  "  console.log(e);\n"\
  "};\n"\
  "\n"\
  "ws.onmessage = function(evt) {\n"\
  "  if (evt.data.constructor !== ArrayBuffer) throw 'expecting ArrayBuffer';\n"\
  "  playAudioStream(new Uint16Array(evt.data));\n"\
  "};\n"\
  "\n"\
  "</script>\n"\
  "</body>\n"\
  "</html>\n"

void printWifiStatus() {
  // print the SSID of the network you're attached to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print where to go in the browser
  Serial.println();
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
  Serial.println();
}

int i2s_dma_irq(void *ctx) {
    uint32_t i;
    if(i2s_start_flag)
    {
        int16_t s_tmp;
        if(g_index)
        {
            i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[g_index], FRAME_LEN * 2, DMAC_CHANNEL1);
            g_index = 0;
            for(i = 0; i < FRAME_LEN; i++)
            {
                s_tmp = (int16_t)(g_rx_dma_buf[2 * i] & 0xffff); //g_rx_dma_buf[2 * i + 1] Low left
                rx_buf[i] = s_tmp + 32768;
            }
            i2s_rec_flag = 1;
        }
        else
        {
            i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[0], FRAME_LEN * 2, DMAC_CHANNEL1);
            g_index = FRAME_LEN * 2;
            for(i = FRAME_LEN; i < FRAME_LEN * 2; i++)
            {
                s_tmp = (int16_t)(g_rx_dma_buf[2 * i] & 0xffff);//g_rx_dma_buf[2 * i + 1] Low left
                rx_buf[i] = s_tmp + 32768;
            }
            i2s_rec_flag = 2;
        }
    }
    else
    {
        i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[0], FRAME_LEN * 2, DMAC_CHANNEL1);
        g_index = FRAME_LEN * 2;
    }
    return 0;
}

int pio_init() {
    fpioa_set_function(MIC_DAT3, FUNC_I2S0_IN_D0);
    fpioa_set_function(MIC_WS, FUNC_I2S0_WS);
    fpioa_set_function(MIC_BCK, FUNC_I2S0_SCLK);

    //i2s init
    i2s_init(I2S_DEVICE_0, I2S_RECEIVER, 0x03);

    i2s_rx_channel_config(I2S_DEVICE_0, I2S_CHANNEL_0,
            RESOLUTION_16_BIT, SCLK_CYCLES_16,
            TRIGGER_LEVEL_4, STANDARD_MODE);

    i2s_set_sample_rate(I2S_DEVICE_0, 8000);

    plic_init();
    dmac_init();
    dmac_set_irq(DMAC_CHANNEL1, i2s_dma_irq, NULL, 4);
    i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[0], FRAME_LEN * 2, DMAC_CHANNEL1);

    /* Enable the machine interrupt */
    sysctl_enable_irq();
    return 0;
}

void hexprint(String& hex) {
    const char* hexstring = hex.c_str();
    char *pos = (char*) hexstring;

    Serial.printf("length: %d\n", hex.length());
    Serial.printf("0x");
    for (int count = 0; count < hex.length(); count++) {
        Serial.printf("%02x", (uint8_t )hexstring[count]);
    }
    Serial.printf("\n");

    return;
}

void sendHttpResponse(WiFiEspClient client) {
  IPAddress ip = WiFi.localIP();
  char adrs[16];
  sprintf(adrs, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  int len = sizeof(BODY1) + sizeof(adrs) + sizeof(BODY2);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: text/html");
  client.printf("Content-lengzh: %d\n", len);
  client.println("Connection: close");
  client.println();

  client.print(BODY1);
  client.print(adrs);
  client.print(BODY2);
  client.println();
}

void sendStream(WiFiEspClient client) {
  int first = 1;
  while (client.connected()) {
    if (i2s_rec_flag != 0) {
      if (i2s_rec_flag == 1) {
          client.write((uint8_t)0x82);
          client.write((uint8_t)126);
          client.write((uint8_t)0x04);
          client.write((uint8_t)0x00);
          client.write((const uint8_t *)&rx_buf[0], (size_t) FRAME_LEN * 2);
      } else {
          client.write((uint8_t)0x82);
          client.write((uint8_t)126);
          client.write((uint8_t)0x04);
          client.write((uint8_t)0x00);
          client.write((const uint8_t *)&rx_buf[FRAME_LEN], (size_t) FRAME_LEN * 2);
      }
    }
    i2s_rec_flag = 0;
  }
}

void sendStream2(WiFiEspClient client) {
  int first = 1;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      Serial.print(c, HEX);
    }
    Serial.println("send");
    if (first == 1) {
      first = 0;
      client.write((uint8_t)0x02);
    } else {
      client.write((uint8_t)0x00);
    }
    client.write((uint8_t)0x04);
    client.write((uint8_t)0x44);
    client.write((uint8_t)0x44);
    client.write((uint8_t)0x44);
    client.write((uint8_t)0x44);
    sleep(0.1);
  }
}

void sendWsResponse(WiFiEspClient client, String& header) {
  String key = "";
  int pos1 = header.indexOf("Sec-WebSocket-Key");
  if (pos1 > -1) {
    String tmp = header.substring(pos1 + 18);
    key = tmp.substring(0, tmp.indexOf("\r"));
    key.trim();
  }
  String magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  SHA1 sha1;
  sha1.update(key + magic);
  String acc = sha1.final();
  Serial.println(key);
  hexprint(acc);
  char accept[100];
  int len = base64_encode(accept, (char *)acc.c_str(), (int)acc.length());
  Serial.println(accept);

  client.println("HTTP/1.1 101 Switching Protocols");
  client.println("Upgrade: websocket");
  client.println("Connection: Upgrade");
  client.printf("Sec-WebSocket-Accept: %s", accept);
  client.println();
  client.println();

  Serial.println("header written");

  sendStream(client);
}


void setup() {
  pll_init();
  uarths_init();

  lcd.begin(15000000, COLOR_BLACK);
  lcd.setTextColor(0xffff);
  lcd.println("Starting...");

  Serial.begin(115200);   // initialize serial for debugging
  WiFi.init();    // initialize ESP32 module

  // check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }

  // attempt to connect to WiFi network
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(SSID);
    // Connect to WPA/WPA2 network
    status = WiFi.begin(SSID, pass);
  }

  Serial.println("You're connected to the network");
  printWifiStatus();

  IPAddress ip = WiFi.localIP();
  lcd.print("Show http://");
  lcd.print(ip);
  lcd.println("/");

  // start the web server on port 80
  server.begin();

  pio_init();

  g_index = 0;
  i2s_rec_flag = 0;
  i2s_start_flag = 1;
}

void loop() {
  client = server.available();  // listen for incoming clients

  if (client) {
    Serial.println("New client");
    String header = "";
    buf.init();
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        buf.push(c);
        header += c;

        // you got two newline characters in a row
        // that's the end of the HTTP request, so send a response
        if (buf.endsWith("\r\n\r\n")) {
          if (header.indexOf("/ws") > -1) {
            sendWsResponse(client, header);
          } else if (header.indexOf("/") > -1) {
            sendHttpResponse(client);
            buf.reset();
            // close the connection
            client.stop();
            break;
          }
        }

      }
    }
    buf.reset();
    // close the connection
    client.stop();
    Serial.println("Client disconnected");
  }
}
