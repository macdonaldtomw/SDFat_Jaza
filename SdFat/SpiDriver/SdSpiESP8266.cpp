/**
* Copyright (c) 20011-2017 Bill Greiman
* This file is part of the SdFat library for SD memory cards.
*
* MIT License
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#if defined(ESP8266)
#include "SdSpiDriver.h"

#include "TestingConfig.h"
//------------------------------------------------------------------------------
/** Initialize the SPI bus.
*
* \param[in] chipSelectPin SD card chip select pin.
*/
void SdSpiAltDriver::begin(uint8_t csPin) {
   m_csPin = csPin;
   pinMode(m_csPin, OUTPUT);
   digitalWrite(m_csPin, HIGH);
   // SPI.begin();
}
//------------------------------------------------------------------------------
/** Set SPI options for access to SD/SDHC cards.
*
*/
void SdSpiAltDriver::activate() {
   SPI.beginTransaction(m_spiSettings);
}
//------------------------------------------------------------------------------
void SdSpiAltDriver::deactivate() {
   // Note: endTransaction is an empty function on ESP8266.
   SPI.endTransaction();
}
//------------------------------------------------------------------------------
/** Receive a byte.
*
* \return The byte.
*/
uint8_t SdSpiAltDriver::receive() {
   #ifdef TEST_MODE_VERBOSE_SDFAT_DEBUG
   if(SD_FAT_DEBUG_ENABLED)
   Serial.printlnf("SPI transfer - line %u", __LINE__);
   #endif
   return SPI.transfer(0XFF);
}
//------------------------------------------------------------------------------
/** Receive multiple bytes.
*
* \param[out] buf Buffer to receive the data.
* \param[in] n Number of bytes to receive.
*
* \return Zero for no error or nonzero error code.
*/
uint8_t SdSpiAltDriver::receive(uint8_t* buf, size_t n) {
   #ifdef TEST_MODE_VERBOSE_SDFAT_DEBUG
   if(SD_FAT_DEBUG_ENABLED)
   Serial.printlnf("SPI transferBytes - line %u", __LINE__);
   #endif
   // Works without 32-bit alignment of buf.
   SPI.transferBytes(0, buf, n);
   return 0;
}
//------------------------------------------------------------------------------
/** Send a byte.
*
* \param[in] b Byte to send
*/
void SdSpiAltDriver::send(uint8_t b) {
   #ifdef TEST_MODE_VERBOSE_SDFAT_DEBUG
   if(SD_FAT_DEBUG_ENABLED)
   Serial.printlnf("SPI transfer - line %u", __LINE__);
   #endif
   SPI.transfer(b);
}
//------------------------------------------------------------------------------
/** Send multiple bytes.
*
* \param[in] buf Buffer for data to be sent.
* \param[in] n Number of bytes to send.
*/
void SdSpiAltDriver::send(const uint8_t* buf , size_t n) {
   // Adjust to 32-bit alignment.
   while ((reinterpret_cast<uintptr_t>(buf) & 0X3) && n) {
      #ifdef TEST_MODE_VERBOSE_SDFAT_DEBUG
      if(SD_FAT_DEBUG_ENABLED)
      Serial.printlnf("SPI transfer - line %u", __LINE__);
      #endif
      SPI.transfer(*buf++);
      n--;
   }
   #ifdef TEST_MODE_VERBOSE_SDFAT_DEBUG
   if(SD_FAT_DEBUG_ENABLED)
   Serial.printlnf("SPI transferBytes - line %u", __LINE__);
   #endif
   SPI.transferBytes(const_cast<uint8_t*>(buf), 0, n);
}
#endif  // defined(ESP8266)
