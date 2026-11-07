# GD32F303 USB CDC / MSP

Board wiring from the JLC EDA project: PA11 = D−, PA12 = D+ through 22 Ω;
PA8 drives the 1.5 kΩ D+ pull-up. PA8 is held low for 30 ms at initialization,
then asserted by the GD32 USBD driver. This is the dedicated **USBD** peripheral
at IRQ `USBD_LP_CAN0_RX0`, not the GD32 USBFS OTG peripheral.

USB clock: 120 MHz PLL / 2.5 = 48 MHz. The board's 8 MHz crystal assumption must
still be checked on hardware. USB initialization fails closed at other CPU clocks.
The USB suspend path does not put the CPU into deep sleep or change its clocks.

The device exposes a single CDC ACM serial function with two interfaces:

| Endpoint | Purpose | Max packet | PMA offset |
| --- | --- | --- | --- |
| EP0 OUT / IN | Control | 64 | 0x40 / 0x80 |
| 0x82 IN | CDC notification | 8 | 0xc0 |
| 0x03 OUT | MSP / CLI receive | 64 | 0x100 |
| 0x81 IN | MSP / CLI transmit | 64 | 0x140 |

The 32-byte endpoint table starts at zero; all allocations fit in 512 bytes of
logical USB packet RAM. CPU-visible PMA addresses are spaced by the hardware.

The descriptor uses the local SDK example's VID:PID **28e9:018a**, CDC class
02/02/01 plus data interface 0a/00/00, product `Betaflight GD32F303`, and the
SDK-generated MCU serial number. Select the resulting serial port in Configurator;
automatic flight-controller classification has not been tested.

USB VCP and UART1 both default to MSP; UART3 remains ELRS/CRSF. The GD32 serial
configuration validator resets only the serial configuration group when loading
an old pre-VCP identifier layout, avoiding a missing USB MSP entry without
resetting the other parameter groups.

Receive uses one persistent 64-byte buffer: the OUT endpoint is rearmed only
after the application consumes it. USB NAK provides backpressure. Control
requests never masquerade as serial input. Transmit uses a 2048-byte queue and
a separate persistent 64-byte staging buffer; IN completion advances the queue,
including the terminating ZLP for full packets. Critical sections copy at most
64 bytes. Writes under backpressure have a 50 ms timeout, and never wait in an
interrupt or while interrupts are masked. If the host stops consuming data,
remaining bytes can be dropped after that timeout, matching the bounded-write
behavior expected of a VCP transport.

SET/GET_LINE_CODING and DTR/RTS callbacks are supported. SET_LINE_CODING is
restricted to the 7-byte structure. CDC class deconfiguration and USB bus reset
clear pending stream state. Active-link detection also checks recent SOFs.
This implements the Betaflight serial interface, so MSP v1/v2 and CLI are handled
by Betaflight's existing parsers; there is no second custom MSP parser here.

`lib/main/GD32F30x/USB/{device,usbd}` was copied from the workspace's
`0_GD32F303_MiniSystem/Libraries/GD32F30x_usbd_library` (V3.0.3). Vendor copyright
and license headers remain intact. Two unused-variable/parameter diagnostics
were fixed without changing the vendor transfer logic. `cdc_acm_core.[ch]` are
adapted from that SDK's CDC example, with descriptor strings, class lifecycle,
control-request handling and TX/RX callbacks changed for the serial transport.

Validation completed: firmware builds with `-Werror`; ELF checks confirm the
native USB IRQ, CDC descriptors, endpoint/interface topology, PMA layout and
VCP/UART identifiers, alongside the existing flight-path and BIN/HEX checks.
USB enumeration and real MSP transactions were subsequently verified on COM11:
the 18 read-only requests in `tools/gd32_usb_handshake.py` completed, including
the 155-byte BOXNAMES response. The application can display the setup page.
STATUS_EX still reported no detected IMU, one I2C error, and a zero PID cycle;
sensor detection and moving attitude are not yet validated. An earlier transient
write timeout has not been conclusively explained by the later successful run.

After flashing over SWD, close Configurator and run the read-only smoke probe:

```powershell
python tools/gd32_usb_msp_smoke.py COM5
```

Use the actual USB CDC COM port. The script reads API_VERSION, FC_VARIANT,
FC_VERSION, BOARD_INFO and STATUS, with a split request to check stream parsing.
It does not change settings or send ARM/motor commands. Also test reconnect,
configuration save/reboot, CLI dump and USB suspend/resume with props removed.
USB DFU/ROM bootloader entry is not implemented; firmware updates still use SWD.
