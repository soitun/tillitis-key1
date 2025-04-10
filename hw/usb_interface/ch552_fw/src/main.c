// SPDX-FileCopyrightText: 2017 WCH <wch-ic.com>
// SPDX-FileCopyrightText: 2022 Tillitis AB <tillitis.se>
// SPDX-License-Identifier: MIT

/********************************** (C) COPYRIGHT *******************************
 * File Name          : CDC.C
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2017/03/01
 * Description        : CH554 as CDC device to serial port, select serial port 1
 *******************************************************************************/
#include <stdint.h>
#include <string.h>

#include <ch554.h>
#include <ch554_usb.h>

#include "config.h"
#include "debug.h"
#include "flash.h"
#include "io.h"
#include "lib.h"
#include "mem.h"
#include "print.h"
#include "usb_strings.h"

XDATA AT0000 uint8_t Ep0Buffer[3*MAX_PACKET_SIZE] = { 0 }; // Endpoint 0, Default endpoint, OUT & IN buffer[64], must be an even address +
                                                           // Endpoint 4, DEBUG endpoint, buffer OUT[64]+IN[64], must be an even address
XDATA AT00C0 uint8_t Ep1Buffer[DEFAULT_EP1_SIZE]  = { 0 }; // Endpoint 1, CDC Ctrl endpoint, IN[8] buffer
XDATA AT00C8 uint8_t Ep2Buffer[2*MAX_PACKET_SIZE] = { 0 }; // Endpoint 2, CDC Data endpoint, buffer OUT[64]+IN[64], must be an even address
XDATA AT0148 uint8_t Ep3Buffer[2*MAX_PACKET_SIZE] = { 0 }; // Endpoint 3, FIDO endpoint, buffer OUT[64]+IN[64], must be an even address

IDATA uint16_t SetupLen = 0;
IDATA uint8_t SetupReq = 0;
IDATA uint8_t UsbConfig = 0;
const uint8_t *pDescr = NULL;         // USB configuration flag

#define UsbSetupBuf                   ((PUSB_SETUP_REQ)Ep0Buffer)

#define CDC_CTRL_EPOUT_ADDR            0x01              // CDC Ctrl Endpoint OUT Address
#define CDC_CTRL_EPOUT_SIZE            DEFAULT_EP1_SIZE  // CDC Ctrl Endpoint OUT Size

#define CDC_CTRL_EPIN_ADDR             0x81              // CDC Ctrl Endpoint IN Address
#define CDC_CTRL_EPIN_SIZE             DEFAULT_EP1_SIZE  // CDC Ctrl Endpoint IN Size

#define CDC_DATA_EPOUT_ADDR            0x02              // CDC Data Endpoint OUT Address
#define CDC_DATA_EPOUT_SIZE            MAX_PACKET_SIZE   // CDC Data Endpoint OUT Size

#define CDC_DATA_EPIN_ADDR             0x82              // CDC Data Endpoint IN Address
#define CDC_DATA_EPIN_SIZE             MAX_PACKET_SIZE   // CDC Data Endpoint IN Size

#define FIDO_EPOUT_ADDR                0x03              // FIDO Endpoint OUT Address
#define FIDO_EPOUT_SIZE                MAX_PACKET_SIZE   // FIDO Endpoint OUT Size

#define FIDO_EPIN_ADDR                 0x83              // FIDO Endpoint IN Address
#define FIDO_EPIN_SIZE                 MAX_PACKET_SIZE   // FIDO Endpoint IN Size

#define DEBUG_EPOUT_ADDR               0x04              // DEBUG Endpoint OUT Address
#define DEBUG_EPOUT_SIZE               MAX_PACKET_SIZE   // DEBUG Endpoint OUT Size

#define DEBUG_EPIN_ADDR                0x84              // DEBUG Endpoint IN Address
#define DEBUG_EPIN_SIZE                MAX_PACKET_SIZE   // DEBUG Endpoint IN Size

#define CDC_CTRL_FS_BINTERVAL          32                // Gives 32 ms polling interval at Full Speed for interrupt transfers
#define CDC_DATA_FS_BINTERVAL          0                 // bInterval is ignored for BULK transfers
#define FIDO_FS_BINTERVAL              2                 // Gives 2 ms polling interval at Full Speed for interrupt transfers
#define DEBUG_FS_BINTERVAL             2                 // Gives 2 ms polling interval at Full Speed for interrupt transfers

#define CFG_DESC_SIZE                  139U              // Size of Cfg_Desc
#define NUM_INTERFACES                 4                 // Number of interfaces

#define FIDO_REPORT_DESC_SIZE          47                // Size of FidoReportDesc
#define DEBUG_REPORT_DESC_SIZE         34                // Size of DebugReportDesc

#define LOBYTE(x)  ((uint8_t)( (x) & 0x00FFU))
#define HIBYTE(x)  ((uint8_t)(((x) & 0xFF00U) >> 8U))

IDATA uint8_t FidoInterfaceNum = 0;
IDATA uint8_t DebugInterfaceNum = 0;

XDATA uint8_t ActiveCfgDesc[CFG_DESC_SIZE];
XDATA uint8_t ActiveCfgDescSize = 0;

// Device Descriptor
FLASH uint8_t DevDesc[] = {
        0x12,                             /* bLength */
        USB_DESC_TYPE_DEVICE,             /* bDescriptorType: Device */
        0x00,                             /* bcdUSB (low byte), USB Specification Release Number in Binary-Coded Decimal (2.0 is 200h) */
        0x02,                             /* bcdUSB (high byte) USB Specification Release Number in Binary-Coded Decimal (2.0 is 200h) */
        USB_DEV_CLASS_MISCELLANEOUS,      /* bDeviceClass: Miscellaneous Device Class (Composite) */
        0x02,                             /* bDeviceSubClass: Common Class */
        0x01,                             /* bDeviceProtocol: IAD (Interface Association Descriptor) */
        DEFAULT_EP0_SIZE,                 /* bMaxPacketSize */
        0x07,                             /* idVendor */            // VID LOBYTE
        0x12,                             /* idVendor */            // VID HIBYTE
        0x87,                             /* idProduct */           // PID LOBYTE
        0x88,                             /* idProduct */           // PID HIBYTE
        0x00,                             /* bcdDevice (device release number in binary-coded decimal (BCD) format, low byte, i.e. YY) rel. XX.YY */
        0x01,                             /* bcdDevice (device release number in binary-coded decimal (BCD) format, high byte, i.e. XX) rel. XX.YY */
        USB_IDX_MFC_STR,                  /* Index of manufacturer string */
        USB_IDX_PRODUCT_STR,              /* Index of product string */
        USB_IDX_SERIAL_STR,               /* Index of serial number string */
        0x01,                             /* bNumConfigurations */
};

// Configuration Descriptor
FLASH uint8_t CfgDesc[] = {
        /******************** Configuration Descriptor ********************/
        0x09,                             /* bLength: Configuration Descriptor size */
        USB_DESC_TYPE_CONFIGURATION,      /* bDescriptorType: Configuration */
        CFG_DESC_SIZE,                    /* wTotalLength (low byte): Bytes returned */
        0x00,                             /* wTotalLength (high byte): Bytes returned */
        NUM_INTERFACES,                   /* bNumInterfaces: 4 interfaces (1 CDC Ctrl, 1 CDC Data, 1 FIDO, 1 DEBUG ) */
        0x01,                             /* bConfigurationValue: Configuration value */
        0x00,                             /* iConfiguration: Index of string descriptor describing the configuration */
        0xA0,                             /* bmAttributes: Bus powered and Support Remote Wake-up */
        0x32,                             /* MaxPower 100 mA: this current is used for detecting Vbus */
        /* 9 */
};

// CDC Descriptor
FLASH uint8_t CdcDesc[] = {
        /******************** IAD (Interface Association Descriptor), should be positioned just before the CDC interfaces ********************/
        /******************** This is to associate the two CDC interfaces with the CDC class ********************/
        0x08,                                /* bLength: IAD Descriptor size */
        USB_DESC_TYPE_INTERFACE_ASSOCIATION, /* bDescriptorType: Interface Association */
        0x00,                                /* bFirstInterface: 0 */
        0x02,                                /* bInterfaceCount: 2 */
        0x02,                                /* bFunctionClass: Communications & CDC Control */
        0x02,                                /* bFunctionSubClass: Abstract Control Model */
        0x01,                                /* bFunctionProtocol: Common AT commands */
        0x00,                                /* iFunction: Index of string descriptor */
        /******************** Interface 0, CDC Ctrl Descriptor (one endpoint) ********************/
        /* 8 */
        0x09,                             /* bLength: Interface Descriptor size */
        USB_DESC_TYPE_INTERFACE,          /* bDescriptorType: Interface */
        0x00,                             /* bInterfaceNumber: Number of Interface */
        0x00,                             /* bAlternateSetting: Alternate setting */
        0x01,                             /* bNumEndpoints */
        USB_DEV_CLASS_CDC_CONTROL,        /* bInterfaceClass: Communications and CDC Control */
        0x02,                             /* bInterfaceSubClass : Abstract Control Model */
        0x01,                             /* bInterfaceProtocol : AT Commands: V.250 etc */
        USB_IDX_INTERFACE_CDC_CTRL_STR,   /* iInterface: Index of string descriptor */
        /******************** Header Functional Descriptor ********************/
        /* 17 */
        0x05,                             /* bFunctionLength: Size of this descriptor in bytes */
        USB_DESC_TYPE_CS_INTERFACE,       /* bDescriptorType: CS_INTERFACE (24h) */
        0x00,                             /* bDescriptorSubtype: Header Functional Descriptor */
        0x10,                             /* bcdCDC (low byte): CDC version 1.10 */
        0x01,                             /* bcdCDC (high byte): CDC version 1.10 */
        /******************** Call Management Functional Descriptor (no data interface, bmCapabilities=03, bDataInterface=01) ********************/
        /* 22 */
        0x05,                             /* bFunctionLength: Size of this descriptor */
        USB_DESC_TYPE_CS_INTERFACE,       /* bDescriptorType: CS_INTERFACE (24h) */
        0x01,                             /* bDescriptorSubtype: Call Management Functional Descriptor */
        0x00,                             /* bmCapabilities:
                                             D7..2: 0x00 (RESERVED,
                                             D1   : 0x00 (0 - Device sends/receives call management information only over the Communications Class interface
                                                          1 - Device can send/receive call management information over a Data Class interface)
                                             D0   : 0x00 (0 - Device does not handle call management itself
                                                          1 - Device handles call management itself) */
        0x00,                             /* bDataInterface: Interface number of Data Class interface optionally used for call management */
        /******************** Abstract Control Management Functional Descriptor ********************/
        /* 27 */
        0x04,                             /* bLength */
        0x24,                             /* bDescriptorType: CS_INTERFACE (24h) */
        0x02,                             /* bDescriptorSubtype: Abstract Control Management Functional Descriptor */
        0x02,                             /* bmCapabilities:
                                             D7..4: 0x00 (RESERVED, Reset to zero)
                                             D3   : 0x00 (1 - Device supports the notification Network_Connection)
                                             D2   : 0x00 (1 - Device supports the request Send_Break)
                                             D1   : 0x01 (1 - Device supports the request combination of Set_Line_Coding, Set_Control_Line_State, Get_Line_Coding, and the notification Serial_State)
                                             D0   : 0x00 (1 - Device supports the request combination of Set_Comm_Feature, Clear_Comm_Feature, and Get_Comm_Feature) */
        /******************** Union Functional Descriptor. CDC Ctrl interface numbered 0; CDC Data interface numbered 1 ********************/
        /* 31 */
        0x05,                             /* bLength */
        0x24,                             /* bDescriptorType: CS_INTERFACE (24h) */
        0x06,                             /* bDescriptorSubtype: Union Functional Descriptor */
        0x00,                             /* bControlInterface: Interface number 0 (Control interface) */
        0x01,                             /* bSubordinateInterface0: Interface number 1 (Data interface) */
        /******************** CDC Ctrl Endpoint descriptor (IN) ********************/
        /* 36 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        CDC_CTRL_EPIN_ADDR,               /* bEndpointAddress: Endpoint Address (IN) */
        USB_EP_TYPE_INTERRUPT,            /* bmAttributes: Interrupt Endpoint */
        LOBYTE(CDC_CTRL_EPIN_SIZE),       /* wMaxPacketSize (low byte): 8 Byte max */
        HIBYTE(CDC_CTRL_EPIN_SIZE),       /* wMaxPacketSize (high byte): 8 Byte max */
        CDC_CTRL_FS_BINTERVAL,            /* bInterval: Polling Interval */
        /******************** Interface 1, CDC Data Descriptor (two endpoints) ********************/
        /* 43 */
        0x09,                             /* bLength: Interface Descriptor size */
        USB_DESC_TYPE_INTERFACE,          /* bDescriptorType: Interface */
        0x01,                             /* bInterfaceNumber: Number of Interface */
        0x00,                             /* bAlternateSetting: Alternate setting */
        0x02,                             /* bNumEndpoints */
        USB_DEV_CLASS_CDC_DATA,           /* bInterfaceClass: CDC Data */
        0x00,                             /* bInterfaceSubClass : 1=BOOT, 0=no boot */
        0x00,                             /* bInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
        USB_IDX_INTERFACE_CDC_DATA_STR,   /* iInterface: Index of string descriptor */
        /******************** CDC Data Endpoint descriptor (OUT) ********************/
        /* 52 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        CDC_DATA_EPOUT_ADDR,              /* bEndpointAddress: Endpoint Address (OUT) */
        USB_EP_TYPE_BULK,                 /* bmAttributes: Bulk Endpoint */
        LOBYTE(CDC_DATA_EPOUT_SIZE),      /* wMaxPacketSize (low byte): 64 Byte max */
        HIBYTE(CDC_DATA_EPOUT_SIZE),      /* wMaxPacketSize (high byte): 64 Byte max */
        CDC_DATA_FS_BINTERVAL,            /* bInterval: Polling Interval */
        /******************** CDC Data Endpoint descriptor (IN) ********************/
        /* 59 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        CDC_DATA_EPIN_ADDR,               /* bEndpointAddress: Endpoint Address (IN) */
        USB_EP_TYPE_BULK,                 /* bmAttributes: Bulk Endpoint */
        LOBYTE(CDC_DATA_EPIN_SIZE),       /* wMaxPacketSize (low byte): 64 Byte max */
        HIBYTE(CDC_DATA_EPIN_SIZE),       /* wMaxPacketSize (high byte): 64 Byte max */
        CDC_DATA_FS_BINTERVAL,            /* bInterval: Polling Interval */
        /* 66 */
};

// FIDO Descriptor
FLASH uint8_t FidoDesc[] = {
        /******************** Interface 2, FIDO Descriptor (two endpoints) ********************/
        0x09,                             /* bLength: Interface Descriptor size */
        USB_DESC_TYPE_INTERFACE,          /* bDescriptorType: Interface */
        0x02,                             /* bInterfaceNumber: Number of Interface */
        0x00,                             /* bAlternateSetting: Alternate setting */
        0x02,                             /* bNumEndpoints: 2 */
        USB_DEV_CLASS_HID,                /* bInterfaceClass: Human Interface Device */
        0x00,                             /* bInterfaceSubClass : 1=BOOT, 0=no boot */
        0x00,                             /* bInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
        USB_IDX_INTERFACE_FIDO_STR,       /* iInterface: Index of string descriptor */
        /******************** FIDO Device Descriptor ********************/
        /* 9 */
        0x09,                             /* bLength: HID Descriptor size */
        USB_DESC_TYPE_HID,                /* bDescriptorType: HID */
        0x11,                             /* bcdHID (low byte): HID Class Spec release number */
        0x01,                             /* bcdHID (high byte): HID Class Spec release number */
        0x00,                             /* bCountryCode: Hardware target country */
        0x01,                             /* bNumDescriptors: Number of HID class descriptors to follow */
        USB_DESC_TYPE_REPORT,             /* bDescriptorType: Report */
        LOBYTE(FIDO_REPORT_DESC_SIZE),    /* wDescriptorLength (low byte): Total length of Report descriptor */
        HIBYTE(FIDO_REPORT_DESC_SIZE),    /* wDescriptorLength (high byte): Total length of Report descriptor */
        /******************** FIDO Endpoint Descriptor (OUT) ********************/
        /* 18 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        FIDO_EPOUT_ADDR,                  /* bEndpointAddress: Endpoint Address (OUT) */
        USB_EP_TYPE_INTERRUPT,            /* bmAttributes: Interrupt endpoint */
        LOBYTE(FIDO_EPOUT_SIZE),          /* wMaxPacketSize (low byte): 64 Byte max */
        HIBYTE(FIDO_EPOUT_SIZE),          /* wMaxPacketSize (high byte): 64 Byte max */
        FIDO_FS_BINTERVAL,                /* bInterval: Polling Interval */
        /******************** FIDO Endpoint Descriptor (IN) ********************/
        /* 25 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        FIDO_EPIN_ADDR,                   /* bEndpointAddress: Endpoint Address (IN) */
        USB_EP_TYPE_INTERRUPT,            /* bmAttributes: Interrupt endpoint */
        LOBYTE(FIDO_EPIN_SIZE),           /* wMaxPacketSize (low byte): 64 Byte max */
        HIBYTE(FIDO_EPIN_SIZE),           /* wMaxPacketSize (high byte): 64 Byte max */
        FIDO_FS_BINTERVAL,                /* bInterval: Polling Interval */
        /* 32 */
};

// DEBUG Descriptor
FLASH uint8_t DebugDesc[] = {
        /******************** Interface 3, DEBUG Descriptor (two endpoints) ********************/
        0x09,                             /* bLength: Interface Descriptor size */
        USB_DESC_TYPE_INTERFACE,          /* bDescriptorType: Interface */
        0x03,                             /* bInterfaceNumber: Number of Interface */
        0x00,                             /* bAlternateSetting: Alternate setting */
        0x02,                             /* bNumEndpoints: 2 */
        USB_DEV_CLASS_HID,                /* bInterfaceClass: Human Interface Device */
        0x00,                             /* bInterfaceSubClass : 1=BOOT, 0=no boot */
        0x00,                             /* bInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
        USB_IDX_INTERFACE_DEBUG_STR,      /* iInterface: Index of string descriptor */
        /******************** DEBUG Device Descriptor ********************/
        /* 9 */
        0x09,                             /* bLength: HID Descriptor size */
        USB_DESC_TYPE_HID,                /* bDescriptorType: HID */
        0x11,                             /* bcdHID (low byte): HID Class Spec release number */
        0x01,                             /* bcdHID (high byte): HID Class Spec release number */
        0x00,                             /* bCountryCode: Hardware target country */
        0x01,                             /* bNumDescriptors: Number of HID class descriptors to follow */
        USB_DESC_TYPE_REPORT,             /* bDescriptorType: Report */
        LOBYTE(DEBUG_REPORT_DESC_SIZE),   /* wDescriptorLength (low byte): Total length of Report descriptor */
        HIBYTE(DEBUG_REPORT_DESC_SIZE),   /* wDescriptorLength (high byte): Total length of Report descriptor */
        /******************** DEBUG Endpoint Descriptor (OUT) ********************/
        /* 18 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        DEBUG_EPOUT_ADDR,                 /* bEndpointAddress: Endpoint Address (OUT) */
        USB_EP_TYPE_INTERRUPT,            /* bmAttributes: Interrupt endpoint */
        LOBYTE(DEBUG_EPOUT_SIZE),         /* wMaxPacketSize (low byte): 64 Byte max */
        HIBYTE(DEBUG_EPOUT_SIZE),         /* wMaxPacketSize (high byte): 64 Byte max */
        DEBUG_FS_BINTERVAL,               /* bInterval: Polling Interval */
        /******************** DEBUG Endpoint Descriptor (IN) ********************/
        /* 25 */
        0x07,                             /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT,           /* bDescriptorType: Endpoint */
        DEBUG_EPIN_ADDR,                  /* bEndpointAddress: Endpoint Address (IN) */
        USB_EP_TYPE_INTERRUPT,            /* bmAttributes: Interrupt endpoint */
        LOBYTE(DEBUG_EPIN_SIZE),          /* wMaxPacketSize (low byte): 64 Byte max */
        HIBYTE(DEBUG_EPIN_SIZE),          /* wMaxPacketSize (high byte): 64 Byte max */
        DEBUG_FS_BINTERVAL,               /* bInterval: Polling Interval */
        /* 32 */
};

// FIDO Device Descriptor (copy from FidoDesc)
FLASH uint8_t FidoCfgDesc[] = {
        0x09,                             /* bLength: HID Descriptor size */
        USB_DESC_TYPE_HID,                /* bDescriptorType: HID */
        0x11,                             /* bcdHID (low byte): HID Class Spec release number */
        0x01,                             /* bcdHID (high byte): HID Class Spec release number */
        0x00,                             /* bCountryCode: Hardware target country */
        0x01,                             /* bNumDescriptors: Number of HID class descriptors to follow */
        USB_DESC_TYPE_REPORT,             /* bDescriptorType: Report */
        LOBYTE(FIDO_REPORT_DESC_SIZE),    /* wDescriptorLength (low byte): Total length of Report descriptor */
        HIBYTE(FIDO_REPORT_DESC_SIZE),    /* wDescriptorLength (high byte): Total length of Report descriptor */
};

FLASH uint8_t FidoReportDesc[] ={
        0x06, 0xD0, 0xF1,                 /* Usage Page (FIDO Alliance Page) */
        0x09, 0x01,                       /* Usage (U2F Authenticator Device) */
        0xA1, 0x01,                       /*   Collection (Application) */
        /* 7 */
        0x09, 0x20,                       /*     Usage (Input Report Data) */
        0x15, 0x00,                       /*     Logical Minimum (0) */
        0x26, 0xFF, 0x00,                 /*     Logical Maximum (255) */
        0x75, 0x08,                       /*     Report Size (8) */
        0x95, MAX_PACKET_SIZE,            /*     Report Count (64) */
        0x81, 0x02,                       /*     Input (Data, Variable, Absolute); */
        /* 20 */
        0x09, 0x21,                       /*     Usage (Output Report Data) */
        0x15, 0x00,                       /*     Logical Minimum (0) */
        0x26, 0xFF, 0x00,                 /*     Logical Maximum (255) */
        0x75, 0x08,                       /*     Report Size (8) */
        0x95, MAX_PACKET_SIZE,            /*     Report Count (64) */
        0x91, 0x02,                       /*     Output (Data, Variable, Absolute) */
        /* 33 */
        0x09, 0x07,                       /*     Usage (7, Reserved) */
        0x15, 0x00,                       /*     Logical Minimum (0) */
        0x26, 0xFF, 0x00,                 /*     Logical Maximum (255) */
        0x75, 0x08,                       /*     Report Size (8) */
        0x95, 0x08,                       /*     Report Count (8) */
        0xB1, 0x02,                       /*     Feature (2) (???) */
        /* 46 */
        0xC0                              /*   End Collection */
        /* 47 */
};

// DEBUG Device Descriptor (copy from DebugDesc)
FLASH uint8_t DebugCfgDesc[] = {
        0x09,                             /* bLength: HID Descriptor size */
        USB_DESC_TYPE_HID,                /* bDescriptorType: HID */
        0x11,                             /* bcdHID (low byte): HID Class Spec release number */
        0x01,                             /* bcdHID (high byte): HID Class Spec release number */
        0x00,                             /* bCountryCode: Hardware target country */
        0x01,                             /* bNumDescriptors: Number of HID class descriptors to follow */
        USB_DESC_TYPE_REPORT,             /* bDescriptorType: Report */
        LOBYTE(DEBUG_REPORT_DESC_SIZE),   /* wDescriptorLength (low byte): Total length of Report descriptor */
        HIBYTE(DEBUG_REPORT_DESC_SIZE),   /* wDescriptorLength (high byte): Total length of Report descriptor */
};

// DEBUG Report Descriptor
FLASH uint8_t DebugReportDesc[] ={
        0x06, 0x00, 0xFF,                 /* Usage Page (Vendor Defined 0xFF00) */
        0x09, 0x01,                       /* Usage (Vendor Usage 1) */
        0xA1, 0x01,                       /*   Collection (Application) */
        /* 7 */
        0x09, 0x02,                       /*     Usage (Output Report Data), 0x02 defines that the output report carries raw data from the host to the device. */
        0x15, 0x00,                       /*     Logical Minimum (0) */
        0x26, 0xFF, 0x00,                 /*     Logical Maximum (255) */
        0x75, 0x08,                       /*     Report Size (8 bits) */
        0x95, MAX_PACKET_SIZE,            /*     Report Count (64 bytes) */
        0x91, 0x02,                       /*     Output (Data, Variable, Absolute) */
        /* 20 */
        0x09, 0x03,                       /*     Usage (Input Report), 0x03 defines that the input report carries raw data for the host. */
        0x15, 0x00,                       /*     Logical Minimum (0) */
        0x26, 0xFF, 0x00,                 /*     Logical Maximum (255) */
        0x75, 0x08,                       /*     Report Size (8 bits) */
        0x95, MAX_PACKET_SIZE,            /*     Report Count (64 bytes) */
        0x81, 0x02,                       /*     Input (Data, Variable, Absolute) */
        /* 33 */
        0xC0                              /*   End Collection */
        /* 34 */
};

// String Descriptor (Language descriptor )
FLASH uint8_t LangDesc[] = {
        4,           // Length of this descriptor (in bytes)
        0x03,        // Descriptor type (String)
        0x09, 0x04,  // Language ID (English - US)
};

// CDC Parameters: The initial baud rate is 500000, 1 stop bit, no parity, 8 data bits.
FLASH uint8_t LineCoding[7] = { 0x20, 0xA1, 0x07, 0x00, /* Data terminal rate, in bits per second: 500000 */
                                                  0x00, /* Stop bits: 0 - 1 Stop bit, 1 - 1.5 Stop bits, 2 - 2 Stop bits */
                                                  0x00, /* Parity: 0 - None, 1 - Odd, 2 - Even, 3 - Mark, 4 - Space */
                                                  0x08, /* Data bits (5, 6, 7, 8 or 16) */
                                };

#define UART_TX_BUF_SIZE     64   // Serial transmit buffer
#define UART_RX_BUF_SIZE     140  // Serial receive buffer

/** Communication UART */
XDATA uint8_t UartTxBuf[UART_TX_BUF_SIZE] = { 0 };  // Serial transmit buffer
volatile IDATA uint8_t Ep2ByteLen;
volatile IDATA uint8_t Ep3ByteLen;
volatile IDATA uint8_t Ep4ByteLen;

XDATA uint8_t UartRxBuf[UART_RX_BUF_SIZE] = { 0 };  // Serial receive buffer
volatile IDATA uint8_t UartRxBufInputPointer = 0;   // Circular buffer write pointer, bus reset needs to be initialized to 0
volatile IDATA uint8_t UartRxBufOutputPointer = 0;  // Take pointer out of circular buffer, bus reset needs to be initialized to 0
volatile IDATA uint8_t UartRxBufByteCount = 0;      // Number of unprocessed bytes remaining in the buffer

/** Debug UART */
#ifdef DEBUG_PRINT_HW
#define DEBUG_UART_RX_BUF_SIZE        8
XDATA uint8_t DebugUartRxBuf[DEBUG_UART_RX_BUF_SIZE] = { 0 };
volatile IDATA uint8_t DebugUartRxBufInputPointer = 0;
volatile IDATA uint8_t DebugUartRxBufOutputPointer = 0;
volatile IDATA uint8_t DebugUartRxBufByteCount = 0;
#endif

/** Endpoint handling */
volatile IDATA uint8_t UsbEp2ByteCount = 0;     // Represents the data received by USB endpoint 2 (CDC)
volatile IDATA uint8_t UsbEp3ByteCount = 0;     // Represents the data received by USB endpoint 3 (FIDO)
volatile IDATA uint8_t UsbEp4ByteCount = 0;     // Represents the data received by USB endpoint 4 (DEBUG)

volatile IDATA uint8_t Endpoint2UploadBusy = 0; // Whether the upload endpoint 2 (CDC) is busy
volatile IDATA uint8_t Endpoint3UploadBusy = 0; // Whether the upload endpoint 3 (FIDO) is busy
volatile IDATA uint8_t Endpoint4UploadBusy = 0; // Whether the upload endpoint 4 (DEBUG) is busy

/** CH552 variables */
IDATA uint8_t CH552DataAvailable = 0;

/** DEBUG variables */
IDATA uint8_t DebugDataAvailable = 0;

/** CDC variables */
IDATA uint8_t CdcDataAvailable = 0;

/** FIDO variables */
IDATA uint8_t FidoDataAvailable = 0;

/** Frame data */
#define MAX_FRAME_SIZE    64
XDATA uint8_t FrameBuf[MAX_FRAME_SIZE] = { 0 };
IDATA uint8_t FrameBufLength = 0;

IDATA uint8_t FrameMode   = 0;
IDATA uint8_t FrameLength = 0;
IDATA uint8_t FrameRemainingBytes = 0;
IDATA uint8_t FrameStarted = 0;
IDATA uint8_t FrameDiscard = 0;
IDATA uint8_t DiscardDataAvailable = 0;

uint32_t increment_pointer(uint32_t pointer, uint32_t increment, uint32_t buffer_size);
uint32_t decrement_pointer(uint32_t pointer, uint32_t decrement, uint32_t buffer_size);
void cts_start(void);
void cts_stop(void);
void check_cts_stop(void);

/*******************************************************************************
 * Function Name  : USBDeviceCfg()
 * Description    : USB device mode configuration
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
void USBDeviceCfg()
{
    USB_CTRL = 0x00;                                       // Clear USB control register
    USB_CTRL &= ~bUC_HOST_MODE;                            // This bit selects the device mode
    USB_CTRL |= bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN; // USB device and internal pull-up enable, automatically return to NAK before interrupt flag is cleared
    USB_DEV_AD = 0x00;                                     // Device address initialization
#if 0
    // USB_CTRL  |= bUC_LOW_SPEED;
    // UDEV_CTRL |= bUD_LOW_SPEED;                         // Select low speed 1.5M mode
#else
    USB_CTRL  &= ~bUC_LOW_SPEED;
    UDEV_CTRL &= ~bUD_LOW_SPEED;                           // Select full speed 12M mode, the default mode
#endif
    UDEV_CTRL |= bUD_PD_DIS;                               // Disable DP / DM pull-down resistor
    UDEV_CTRL |= bUD_PORT_EN;                              // Enable physical port
}

/*******************************************************************************
 * Function Name  : USBDeviceIntCfg()
 * Description    : USB device mode interrupt initialization
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
void USBDeviceIntCfg()
{
    USB_INT_EN |= bUIE_SUSPEND;  // Enable device suspend interrupt
    USB_INT_EN |= bUIE_TRANSFER; // Enable USB transfer completion interrupt
    USB_INT_EN |= bUIE_BUS_RST;  // Enable device mode USB bus reset interrupt
    USB_INT_FG |= (UIF_FIFO_OV | UIF_HST_SOF | UIF_SUSPEND | UIF_TRANSFER | UIF_BUS_RST ); // Clear interrupt flag
    IE_USB = 1;                  // Enable USB interrupt
    EA = 1;                      // Allow microcontroller interrupt
}

/*******************************************************************************
 * Function Name  : USBDeviceEndPointCfg()
 * Description    : USB device mode endpoint configuration, simulation compatible
 *                  HID device, in addition to endpoint 0 control transmission,
 *                  also includes endpoint 2 batch upload
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
void USBDeviceEndPointCfg()
{
    // TODO: Is casting the right thing here? What about endianness?
    UEP0_DMA = (uint16_t) Ep0Buffer; // Endpoint 0 data transfer address, Endpoint 4
    UEP1_DMA = (uint16_t) Ep1Buffer; // Endpoint 1 sends data transfer address
    UEP2_DMA = (uint16_t) Ep2Buffer; // Endpoint 2 IN data transfer address
    UEP3_DMA = (uint16_t) Ep3Buffer; // Endpoint 3 IN data transfer address

    UEP4_1_MOD = bUEP4_TX_EN | bUEP4_RX_EN | bUEP1_TX_EN;               // Endpoint 0 single 8-byte send and receive buffer
                                                                        // Endpoint 1, transmit enable
                                                                        // Endpoint 4, single buffer, transmit+receive enable
    UEP2_3_MOD = bUEP2_TX_EN | bUEP2_RX_EN | bUEP3_TX_EN | bUEP3_RX_EN; // Endpoint 2, single buffer, transmit+receive enable
                                                                        // Endpoint 3, single buffer, transmit+receive enable

    UEP0_CTRL =                 UEP_T_RES_NAK | UEP_R_RES_ACK; // Endpoint 0, manual flip, OUT transaction returns ACK, IN transaction returns NAK
    UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;                 // Endpoint 1, automatically flips the synchronization flag, IN transaction returns NAK
    UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK; // Endpoint 2, automatically flips the synchronization flag, IN transaction returns NAK, OUT returns ACK
    UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK; // Endpoint 3, automatically flips the synchronization flag, IN transaction returns NAK, OUT returns ACK
    UEP4_CTRL =                 UEP_T_RES_NAK | UEP_R_RES_ACK; // Endpoint 4, manual flip, OUT transaction returns ACK, IN transaction returns NAK
}

void CreateCfgDescriptor(uint8_t ep_config)
{
    uint8_t num_iface = 0;   // Interface number

    FidoInterfaceNum  = 0xFF; // Set as invalid until we have parsed each interface
    DebugInterfaceNum = 0xFF; // Set as invalid until we have parsed each interface

    memset(ActiveCfgDesc, 0, CFG_DESC_SIZE); // Clean the descriptor

    uint8_t cfg_desc_size = sizeof(CfgDesc);
    memcpy(ActiveCfgDesc, CfgDesc, cfg_desc_size);
    ActiveCfgDescSize += cfg_desc_size;

    if (ep_config & IO_CDC) {
        uint8_t cdc_desc_size = sizeof(CdcDesc);
        memcpy(ActiveCfgDesc + ActiveCfgDescSize, CdcDesc, cdc_desc_size);
        num_iface += 2;
        ActiveCfgDescSize += cdc_desc_size;
    }

    if (ep_config & IO_FIDO) {
        uint8_t fido_desc_size = sizeof(FidoDesc);
        memcpy(ActiveCfgDesc + ActiveCfgDescSize, FidoDesc, fido_desc_size);
        ActiveCfgDesc[ActiveCfgDescSize + 2] = num_iface;
        FidoInterfaceNum = num_iface;
        num_iface++;
        ActiveCfgDescSize += fido_desc_size;
    }

    if (ep_config & IO_DEBUG) {
        uint8_t debug_desc_size = sizeof(DebugDesc);
        memcpy(ActiveCfgDesc + ActiveCfgDescSize, DebugDesc, debug_desc_size);
        ActiveCfgDesc[ActiveCfgDescSize + 2] = num_iface;
        DebugInterfaceNum = num_iface;
        num_iface++;
        ActiveCfgDescSize += debug_desc_size;
    }

    ActiveCfgDesc[2] = ActiveCfgDescSize;
    ActiveCfgDesc[4] = num_iface;
}

/*******************************************************************************
 * Function Name  : Config_Uart1(uint8_t *cfg_uart)
 * Description    : Configure serial port 1 parameters
 * Input          : Serial port configuration parameters Four bit baud rate, stop bit, parity, data bit
 * Output         : None
 * Return         : None
 *******************************************************************************/
void Config_Uart1(uint8_t *cfg_uart)
{
    uint32_t uart1_baud = 0;
    *((uint8_t*) &uart1_baud) = cfg_uart[0];
    *((uint8_t*) &uart1_baud + 1) = cfg_uart[1];
    *((uint8_t*) &uart1_baud + 2) = cfg_uart[2];
    *((uint8_t*) &uart1_baud + 3) = cfg_uart[3];
    SBAUD1 = 256 - FREQ_SYS / 16 / uart1_baud;   // SBAUD1 = 256 - Fsys / 16 / baud rate
    IE_UART1 = 1; // Enable UART1 interrupt
}

void usb_irq_setup_handler(void)
{
    uint16_t len = USB_RX_LEN;
    printStrSetup("Setup ");

    if (len == (sizeof(USB_SETUP_REQ))) {
        SetupLen = ((uint16_t) UsbSetupBuf->wLengthH << 8) | (UsbSetupBuf->wLengthL);
        len = 0; // Defaults to success and uploading 0 length
        SetupReq = UsbSetupBuf->bRequest;

        // Class-Specific Requests, i.e. HID request, CDC request etc.
        if ((UsbSetupBuf->bmRequestType & USB_REQ_TYPE_MASK) != USB_REQ_TYPE_STANDARD) {
            printStrSetup("Class-Specific ");
            printStrSetup("SetupReq=");
            printStrSetup("0x");
            printNumU8HexSetup(SetupReq);
            printStrSetup(" ");
            switch(SetupReq) {
            case USB_HID_REQ_TYPE_GET_REPORT:
                printStrSetup("HID Get Report\n");
                break;
            case USB_HID_REQ_TYPE_GET_IDLE:
                printStrSetup("HID Get Idle\n");
                break;
            case USB_HID_REQ_TYPE_GET_PROTOCOL:
                printStrSetup("HID Get Protocol\n");
                break;
            case USB_HID_REQ_TYPE_SET_REPORT:
                printStrSetup("HID Set Report\n");
                break;
            case USB_HID_REQ_TYPE_SET_IDLE:
                printStrSetup("HID Set Idle\n");
                break;
            case USB_HID_REQ_TYPE_SET_PROTOCOL:
                printStrSetup("HID Set Protocol\n");
                break;
            case USB_CDC_REQ_TYPE_SET_LINE_CODING:
                printStrSetup("CDC Set Line Coding\n");
                break;
            case USB_CDC_REQ_TYPE_GET_LINE_CODING:
                printStrSetup("CDC Get Line Coding\n");
                pDescr = LineCoding;
                len = sizeof(LineCoding);
                len = SetupLen >= DEFAULT_EP0_SIZE ? DEFAULT_EP0_SIZE : SetupLen; // The length of this transmission
                memcpy(Ep0Buffer, pDescr, len);
                SetupLen -= len;
                pDescr += len;
                break;
            case USB_CDC_REQ_TYPE_SET_CONTROL_LINE_STATE: // Generates RS-232/V.24 style control signals
                printStrSetup("CDC Set Control Line State\n");
                break;
            default:
                len = 0xFF; // Command not supported
                printStrSetup("Unsupported\n");
                break;
            }
        } // END Non-standard request

        else { // Standard Requests
            // Request code
            switch (SetupReq) {
            case USB_GET_DESCRIPTOR: {
                switch (UsbSetupBuf->wValueH) {
                case USB_DESC_TYPE_DEVICE: // Device descriptor
                    pDescr = DevDesc; // Send the device descriptor to the buffer to be sent
                    len = sizeof(DevDesc);
                    printStrSetup("DevDesc\n");
                    break;
                case USB_DESC_TYPE_CONFIGURATION: // Configuration descriptor
                    pDescr = ActiveCfgDesc; // Send the configuration descriptor to the buffer to be sent
                    len = sizeof(ActiveCfgDesc);
                    printStrSetup("CfgDesc\n");
                    break;
                case USB_DESC_TYPE_STRING: // String descriptors
                    if (UsbSetupBuf->wValueL == USB_IDX_LANGID_STR) {
                        pDescr = LangDesc;
                        len = sizeof(LangDesc);
                        printStrSetup("LangDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_MFC_STR) {
                        pDescr = ManufDesc;
                        len = sizeof(ManufDesc);
                        printStrSetup("ManufDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_PRODUCT_STR) {
                        pDescr = ProdDesc;
                        len = sizeof(ProdDesc);
                        printStrSetup("ProdDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_SERIAL_STR) {
                        pDescr = SerialDesc;
                        len = sizeof(SerialDesc);
                        printStrSetup("SerialDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_INTERFACE_CDC_CTRL_STR) {
                        pDescr = CdcCtrlInterfaceDesc;
                        len = sizeof(CdcCtrlInterfaceDesc);
                        printStrSetup("CdcCtrlInterfaceDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_INTERFACE_CDC_DATA_STR) {
                        pDescr = CdcDataInterfaceDesc;
                        len = sizeof(CdcDataInterfaceDesc);
                        printStrSetup("CdcDataInterfaceDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_INTERFACE_FIDO_STR) {
                        pDescr = FidoInterfaceDesc;
                        len = sizeof(FidoInterfaceDesc);
                        printStrSetup("FidoHidInterfaceDesc\n");
                    } else if (UsbSetupBuf->wValueL == USB_IDX_INTERFACE_DEBUG_STR) {
                        pDescr = DebugInterfaceDesc;
                        len = sizeof(DebugInterfaceDesc);
                        printStrSetup("DebugInterfaceDesc\n");
                    } else {
                        printStrSetup("Error: USB_DESC_TYPE_STRING\n");
                    }
                    break;
                case USB_DESC_TYPE_HID:
                    if (UsbSetupBuf->wIndexL == FidoInterfaceNum) { // Interface number for FIDO
                        pDescr = FidoCfgDesc;
                        len = sizeof(FidoCfgDesc);
                        printStrSetup("FidoCfgDesc\n");
                    } else if (UsbSetupBuf->wIndexL == DebugInterfaceNum) { // Interface number for DEBUG
                        pDescr = DebugCfgDesc;
                        len = sizeof(DebugCfgDesc);
                        printStrSetup("DebugCfgDesc\n");
                    }
                    break;
                case USB_DESC_TYPE_REPORT:
                    if (UsbSetupBuf->wIndexL == FidoInterfaceNum) { // Interface number for FIDO
                        pDescr = FidoReportDesc;
                        len = sizeof(FidoReportDesc);
                        printStrSetup("FidoReportDesc\n");
                    } else if (UsbSetupBuf->wIndexL == DebugInterfaceNum) { // Interface number for DEBUG
                        pDescr = DebugReportDesc;
                        len = sizeof(DebugReportDesc);
                        printStrSetup("DebugReportDesc\n");
                    }
                    break;
                default:
                    len = 0xFF; // Unsupported command or error
                    printStrSetup("Unsupported\n");
                    break;
                }

                if (SetupLen > len) {
                    SetupLen = len; // Limit total length
                }

                len = SetupLen >= DEFAULT_EP0_SIZE ? DEFAULT_EP0_SIZE : SetupLen; // This transmission length
                memcpy(Ep0Buffer, pDescr, len); // Copy upload data
                SetupLen -= len;
                pDescr += len;
            }
            break;

            case USB_SET_ADDRESS:
                SetupLen = UsbSetupBuf->wValueL; // Temporary storage of USB device address
                printStrSetup("SetAddress\n");
                break;

            case USB_GET_CONFIGURATION:
                Ep0Buffer[0] = UsbConfig;
                if (SetupLen >= 1) {
                    len = 1;
                }
                printStrSetup("GetConfig\n");
                break;

            case USB_SET_CONFIGURATION:
                UsbConfig = UsbSetupBuf->wValueL;
                printStrSetup("SetConfig\n");
                break;

            case USB_GET_INTERFACE:
                printStrSetup("GetInterface\n");
                break;

            case USB_CLEAR_FEATURE: // Clear Feature
                printStrSetup("ClearFeature\n");
                if ((UsbSetupBuf->bmRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {  // Remove device
                    if ((((uint16_t) UsbSetupBuf->wValueH << 8) | UsbSetupBuf->wValueL) == 0x01) {
                        if (CfgDesc[7] & 0x20) {
                            // Wake
                        } else {
                            len = 0xFF; // Operation failed
                            printStrSetup("Unsupported\n");
                        }
                    } else {
                        len = 0xFF; // Operation failed
                        printStrSetup("Unsupported\n");
                    }
                } else if ((UsbSetupBuf->bmRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) { // Endpoint
                    switch (UsbSetupBuf->wIndexL) {
                    case 0x84:
                        UEP4_CTRL = (UEP4_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK; // Set endpoint 4 IN (TX) NAK
                        break;
                    case 0x04:
                        UEP4_CTRL = (UEP4_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK; // Set endpoint 4 OUT (RX) ACK
                        break;
                    case 0x83:
                        UEP3_CTRL = (UEP3_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK; // Set endpoint 3 IN (TX) NAK
                        break;
                    case 0x03:
                        UEP3_CTRL = (UEP3_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK; // Set endpoint 3 OUT (RX) ACK
                        break;
                    case 0x82:
                        UEP2_CTRL = (UEP2_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK; // Set endpoint 2 IN (TX) NACK
                        break;
                    case 0x02:
                        UEP2_CTRL = (UEP2_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK; // Set endpoint 2 OUT (RX) ACK
                        break;
                    case 0x81:
                        UEP1_CTRL = (UEP1_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK; // Set endpoint 1 IN (TX) NACK
                        break;
                    case 0x01:
                        UEP1_CTRL = (UEP1_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK; // Set endpoint 1 OUT (RX) ACK
                        break;
                    default:
                        len = 0xFF;  // Unsupported endpoint
                        printStrSetup("Unsupported\n");
                        break;
                    }
                } else {
                    len = 0xFF; // It's not that the endpoint doesn't support it
                    printStrSetup("Unsupported\n");
                }
                break;

            case USB_SET_FEATURE: // Set Feature
                printStrSetup("SetFeature\n");
                if (( UsbSetupBuf->bmRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) { // Set up the device
                    if ((((uint16_t) UsbSetupBuf->wValueH << 8) | UsbSetupBuf->wValueL) == 0x01) {
                        if (CfgDesc[7] & 0x20) {
                            printStrSetup("Suspend\n");
                            while (XBUS_AUX & bUART0_TX) {
                                ; // Wait for sending to complete
                            }
                            SAFE_MOD = 0x55;
                            SAFE_MOD = 0xAA;
                            WAKE_CTRL = bWAK_BY_USB | bWAK_RXD0_LO | bWAK_RXD1_LO; // USB or RXD0/1 can be woken up when there is a signal
                            PCON |= PD; // Sleep
                            SAFE_MOD = 0x55;
                            SAFE_MOD = 0xAA;
                            WAKE_CTRL = 0x00;
                        } else {
                            len = 0xFF; // Operation failed
                        }
                    } else {
                        len = 0xFF; // Operation failed
                    }
                } else if (( UsbSetupBuf->bmRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) { // Set endpoint
                    if ((((uint16_t) UsbSetupBuf->wValueH << 8) | UsbSetupBuf->wValueL) == 0x00) {
                        switch (((uint16_t) UsbSetupBuf->wIndexH << 8) | UsbSetupBuf->wIndexL) {
                        case 0x84:
                            UEP4_CTRL = (UEP4_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL; // Set endpoint 4 IN (TX) Stall (error)
                            break;
                        case 0x04:
                            UEP4_CTRL = (UEP4_CTRL & ~bUEP_R_TOG) | UEP_R_RES_STALL; // Set endpoint 4 OUT (RX) Stall (error)
                            break;
                        case 0x83:
                            UEP3_CTRL = (UEP3_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL; // Set endpoint 3 IN (TX) Stall (error)
                            break;
                        case 0x03:
                            UEP3_CTRL = (UEP3_CTRL & ~bUEP_R_TOG) | UEP_R_RES_STALL; // Set endpoint 3 OUT (RX) Stall (error)
                            break;
                        case 0x82:
                            UEP2_CTRL = (UEP2_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL; // Set endpoint 2 IN (TX) Stall (error)
                            break;
                        case 0x02:
                            UEP2_CTRL = (UEP2_CTRL & ~bUEP_R_TOG) | UEP_R_RES_STALL; // Set endpoint 2 OUT (RX) Stall (error)
                            break;
                        case 0x81:
                            UEP1_CTRL = (UEP1_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL; // Set endpoint 1 IN (TX) Stall (error)
                            break;
                        case 0x01:
                            UEP1_CTRL = (UEP1_CTRL & ~bUEP_R_TOG) | UEP_R_RES_STALL; // Set endpoint 1 OUT (RX) Stall (error)
                        default:
                            len = 0xFF; // Operation failed
                            break;
                        }
                    } else {
                        len = 0xFF; // Operation failed
                        printStrSetup("Unsupported\n");
                    }
                } else {
                    len = 0xFF; // Operation failed
                    printStrSetup("Unsupported\n");
                }
                break;

            case USB_GET_STATUS:
                printStrSetup("GetStatus\n");
                Ep0Buffer[0] = 0x00;
                Ep0Buffer[1] = 0x00;
                if (SetupLen >= 2) {
                    len = 2;
                } else {
                    len = SetupLen;
                }
                break;

            default:
                len = 0xFF; // Operation failed
                printStrSetup("Unsupported\n");
                break;

            } // END switch (SetupReq)
        } // END Standard request
    } else {
        len = 0xFF; // Packet length error
    }

    if (len == 0xFF) {
        SetupReq = 0xFF;
        UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL; // STALL
    } else if (len <= DEFAULT_EP0_SIZE) { // Upload data or status phase returns 0 length packet
        UEP0_T_LEN = len;
        UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK; // The default packet is DATA1, return response ACK
    } else {
        UEP0_T_LEN = 0; // Although it has not yet reached the status stage, it is preset to upload 0-length data packets in advance to prevent the host from entering the status stage early.
        UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK; // The default data packet is DATA1, and the response ACK is returned
    }
}

/*******************************************************************************
 * Function Name  : DeviceInterrupt()
 * Description    : CH559USB interrupt processing function
 *******************************************************************************/
#ifdef BUILD_CODE
#define IRQ_USB __interrupt(INT_NO_USB)
#else
#define IRQ_USB
#endif

void DeviceInterrupt(void)IRQ_USB // USB interrupt service routine, using register set 1
{
    uint16_t len;

    if (UIF_TRANSFER) { // Check USB transfer complete flag
        switch (USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {

        case UIS_TOKEN_SETUP | 0: // SETUP routine
            usb_irq_setup_handler();
            break;

        case UIS_TOKEN_IN | 0: // Endpoint 0 IN (TX)
            switch (SetupReq) {
            case USB_GET_DESCRIPTOR:
                len = SetupLen >= DEFAULT_EP0_SIZE ? DEFAULT_EP0_SIZE : SetupLen; // The length of this transmission
                memcpy(Ep0Buffer, pDescr, len); // Load upload data
                SetupLen -= len;
                pDescr += len;
                UEP0_T_LEN = len;
                UEP0_CTRL ^= bUEP_T_TOG; // Sync flag flip
                break;
            case USB_SET_ADDRESS:
                USB_DEV_AD = (USB_DEV_AD & bUDA_GP_BIT) | SetupLen;
                UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            default:
                UEP0_T_LEN = 0; // The status phase is completed and interrupted or the 0-length data packet is forced to be uploaded to end the control transmission.
                UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            }
            break;

        case UIS_TOKEN_IN | 1: // Endpoint 1 IN (TX), Endpoint interrupts upload
            UEP1_T_LEN = 0;    // Transmit length must be cleared (Endpoint 1)
            UEP1_CTRL = (UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK; // Default answer NAK
            break;

        case UIS_TOKEN_IN | 2: // Endpoint 2 IN (TX), Endpoint bulk upload
            UEP2_T_LEN = 0;    // Transmit length must be cleared (Endpoint 2)
            UEP2_CTRL = (UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK; // Default answer NAK
            Endpoint2UploadBusy = 0; // Clear busy flag
            break;

        case UIS_TOKEN_IN | 3: // Endpoint 3 IN (TX), Endpoint bulk upload
            UEP3_T_LEN = 0;    // Transmit length must be cleared (Endpoint 3)
            UEP3_CTRL = (UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK; // Default answer NAK
            Endpoint3UploadBusy = 0; // Clear busy flag
            break;

        case UIS_TOKEN_IN | 4: // Endpoint 4 IN (TX), Endpoint bulk upload
            UEP4_T_LEN = 0;    // Transmit length must be cleared (Endpoint 4)
            UEP4_CTRL = (UEP4_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK; // Default answer NAK
            UEP4_CTRL ^= bUEP_T_TOG; // Sync flag flip
            Endpoint4UploadBusy = 0; // Clear busy flag
            break;

        case UIS_TOKEN_OUT | 0: // Endpoint 0 OUT (RX)
            switch (SetupReq) {
            case USB_CDC_REQ_TYPE_SET_LINE_CODING: // We ignore line coding here because baudrate to the FPGA should not change
                if (U_TOG_OK) {
                    UEP0_T_LEN = 0;
                    UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_ACK; // Prepare to upload 0 packages
                }
                break;
            default:
                UEP0_T_LEN = 0;
                UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_NAK; // Status phase, responds to IN with NAK
            }
            break;

        case UIS_TOKEN_OUT | 1: // Endpoint 1 OUT (RX), Disabled for now.
            // Out-of-sync packets will be dropped
            if (U_TOG_OK) {
                //UsbEpXByteCount = USB_RX_LEN;                              // Length of received data
                //UEP1_CTRL = (UEP1_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK; // NAK after receiving a packet of data, the main function finishes processing, and the main function modifies the response mode
            }
            break;

        case UIS_TOKEN_OUT | 2: // Endpoint 2 OUT (RX), Endpoint batch download
            // Out-of-sync packets will be dropped
            if (U_TOG_OK) {
                UsbEp2ByteCount = USB_RX_LEN;                              // Length of received data
                UEP2_CTRL = (UEP2_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK; // NAK after receiving a packet of data, the main function finishes processing, and the main function modifies the response mode
            }
            break;

        case UIS_TOKEN_OUT | 3: // Endpoint 3 OUT (RX), Endpoint batch download
            // Out-of-sync packets will be dropped
            if (U_TOG_OK) {
                UsbEp3ByteCount = USB_RX_LEN;                              // Length of received data
                UEP3_CTRL = (UEP3_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK; // NAK after receiving a packet of data, the main function finishes processing, and the main function modifies the response mode
            }
            break;

        case UIS_TOKEN_OUT | 4: // Endpoint 4 OUT (RX), Endpoint batch download
            // Out-of-sync packets will be dropped
            if (U_TOG_OK) {
                UsbEp4ByteCount = USB_RX_LEN;                              // Length of received data
                UEP4_CTRL = (UEP4_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK; // NAK after receiving a packet of data, the main function finishes processing, and the main function modifies the response mode
                UEP4_CTRL ^= bUEP_R_TOG;                                   // Sync flag flip
            }
            break;

        default:
            break;
        }

        UIF_TRANSFER = 0; // Writing 0 clears the interrupt

    } else if (UIF_BUS_RST) { // Check device mode USB bus reset interrupt

        printStrSetup("Reset\n");

        UEP0_CTRL =                 UEP_T_RES_NAK | UEP_R_RES_ACK;
        UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
        UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
        UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
        UEP4_CTRL =                 UEP_T_RES_NAK | UEP_R_RES_ACK;
        USB_DEV_AD = 0x00;
        UIF_SUSPEND = 0;
        UIF_TRANSFER = 0;               // Writing 0 clears the interrupt
        UIF_BUS_RST = 0;                // Clear interrupt flag

        UartRxBufInputPointer = 0;      // Circular buffer input pointer
        UartRxBufOutputPointer = 0;     // Circular buffer read pointer
        UartRxBufByteCount = 0;         // The number of bytes remaining in the current buffer to be fetched
        UsbEp2ByteCount = 0;            // USB endpoint 2 (CDC) received length
        UsbEp3ByteCount = 0;            // USB endpoint 3 (FIDO) received length
        UsbEp4ByteCount = 0;            // USB endpoint 4 (DEBUG) received length
        Endpoint2UploadBusy = 0;        // Clear busy flag
        Endpoint3UploadBusy = 0;        // Clear busy flag
        Endpoint4UploadBusy = 0;        // Clear busy flag

        FrameMode = 0;

        UsbConfig = 0;                  // Clear configuration values

    } else if (UIF_SUSPEND) { // Check USB bus suspend/wake completed

        UIF_SUSPEND = 0;

        if (USB_MIS_ST & bUMS_SUSPEND) { // Hang

            printStrSetup("Suspend\n");

            while (XBUS_AUX & bUART0_TX) {
                ; // Wait for sending to complete
            }

            SAFE_MOD = 0x55;
            SAFE_MOD = 0xAA;
            WAKE_CTRL = bWAK_BY_USB | bWAK_RXD0_LO | bWAK_RXD1_LO; // Can be woken up when there is a signal from USB or RXD0/1
            PCON |= PD; // Sleep
            SAFE_MOD = 0x55;
            SAFE_MOD = 0xAA;
            WAKE_CTRL = 0x00;

        }
    } else { // Unexpected IRQ, should not happen
        printStrSetup("Unexpected IRQ\n");
        USB_INT_FG = 0xFF; // Clear interrupt flag
    }
}

/*******************************************************************************
 * Function Name  : Uart0_ISR()
 * Description    : Serial debug port receiving interrupt function to realize circular buffer receiving
 *******************************************************************************/
#ifdef DEBUG_PRINT_HW
#ifdef BUILD_CODE
#define IRQ_UART0 __interrupt(INT_NO_UART0)
#else
#define IRQ_UART0
#endif

void Uart0_ISR(void)IRQ_UART0
{
    // Check if data has been received
    if (RI) {
        DebugUartRxBuf[DebugUartRxBufInputPointer++] = SBUF;
        if (DebugUartRxBufInputPointer >= DEBUG_UART_RX_BUF_SIZE) {
            DebugUartRxBufInputPointer = 0; // Reset write pointer
        }
        RI = 0;
    }
}

uint8_t debug_uart_byte_count()
{
    uint8_t  in = DebugUartRxBufInputPointer;
    uint8_t out = DebugUartRxBufOutputPointer;

    if (in < out) {
        in = in + DEBUG_UART_RX_BUF_SIZE;
    }
    return in - out;
}
#endif

/*******************************************************************************
 * Function Name  : Uart1_ISR()
 * Description    : Serial port receiving interrupt function to realize circular buffer receiving
 *******************************************************************************/
#ifdef BUILD_CODE
#define IRQ_UART1 __interrupt(INT_NO_UART1)
#else
#define IRQ_UART1
#endif

void Uart1_ISR(void)IRQ_UART1
{
    // Check if data has been received
    if (U1RI) {
        UartRxBuf[UartRxBufInputPointer++] = SBUF1;
        if (UartRxBufInputPointer >= UART_RX_BUF_SIZE) {
            UartRxBufInputPointer = 0; // Reset write pointer
        }

        check_cts_stop();

        U1RI = 0;
    }
}

uint8_t uart_byte_count()
{
    uint8_t in = UartRxBufInputPointer;
    uint8_t out = UartRxBufOutputPointer;

    if (in >= out) {
        return (in - out);
    } else {
        return (UART_RX_BUF_SIZE - (out - in));
    }
}

// Copy data from a circular buffer
void circular_copy(uint8_t *dest, uint8_t *src, uint32_t src_size, uint32_t start_pos, uint32_t length) {

    // Calculate the remaining space from start_pos to end of buffer
    uint32_t remaining_space = src_size - start_pos;

    if (length <= remaining_space) {
        // If the length to copy doesn't exceed the remaining space, do a single memcpy
        memcpy(dest, src + start_pos, length);
    } else {
        // If the length to copy exceeds the remaining space, split the copy
        memcpy(dest, src + start_pos, remaining_space);                // Copy from start_pos to end of buffer
        memcpy(dest + remaining_space, src, length - remaining_space); // Copy the rest from the beginning of buffer
    }
}

// Function to increment a pointer and wrap around the buffer
uint32_t increment_pointer(uint32_t pointer, uint32_t increment, uint32_t buffer_size)
{
    return (pointer + increment) % buffer_size;
}

// Function to decrement a pointer and wrap around the buffer
uint32_t decrement_pointer(uint32_t pointer, uint32_t decrement, uint32_t buffer_size)
{
    return (pointer + buffer_size - (decrement % buffer_size)) % buffer_size;
}

void cts_start(void)
{
    gpio_p1_5_set(); // Signal to FPGA to send more data
}

void cts_stop(void)
{
    gpio_p1_5_unset(); // Signal to FPGA to not send more data
}

void check_cts_stop(void)
{
    if (uart_byte_count() >= 133) // UartRxBuf is filled to 95% or more
    {
        cts_stop();
    }
}

void main()
{
    CfgFsys();     // CH559 clock selection configuration
    mDelaymS(5);   // Modify the main frequency and wait for the internal crystal to stabilize, which must be added
#ifdef DEBUG_PRINT_HW
    mInitSTDIO();  // Serial port 0, can be used for debugging
#endif
    UART1Setup();  // For communication with FPGA
    UART1Clean();  // Clean register from spurious data

    printStrSetup("Startup\n");

    uint8_t ActiveEndpoints = RESET_KEEP;

    // Always enable CDC endpoint
    if ((ActiveEndpoints & IO_CDC) == 0x0) {
        ActiveEndpoints |= IO_CDC;
    }

    // Always enable CH552 endpoint
    if ((ActiveEndpoints & IO_CH552) == 0x0) {
        ActiveEndpoints |= IO_CH552;
    }

    CreateCfgDescriptor(ActiveEndpoints);

    USBDeviceCfg();
    USBDeviceEndPointCfg(); // Endpoint configuration
    USBDeviceIntCfg();      // Interrupt initialization

    UEP0_T_LEN = 0;         // Transmit length must be cleared (Endpoint 0)
    UEP1_T_LEN = 0;         // Transmit length must be cleared (Endpoint 1)
    UEP2_T_LEN = 0;         // Transmit length must be cleared (Endpoint 2)
    UEP3_T_LEN = 0;         // Transmit length must be cleared (Endpoint 3)
    UEP4_T_LEN = 0;         // Transmit length must be cleared (Endpoint 4)

    gpio_init_p1_4_in();    // Init GPIO p1.4 to input mode for FPGA_CTS
    gpio_init_p1_5_out();   // Init GPIO p1.5 to output mode for CH552_CTS
    cts_start();            // Signal OK to send

    while (1) {
        if (UsbConfig) {

            // Check if Endpoint 2 (CDC) has received data
            if (UsbEp2ByteCount) {
                Ep2ByteLen = UsbEp2ByteCount; // UsbEp2ByteCount can be maximum 64 bytes
                memcpy(UartTxBuf, Ep2Buffer, Ep2ByteLen);

                UsbEp2ByteCount = 0;
                CH554UART1SendByte(IO_CDC);  // Send CDC mode header
                CH554UART1SendByte(Ep2ByteLen);  // Send length
                CH554UART1SendBuffer(UartTxBuf, Ep2ByteLen);
                UEP2_CTRL = (UEP2_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK; // Enable Endpoint 2 to ACK again
            }

            // Check if Endpoint 3 (FIDO) has received data
            if (UsbEp3ByteCount) {
                Ep3ByteLen = UsbEp3ByteCount; // UsbEp3ByteCount can be maximum 64 bytes
                memcpy(UartTxBuf, Ep3Buffer, Ep3ByteLen);

                UsbEp3ByteCount = 0;
                CH554UART1SendByte(IO_FIDO); // Send FIDO mode header
                CH554UART1SendByte(Ep3ByteLen); // Send length (always 64 bytes)
                CH554UART1SendBuffer(UartTxBuf, Ep3ByteLen);
                UEP3_CTRL = (UEP3_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK; // Enable Endpoint 3 to ACK again
            }

            // Check if Endpoint 4 (DEBUG) has received data
            if (UsbEp4ByteCount) {
                Ep4ByteLen = UsbEp4ByteCount; // UsbEp4ByteCount can be maximum 64 bytes
                memcpy(UartTxBuf, Ep0Buffer+64, Ep4ByteLen); // Endpoint 4 receive is at address UEP0_DMA+64

                UsbEp4ByteCount = 0;
                CH554UART1SendByte(IO_DEBUG); // Send DEBUG mode header
                CH554UART1SendByte(Ep4ByteLen); // Send length (always 64 bytes)
                CH554UART1SendBuffer(UartTxBuf, Ep4ByteLen);
                UEP4_CTRL = (UEP4_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK; // Enable Endpoint 4 to ACK again
            }

            UartRxBufByteCount = uart_byte_count(); // Check amount of data in buffer

            if ((UartRxBufByteCount >= 2) && !FrameStarted) {  // If we have data and the header is not yet validated
                FrameMode = UartRxBuf[UartRxBufOutputPointer]; // Extract frame mode
                if ((FrameMode == IO_CDC)   ||
                    (FrameMode == IO_FIDO)  ||
                    (FrameMode == IO_DEBUG) ||
                    (FrameMode == IO_CH552)) {

                    FrameLength = UartRxBuf[increment_pointer(UartRxBufOutputPointer,
                                                              1,
                                                              UART_RX_BUF_SIZE)]; // Extract frame length
                    FrameRemainingBytes = FrameLength;
                    UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                               2,
                                                               UART_RX_BUF_SIZE); // Start at valid data so skip the mode and length byte
                    UartRxBufByteCount -= 2; // Subtract the frame mode and frame length bytes from the total byte count
                    FrameStarted = 1;

                    // Mark that we should discard data if destination for the frame is not active
                    if ((FrameMode & ActiveEndpoints) == 0) {
                        FrameDiscard = 1;
                    }

                } else { // Invalid frame mode

                    cts_stop();

                    // Reset CH552 to start from a known state
                    SAFE_MOD = 0x55;
                    SAFE_MOD = 0xAA;
                    GLOBAL_CFG = bSW_RESET;
                    while (1)
                        ;
                }
            }

            // Copy CDC data from UartRxBuf to FrameBuf
            if (FrameStarted && !FrameDiscard && !CdcDataAvailable) {
                if (FrameMode == IO_CDC) {
                    if ((FrameRemainingBytes >= MAX_FRAME_SIZE) &&
                        (UartRxBufByteCount >= MAX_FRAME_SIZE)) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      MAX_FRAME_SIZE);
                        FrameBufLength = MAX_FRAME_SIZE;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   MAX_FRAME_SIZE,
                                                                   UART_RX_BUF_SIZE);
                        FrameRemainingBytes -= MAX_FRAME_SIZE;
                        CdcDataAvailable = 1;
                        cts_start();
                    }
                    else if ((FrameRemainingBytes < MAX_FRAME_SIZE) &&
                             (UartRxBufByteCount >= FrameRemainingBytes)) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      FrameRemainingBytes);
                        FrameBufLength = FrameRemainingBytes;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   FrameRemainingBytes,
                                                                   UART_RX_BUF_SIZE);
                        FrameRemainingBytes -= FrameRemainingBytes;
                        CdcDataAvailable = 1;
                        cts_start();
                    }
                }
            }

            // Copy FIDO data from UartRxBuf to FrameBuf
            if (FrameStarted && !FrameDiscard && !FidoDataAvailable) {
                if (FrameMode == IO_FIDO) {
                    // Check if a complete frame has been received
                    if (UartRxBufByteCount >= FrameRemainingBytes) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      FrameRemainingBytes);
                        FrameBufLength = MAX_FRAME_SIZE;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   FrameRemainingBytes,
                                                                   UART_RX_BUF_SIZE);
                        FidoDataAvailable = 1;
                        cts_start();
                    }
                }
            }

            // Copy DEBUG data from UartRxBuf to FrameBuf
            if (FrameStarted && !FrameDiscard && !DebugDataAvailable) {
                if (FrameMode == IO_DEBUG) {
                    if ((FrameRemainingBytes >= MAX_FRAME_SIZE) &&
                        (UartRxBufByteCount >= MAX_FRAME_SIZE)) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      MAX_FRAME_SIZE);
                        FrameBufLength = MAX_FRAME_SIZE;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   MAX_FRAME_SIZE,
                                                                   UART_RX_BUF_SIZE);
                        FrameRemainingBytes -= MAX_FRAME_SIZE;
                        DebugDataAvailable = 1;
                        cts_start();
                    }
                    else if ((FrameRemainingBytes < MAX_FRAME_SIZE) &&
                             (UartRxBufByteCount >= FrameRemainingBytes)) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      FrameRemainingBytes);
                        FrameBufLength = FrameRemainingBytes;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   FrameRemainingBytes,
                                                                   UART_RX_BUF_SIZE);
                        FrameRemainingBytes -= FrameRemainingBytes;
                        DebugDataAvailable = 1;
                        cts_start();
                    }
                }
            }

            // Copy CH552 data from UartRxBuf to FrameBuf
            if (FrameStarted && !FrameDiscard && !CH552DataAvailable) {
                if (FrameMode == IO_CH552) {
                    if ((FrameRemainingBytes >= MAX_FRAME_SIZE) &&
                        (UartRxBufByteCount >= MAX_FRAME_SIZE)) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      MAX_FRAME_SIZE);
                        FrameBufLength = MAX_FRAME_SIZE;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   MAX_FRAME_SIZE,
                                                                   UART_RX_BUF_SIZE);
                        FrameRemainingBytes -= MAX_FRAME_SIZE;
                        CH552DataAvailable = 1;
                        cts_start();
                    }
                    else if ((FrameRemainingBytes < MAX_FRAME_SIZE) &&
                             (UartRxBufByteCount >= FrameRemainingBytes)) {
                        circular_copy(FrameBuf,
                                      UartRxBuf,
                                      UART_RX_BUF_SIZE,
                                      UartRxBufOutputPointer,
                                      FrameRemainingBytes);
                        FrameBufLength = FrameRemainingBytes;
                        // Update output pointer
                        UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                                   FrameRemainingBytes,
                                                                   UART_RX_BUF_SIZE);
                        FrameRemainingBytes -= FrameRemainingBytes;
                        CH552DataAvailable = 1;
                        cts_start();
                    }
                }
            }

            // Discard frame
            if (FrameStarted && FrameDiscard && !DiscardDataAvailable) {
                if ((FrameRemainingBytes >= MAX_FRAME_SIZE) &&
                    (UartRxBufByteCount >= MAX_FRAME_SIZE)) {
                    // Update output pointer
                    UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                               MAX_FRAME_SIZE,
                                                               UART_RX_BUF_SIZE);
                    FrameRemainingBytes -= MAX_FRAME_SIZE;
                    DiscardDataAvailable = 1;
                    cts_start();
                }
                else if ((FrameRemainingBytes < MAX_FRAME_SIZE) &&
                        (UartRxBufByteCount >= FrameRemainingBytes)) {
                    // Update output pointer
                    UartRxBufOutputPointer = increment_pointer(UartRxBufOutputPointer,
                                                               FrameRemainingBytes,
                                                               UART_RX_BUF_SIZE);
                    FrameRemainingBytes -= FrameRemainingBytes;
                    DiscardDataAvailable = 1;
                    cts_start();
                }
            }

            // Check if we should upload data to Endpoint 2 (CDC)
            if (CdcDataAvailable && !Endpoint2UploadBusy) {

                // Write upload endpoint
                memcpy(Ep2Buffer + MAX_PACKET_SIZE, /* Copy to IN buffer of Endpoint 2 */
                       FrameBuf,
                       FrameBufLength);

                Endpoint2UploadBusy = 1; // Set busy flag
                UEP2_T_LEN = FrameBufLength; // Set the number of data bytes that Endpoint 2 is ready to send
                UEP2_CTRL = (UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK; // Answer ACK

                CdcDataAvailable = 0;
                FrameBufLength = 0;

                if (FrameRemainingBytes == 0) {
                    // Complete frame sent, get next header and data
                    FrameStarted = 0;
                }
            }

            // Check if we should upload data to Endpoint 3 (FIDO)
            if (FidoDataAvailable && !Endpoint3UploadBusy) {

                // Write upload endpoint
                memcpy(Ep3Buffer + MAX_PACKET_SIZE, /* Copy to IN buffer of Endpoint 3 */
                       FrameBuf,
                       FrameBufLength);

                Endpoint3UploadBusy = 1; // Set busy flag
                UEP3_T_LEN = MAX_PACKET_SIZE; // Set the number of data bytes that Endpoint 3 is ready to send
                UEP3_CTRL = (UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK; // Answer ACK

                FidoDataAvailable = 0;
                FrameBufLength = 0;

                // Get next header and data
                FrameStarted = 0;
            }

            // Check if we should upload data to Endpoint 4 (DEBUG)
            if (DebugDataAvailable && !Endpoint4UploadBusy) {

                if (FrameBufLength == MAX_PACKET_SIZE) {
                    // Write upload endpoint
                    memcpy(Ep0Buffer + 128, /* Copy to IN (TX) buffer of Endpoint 4 */
                           FrameBuf,
                           FrameBufLength);
                } else {
                    memset(Ep0Buffer + 128, 0, MAX_PACKET_SIZE);
                    // Write upload endpoint
                    memcpy(Ep0Buffer + 128, /* Copy to IN (TX) buffer of Endpoint 4 */
                           FrameBuf,
                           FrameBufLength);
                }

                Endpoint4UploadBusy = 1; // Set busy flag
                UEP4_T_LEN = MAX_PACKET_SIZE; // Set the number of data bytes that Endpoint 4 is ready to send
                UEP4_CTRL = (UEP4_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK; // Answer ACK

                DebugDataAvailable = 0;
                FrameBufLength = 0;

                if (FrameRemainingBytes == 0) {
                    // Complete frame sent, get next header and data
                    FrameStarted = 0;
                }
            }

            // Check if we should handle CH552 data
            if (CH552DataAvailable) {

                switch (FrameBuf[0]) {

                    case SET_ENDPOINTS: {
                        cts_stop(); // Stop UART data from FPGA
                        RESET_KEEP = FrameBuf[1]; // Save endpoints to persistent register
                        SAFE_MOD = 0x55; // Start reset sequence
                        SAFE_MOD = 0xAA;
                        GLOBAL_CFG = bSW_RESET;
                        while (1)
                            ;
                    }
                    break;

                    default: {
                    }
                    break;

                }

                CH552DataAvailable = 0;
                FrameBufLength = 0;

                memset(FrameBuf, 0, MAX_FRAME_SIZE);

                if (FrameRemainingBytes == 0) {
                    // Complete frame sent, get next header and data
                    FrameStarted = 0;
                }
            }

            if (DiscardDataAvailable) {

                DiscardDataAvailable = 0;
                printStr("Frame discarded!\n");

                if (FrameRemainingBytes == 0) {
                    // Complete frame discarded, get next header and data
                    FrameStarted = 0;
                    // Stop discarding frames
                    FrameDiscard = 0;
                }
            }

        } /* END if (UsbConfig) */
    } /* END while (1) */
}
