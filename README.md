# gs_pcb_cansat26

Firmware for the CanSat 2026 ground station board: STM32F446RET6 (LQFP64), STM32CubeIDE /
CubeMX project (`gs_pcb_v1.ioc`, FW_F4 V1.28.3), bare metal (no RTOS).

The board is the receiving end of [rf_pcb-fw](https://github.com/STeRG-MIT-WPU/rf_pcb-fw). It
listens on the XBee and the E22 LoRa and bridges each radio to its own USB-UART, so the PC sees
one clean, validated stream per radio. Lines typed on a USB port go back out on that radio as
telecommands, which the RF board relays to the flight computer.

```
 XBee S3 Pro  <-- USART1 115200 -->            <-- USART2 115200 -->  FT231 (A), USB1   LED0
                                     GS board
 E22 868 MHz  <-- USART3 115200 -->            <-- UART5  115200 -->  FT231 (B), USB2   LED1
```

## Hardware

Schematic: `cansat_gs_board_schematics.pdf` (MCU sheet, radio sheet, USB-UART sheet).

- STM32F446RET6, 16 MHz HSE, SYSCLK 168 MHz via PLL (APB1 42 MHz, APB2 84 MHz)
- XBee S3 Pro on USART1, transparent mode
- EBYTE E22 868 MHz on USART3, transparent mode (M0/M1 driven low by the MCU)
- Two FT231XQ USB-UART bridges on USART2 and UART5 (USB-C connectors USB1 and USB2)
- LD29150 3V3 regulator, ST-LINK 2x10 SWD header, BOOT0 slide switch, NRST button
- Footprints for a SIM800L GSM module and an Ra-02 433 MHz LoRa are on the board but not
  populated and not driven by this firmware

## Pin assignment

Nets as named on the schematic MCU sheet.

| Pin  | Net           | Function        | Direction | Notes                                              |
|------|---------------|-----------------|-----------|----------------------------------------------------|
| PA9  | XBEE_TX       | USART1_TX       | out       | XBee DIN                                           |
| PA10 | XBEE_RX       | USART1_RX       | in        | XBee DOUT                                          |
| PA12 | XBEE_RTS      | GPIO output     | out       | driven low: always ready to receive                |
| PA11 | XBEE_CTS      | GPIO input      | in        | not wired to the module on the radio sheet; unused |
| PA8  | XBEE_RSSI     | GPIO input      | in        | PWM from the XBee (also drives LED D8); unused     |
| PC9  | XBEE_ASSO     | GPIO input      | in        | association indicator (also LED D7); unused        |
| PC8  | XBEE_RST      | GPIO open-drain | out       | released (high); R14 pull-up, SW2 manual reset     |
| PB10 | LORA-868_TX   | USART3_TX       | out       | E22 RXD                                            |
| PC5  | LORA-868_RX   | USART3_RX       | in        | E22 TXD                                            |
| PB0  | LORA-868_M0   | GPIO output     | out       | low                                                |
| PB1  | LORA-868_M1   | GPIO output     | out       | low: M0=M1=0 is transparent mode                   |
| PA2  | USBDB1_TX     | USART2_TX       | out       | FT231 (A) RXD, USB1                                |
| PA3  | USBDB1_RX     | USART2_RX       | in        | FT231 (A) TXD                                      |
| PC12 | USBDB2_TX     | UART5_TX        | out       | FT231 (B) RXD, USB2                                |
| PD2  | USBDB2_RX     | UART5_RX        | in        | FT231 (B) TXD                                      |
| PC2  | LED0          | GPIO output     | out       | D3, toggles per valid XBee telemetry packet        |
| PC1  | LED1          | GPIO output     | out       | D4, toggles per valid LoRa telemetry packet        |
| PH0  | HSE_IN        | RCC_OSC_IN      |           | 16 MHz crystal                                     |
| PH1  | HSE_OUT       | RCC_OSC_OUT     |           |                                                    |
| PA13 | SWDIO         |                 |           | J1 ST-LINK header                                  |
| PA14 | SWCLK         |                 |           |                                                    |

On the schematic but not configured (module footprints not populated):

| Pin  | Net            | Would be      |
|------|----------------|---------------|
| PA0  | GSM_TX         | UART4_TX      |
| PA1  | GSM_RX         | UART4_RX      |
| PC3  | GSM_RST        | GPIO          |
| PA15 | LORA-433_NSS   | SPI3 NSS      |
| PC10 | LORA-433_SCLK  | SPI3_SCK      |
| PC11 | LORA-433_MISO  | SPI3_MISO     |
| PB5  | LORA-433_MOSI  | SPI3_MOSI     |
| PB4  | LORA-433_RST   | GPIO          |
| PB6  | LORA-433_DI0   | GPIO / EXTI   |
| PB3  | SWO            | trace         |
| PC14 | LSE_IN         | 32.768 kHz    |
| PC15 | LSE_OUT        |               |

All UARTs are 115200 8N1, no hardware flow control. The XBee and E22 must already be
configured for 115200 (same as the RF board); the firmware does not change module settings.

## Data flow

Each UART receives one byte per interrupt into a 1 KB ring (`gs_rx_t`). The main loop
assembles lines and handles one line per port per pass, round robin.

**Radio to PC.** A line is forwarded verbatim, CRLF terminated, only if it is

- a complete FC telemetry frame, checked with `tlm_parse` from rf_pcb-fw:

  ```
  $,TEAM_ID,TIME,PKT,ACC(x|y|z),GYR(x|y|z),MAG(x|y|z),EUL(r|p|y),ALT,PRES,TEMP,VEL(x|y|z),
  GNSS_TIME,LAT,LON,GNSS_ALT,SATS,GNSS_VEL(x|y),CURRENT,VOLT,BATT%,STATE,EFLAG,PROP_DEPLOY,RW_STATUS,#
  ```

- or an image line (`$IMGSTART`, `$IMG`, `$IMGEND`); the `*hh` checksum is left to the PC.

Anything else (partial LoRa frames after a lost packet, noise, echoes) is dropped. The LoRa
telemetry arrives as a 240-byte chunk, a 500 ms gap, then the rest; the assembler simply waits
for the `\n`. XBee carries telemetry at 5 Hz plus image lines; LoRa carries telemetry at 1 Hz.

**PC to radio (telecommands).** The two USB ports are listened to continuously. Each line
received on USB-A is transmitted on the XBee, each line on USB-B on the LoRa, verbatim plus
CRLF. rf_pcb-fw relays it unchanged to the flight computer, which parses it as a console
command (`BUZZ ON`, `STATE 2`, `CAPTURE`, `IMGACK,<id>,OK`, ... see `TELECOMMAND_HANDOFF.md`
in the FC repo). Because nothing downstream rewrites the line, the FC framing rules are
enforced here (`gs_tc_ok` in `gs_link.c`) and a line that fails is silently dropped:

- empty, or longer than 118 characters (rf_pcb-fw `TC_MAX` minus CRLF; FC limit is 127)
- any byte outside printable ASCII 0x20..0x7E (the FC treats 0x08/0x7F as backspace)
- first character `$` (the FC drops these; stops a looped-back telemetry line)
- `SIM...` flight-simulator lines (bench only; remove the check in `gs_tc_ok` for bench use)

Command words must be upper case; the firmware does not change case. The FC sends no reply
on the radio link: confirm a command through the `STATE`, `EFLAG` and packet fields of the
telemetry. Send one command per line and leave at least 20 ms between commands.

Routing is the four lines in the main loop of `Core/Src/main.c`; swap `usb_a` / `usb_b` there
to move a radio to the other USB port.

## Project layout

```
Core/Inc/gs_link.h, Core/Src/gs_link.c      byte ring, line assembler, line classifier (HAL-free)
Core/Inc/telemetry.h, Core/Src/telemetry.c  FC frame parser, copied verbatim from rf_pcb-fw
Core/Src/main.c                             clock, UART/GPIO init, bridge loop, UART callbacks
Core/Src/stm32f4xx_hal_msp.c                UART pin muxing and NVIC
Core/Src/stm32f4xx_it.c                     UART interrupt handlers
Tests/                                      host self-checks
gs_pcb_v1.ioc                               CubeMX configuration
```

## Build, flash, test

Open the folder in STM32CubeIDE (File > Open Projects from File System), build, and flash over
the J1 SWD header with an ST-LINK. Regenerating code from the `.ioc` is safe: application code
lives in `USER CODE` blocks and in `gs_link.c` / `telemetry.c`.

On the PC, open USB1 (XBee) and/or USB2 (LoRa) at 115200 8N1 with any serial terminal or the
ground station software. Each line is one telemetry frame or image line.

Host self-checks (not part of the firmware build, need any host `gcc`):

```
cd Tests
gcc -I../Core/Inc test_telemetry.c ../Core/Src/telemetry.c -o t && ./t
gcc -I../Core/Inc test_gs_link.c ../Core/Src/gs_link.c ../Core/Src/telemetry.c -o t && ./t
```
