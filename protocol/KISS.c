#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "KISS.h"
#if CRC_KISS == CRC_SMACK
    #include "util/CRC16.h"
#endif

static uint8_t serialBuffer[AX25_MAX_FRAME_LEN]; // Buffer for holding incoming serial data
AX25Ctx *ax25ctx;
Afsk *channel;
Serial *serial;
size_t frame_len;
bool IN_FRAME;
bool ESCAPE;
#if CRC_KISS == CRC_SMACK
  bool SMACK;
  bool SMACK_SEND;
  uint16_t crc_in;
  uint16_t crc_out;
#endif

uint8_t command = CMD_UNKNOWN;
unsigned long custom_preamble = CONFIG_AFSK_PREAMBLE_LEN;
unsigned long custom_tail = CONFIG_AFSK_TRAILER_LEN;

unsigned long slotTime = 200;
uint8_t p = 63;

void kiss_init(AX25Ctx *ax25, Afsk *afsk, Serial *ser) {
    ax25ctx = ax25;
    serial = ser;
    channel = afsk;
  #if CRC_KISS == CRC_SMACK
    SMACK_SEND = false;
  #endif
  #ifdef KISS_BLN_INIT
    fputc(FEND, &serial->uart0);
    fputc(0x00, &serial->uart0);
    printf_P(PSTR("\x82\xa0\xaa\x82\xa0\xa4\x60\xaa\x82\xa0\xa4\xa6\x40\x7e\xae\x92\x88\x8a\x62\x40"
                  "\xe1\x03\xf0\x3a\x42\x4c\x4e\x39\x20\x20\x20\x20\x20\x3a\x4d\x69\x63\x72\x6f\x41"
                  "\x50\x52\x53\x20\x4b\x49\x53\x53\x20\x53\x74\x61\x72\x74\x65\x64\x2e\x2e\x2e"));
    fputc(FEND, &serial->uart0);
  #endif
}

void kiss_messageCallback(AX25Ctx *ctx) {
    fputc(FEND, &serial->uart0);
  #if CRC_KISS == CRC_SMACK
    if (SMACK_SEND) {
        fputc(0x80, &serial->uart0);
        crc_out = CRC16_INIT_VAL;
        crc_out = update_crc16(0x80, crc_out);
    } else {
       fputc(0x00, &serial->uart0);
    }
  #else
    fputc(0x00, &serial->uart0);
  #endif
    for (unsigned i = 0; i < ctx->frame_len-2; i++) {
        uint8_t b = ctx->buf[i];
      #if CRC_KISS == CRC_SMACK
        if (SMACK_SEND) {
            crc_out = update_crc16(b, crc_out);
        }
      #endif 
        if (b == FEND) {
            fputc(FESC, &serial->uart0);
            fputc(TFEND, &serial->uart0);
        } else if (b == FESC) {
            fputc(FESC, &serial->uart0);
            fputc(TFESC, &serial->uart0);
        } else {
            fputc(b, &serial->uart0);
        }
    }
  #if CRC_KISS == CRC_SMACK
    //It's horrible duplicate code, but.....
    //I can't find a better way without waste RAM or
    //slow the code execution. I hope that someone
    //will have a better idea.
    if (SMACK_SEND) {
         // ESCAPE and send low byte crc and high byte crc
         uint8_t crc = (crc_out & 0xff) ^ 0xff;
         if (crc == FEND) {
             fputc(FESC, &serial->uart0);
             fputc(TFEND, &serial->uart0);
         } else if (crc == FESC) {
             fputc(FESC, &serial->uart0);
             fputc(TFESC, &serial->uart0);
         } else {
            fputc(crc, &serial->uart0);
         }
         crc = (ctx->crc_out >> 8) ^ 0xff;
         if (crc == FEND) {
             fputc(FESC, &serial->uart0);
             fputc(TFEND, &serial->uart0);
         } else if (crc == FESC) {
             fputc(FESC, &serial->uart0);
             fputc(TFESC, &serial->uart0);
         } else {
            fputc(crc, &serial->uart0);
         }
    }
  #endif
    fputc(FEND, &serial->uart0);
}

void kiss_csma(AX25Ctx *ctx, uint8_t *buf, size_t len) {
    bool sent = false;
    while (!sent) {
        //puts("Waiting in CSMA");
        if(!channel->hdlc.receiving) {
            uint8_t tp = rand() & 0xFF;
            if (tp < p) {
                ax25_sendRaw(ctx, buf, len);
                sent = true;
            } else {
                ticks_t start = timer_clock();
                long slot_ticks = ms_to_ticks(slotTime);
                while (timer_clock() - start < slot_ticks) {
                    cpu_relax();
                }
            }
        } else {
            while (!sent && channel->hdlc.receiving) {
                // Continously poll the modem for data
                // while waiting, so we don't overrun
                // receive buffers
                ax25_poll(ax25ctx);

                if (channel->status != 0) {
                    // If an overflow or other error
                    // occurs, we'll back off and drop
                    // this packet silently.
                    channel->status = 0;
                    sent = true;
                }
            }
        }

    }
    
}

void kiss_serialCallback(uint8_t sbyte) {
    if (IN_FRAME && sbyte == FEND && command == CMD_DATA) {
        IN_FRAME = false;
      #if CRC_KISS == CRC_SMACK
        //IF SMACK check CRC. If OK set SMACK_SEND and process frame
        //If wrong CRC discard frame. 
        if (SMACK) {
            if (crc_in == CRC16_INIT_VAL) {
                 SMACK_SEND = true;
                 kiss_csma(ax25ctx, serialBuffer, frame_len);
            }
        } else {
            kiss_csma(ax25ctx, serialBuffer, frame_len);
        }
      #else
        kiss_csma(ax25ctx, serialBuffer, frame_len);
      #endif
    } else if (sbyte == FEND) {
        IN_FRAME = true;
        command = CMD_UNKNOWN;
        frame_len = 0;
    } else if (IN_FRAME && frame_len < AX25_MAX_FRAME_LEN) {
        // Have a look at the command byte first
        if (frame_len == 0 && command == CMD_UNKNOWN) {
            // MicroModem supports only one HDLC port, so we
            // strip off the port nibble of the command byte
        #if CRC_KISS == CRC_SMACK
            if (sbyte & 0x80){
                SMACK = true;
                crc_in = CRC16_INIT_VAL;
                crc_in = update_crc16(sbyte, crc_in);
            } else {
                SMACK = false;
            }
        #endif
            sbyte = sbyte & 0x0F;
            command = sbyte;
        } else if (command == CMD_DATA) {
            if (sbyte == FESC) {
                ESCAPE = true;
            } else {
                if (ESCAPE) {
                    if (sbyte == TFEND) sbyte = FEND;
                    if (sbyte == TFESC) sbyte = FESC;
                    ESCAPE = false;
                }
              #if CRC_KISS == CRC_SMACK
                if (SMACK) {
                    crc_in = update_crc16(sbyte, crc_in);
                }
              #endif
                serialBuffer[frame_len++] = sbyte;
            }
        } else if (command == CMD_TXDELAY) {
            custom_preamble = sbyte * 10UL;
        } else if (command == CMD_TXTAIL) {
            custom_tail = sbyte * 10;
        } else if (command == CMD_SLOTTIME) {
            slotTime = sbyte * 10;
        } else if (command == CMD_P) {
            p = sbyte;
        } 
        
    }
}
