//PARTICLE_STUDIO_READY


/**
*
*

This file contains implementations of functions for the jazaSD utility, which allows
the rest of our app to easily read and write data to the SD card in a csv formatted way

*
*/


//---------------------------------- DEFINES -----------------------------------

// ----------------------------------INCLUDES-----------------------------------
// #include "myParticle.h"

#include "JazaSD.h"

#include "Debug/EscapeChars.h"

#include "Debug/PrintHelper.h"

#include "PCB/jazaPinout.h"

#include "TestingConfig.h"

#include "RetainedVars.h"

#include "Publishing/JazaPublish.h"

#include "Reboot/RebootManager.h"

#include "Database/DB_Manager.h"

#include "TestingConfig.h"

#ifdef TEST_MODE_INCLUDE_LAB_EQUIPMENT_HEADERS
#include "LabEquipment.h"
#endif




/*=============================================>>>>>
= Common debug messages =
===============================================>>>>>*/
// const char* mes_obj_sd = "SD card";
// const char* mes_sd_fileSeekError = "File seek error";
// const char* mes_sd_writeError = "Write error";
// const char* mes_err_sanity = "INSANE";
// const char* mes_buf_Small = "buffer too small";
// const char* mes_inValid = "invalid";
// const char* mes_sd_noEntries = "No entries";
// const char* mes_sd_readError = "SD read error";
// const char* mes_err_thrown = "error thrown";
// const char* mes_sd_variableWidth = "variable width";


/*=============================================>>>>>
= EXTERNALLY LINKED GLOBALS DEFINITIONS =
===============================================>>>>>*/
// bool unhandled_SD_hard_fault = false;
JazaSD jazaSD;   //The global utility of the JazaSD class (with external linkage)
char sdWriteBuf[SD_BUF_SIZE]; //Buffer for writing to the SD card (externally linked!)
char sdBuf[SD_BUF_SIZE];       //I think SDFat uses a 512 byte buffer for writing....
char filePathBuf[100];

SPISettings my_spi_settings(10 * MHZ, MSBFIRST, SPI_MODE0);

/*=============================================>>>>>
= GLOBAL VARIABLES =
===============================================>>>>>*/
//Debug logging object
static Logger myLog(mes_obj_sd);
//SDFat library objects
SdFat sd;            //The instance of the SDFat utility
SdFile file;   //Instance of the SdFile class (from SDFat library)
SdFile archiveFile; //File used during archiving process
SdFile copyFile;

int sd_free_space_KB = 0;

const uint8_t chipSelect = SD_CHIPSELECT;  //Use the default Electron Slave Select pin

bool SD_AUTOSYNC_ENABLED = true; //Global flag for disabling auto-sync when changing multiple entries in a file

bool SD_FAT_DEBUG_ENABLED = false;  //Global flag for enabling/disabling SPI debug messaging in SdFat library

JAZA_FILES_t currentlyOpenFile = NUM_TYPES_JAZA_FILES;

unsigned int lastGetEntryNum = 0;
int lastGetEntryStartPos = 0;

//Declare an array of JazaFile_t files that we are going to use
JazaFile_t jazaFiles[NUM_TYPES_JAZA_FILES]  = {
   [FILE_CHANNEL_INFO]     = JazaFile_t("channelInfo.csv", true),
   [FILE_USERTABLE]        = JazaFile_t("userTable.csv", true),
   [FILE_JAZAPACKTABLE]    = JazaFile_t("jpTable.csv", true),
   [FILE_QUEUE_TOCHARGE]   = JazaFile_t("queueToCharge.csv",true),
   [FILE_QUEUE_CHARGED]    = JazaFile_t("queueCharged.csv",true),
   [FILE_HUB_PROPERTIES]   = JazaFile_t("hubProperties.csv"),
   [FILE_JAZAOFFERINGS]    = JazaFile_t("jazaOfferings.csv"),
   [FILE_PUBLISH_HISTORY]  = JazaFile_t("publishHistory.csv"),
   [FILE_PUBLISH_REGISTRY] = JazaFile_t("publishRegistry.csv", true),
   [FILE_PUBLISH_BACKLOG]  = JazaFile_t("publishBacklog.csv", true),
   [FILE_STORED_STRINGS]   = JazaFile_t("strings.csv"),
   [FILE_TEMP_FILE]        = JazaFile_t("temp.csv"),
   [FILE_JP_HEX_FILE]      = JazaFile_t("firmware.hex")
};

/*=============================================>>>>>
= Function forward declarations =
===============================================>>>>>*/
// void dateAndTimeForFats(uint16_t* date, uint16_t *time);

bool smartFileOpen(JAZA_FILES_t fileType);

uint32_t skipPastNextDelimiter(JAZA_FILES_t fileType, const char* targDelimiter);
bool skipPastNextDelimiters(JAZA_FILES_t fileType, const char* targDelimiter, uint16_t numDelimitersToSkip);
uint32_t numTargDelimitersBeforePosition(JAZA_FILES_t fileType, const char* targDelimiter, uint32_t targPos = 0);
uint32_t numDelimitersInFile(JAZA_FILES_t fileType, const char* targDelimiter);
bool hasADelimiter(JAZA_FILES_t fileType, const char* targDelimiter);

// bool goToNextEntry(JAZA_FILES_t fileType);
uint32_t getCurrentEntryNumber(JAZA_FILES_t fileType, uint32_t seekSpecific = 0);


/*= End of Function forward declarations =*/
/*=============================================<<<<<*/


/*=============================================>>>>>
= SD ERROR LOGIC/PSEUDOCODE =
===============================================>>>>>*/
/**
1) Publish SD error warn event (block until published)
2) Reset with reason == APP_RESET_SD_HARD_FAULT_RECOVERY_ATTEMPT
3) If second SD error is encountered, enter safe mode
4) If no SD error is encountered, publish safe mode avoided event
*/
/*= End of SD ERROR LOGIC/PSEUDOCODE =*/
/*=============================================<<<<<*/


/*=============================================>>>>>
= Function to call when an SD error is encountered =
===============================================>>>>>*/
uint32_t last_sd_recovery_attempt = 0;
void SD_error_handler(unsigned int lineNum){
   //Debug print
   myLog.warn("SD error handler! -- ");
   //Set global SD utility state variables
   SD_INITIALIZED = false;

   if(
      millis() - last_sd_recovery_attempt > 60*60*1000
      ||
      last_sd_recovery_attempt == 0
   ){
      last_sd_recovery_attempt = millis();
      //Get the error information from SDFat library
      int sdErrorCode = sd.cardErrorCode();
      int sdErrorData = sd.cardErrorData();
      //Attempt to fix the SD card by power cycling it
      myLog.warn("Attempting to recover SD card through power cycle");
      Serial.flush();
      digitalWrite(SD_POWER_ENABLE, LOW);
      delay(500);
      digitalWrite(SD_POWER_ENABLE, HIGH);
      delay(500);

      snprintf(
         sdWriteBuf,
         SD_BUF_SIZE,
         "SD Error | %s | 0X%02X | 0X%02X | Recovered = %s",
         jazaFiles[currentlyOpenFile].name,
         sdErrorCode,
         sdErrorData,
         SD_INITIALIZED?"TRUE":"FALSE"
      );

      //See if we can get SD card to start back up
      jazaSD.begin();
      if(SD_INITIALIZED){
         myLog.warn("Successfully recovered from SD error!");
         last_sd_recovery_attempt = 0;
      }
      else{
         myLog.warn("Failed to re-initialize SD card!!!");
      }


      //Create warn message with error data payload
      snprintf(
         sdWriteBuf,
         SD_BUF_SIZE,

         "\"DESC\":\"SD ERROR\","
         "\"FILE\":\"%s\","
         "\"ERROR_CODE\":0X%02X,"
         "\"ERROR_DATA\":0X%02X,"
         "\"RECOVERED\":\"%s\""
         ,
         jazaFiles[currentlyOpenFile].name,
         sdErrorCode,
         sdErrorData,
         SD_INITIALIZED?"TRUE":"FALSE"
      );
      myLog.warn("Recovery algorithm result:");
      Serial.println(sdWriteBuf);

      #if defined(TEST_MODE_VERBOSE_SAFE_MODE_DEBUG) || defined(TEST_MODE_VERBOSE_JAZASD_DEBUG)
      myLog.warn("Publishing following warning: %s", sdWriteBuf);
      #endif
      //Publish warning to da web mon (block until published)
      jazaPublish.publishWarning(
         mes_obj_sd,
         lineNum,
         sdWriteBuf,
         false,
         WARNPUB_SD_ERROR,
         FALSE//don't save to sd card... this could result in a panic loop
      );
   }

   return;

}




/*=============================================>>>>>
= Callback that SDFat library will use to adjust date modified/created timestamps of files on SD card =
===============================================>>>>>*/
void dateTime(uint16_t* date, uint16_t* time) {
   // uint16_t year = Time.year(the_time);
   // uint8_t month, day, hour, minute, second;
   // return date using FAT_DATE macro to format fields
   *date = FAT_DATE(Time.year(the_time), Time.month(the_time), Time.day(the_time));
   // return time using FAT_TIME macro to format fields
   *time = FAT_TIME(Time.hour(the_time), Time.month(the_time), Time.second(the_time));

}


/*=============================================>>>>>
= Function that syncs the currently open file to the SD card =
===============================================>>>>>*/
bool syncFile(unsigned int lineNum, SdFile* targFile = NULL){

   //IF autosync is disabled then quit
   if(!SD_AUTOSYNC_ENABLED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
      myLog.warn("Skipping SD sync!");
      #endif
      return true;
   }

   //Cooldown after publishing
   //(avoids writing to SD card right after publish when cell is transmitting)
   while(lastPublishTimer.elapsedTime() < MIN_MS_BEFORE_SD_WRITE_AFTER_PUBLISH){
      Particle_Process();
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.warn("SD Waiting for post-publish cooldown");
      #endif
   };

   //Pat the watchdog before syncing (in case it is about to reset Electron mid-write)
   HW_Watchdog.pat();

   //Debug output the fact that we're about to sync the file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("targFile.sync() - L%u", lineNum);
   #endif

   if(targFile == NULL){
      targFile = &file;
   }
   //Sync the file
   bool syncSuccess = targFile->sync();

   //Save timestamp of last SD write
   last_sd_write_millis = millis();

   if(!syncSuccess){
      SD_error_handler(lineNum);
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("Done syncing SD file");
   #endif

   return true;
}


bool smartFileClose(){
   currentlyOpenFile = NUM_TYPES_JAZA_FILES;
   if(!file.close()){
      return false;
   }
   return true;
}
/*=============================================>>>>>
= Function for opening files =
===============================================>>>>>*/
//Function open the corresponding sdFat file for the passed jazaFile
bool smartFileOpen(JAZA_FILES_t fileType){

   // #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   // myLog.info("smartFileOpen %s", jazaFiles[fileType].name);
   // #endif
   //
   // #ifdef TEST_MODE_SIMULATE_SD_CRASH_AFTER_TIME
   // if(millis() > TEST_MODE_SIMULATE_SD_CRASH_AFTER_TIME){
   //    SD_error_handler(__LINE__);
   //    return false;
   // }
   // #endif


   //Check if the target file is already open
   // if(jazaFiles[fileType].isOpen){
   if(
      currentlyOpenFile == fileType
      && file.isOpen()
   ){
      //File was already open!
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.info("%s already open", jazaFiles[fileType].name);
      #endif
      return true;
   }
   //File was not already open!
   else{
      //Close the file
      smartFileClose();
   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("file.open(\"%s\") - L%u", jazaFiles[fileType].name, __LINE__);
   #endif
   //Open the target file
   if( !file.open( jazaFiles[fileType].name , (O_RDWR | O_CREAT) ) ){
      //Error opening the target file!
      SD_error_handler(__LINE__);
      //Return
      return false;
   }


   //Debug
   // #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   // myLog.trace("Done opening file");
   // #endif

   //Save the fact that this file is now open
   // jazaFiles[fileType].isOpen = true;
   currentlyOpenFile = fileType;
   //Success!
   return true;
}


/*=============================================>>>>>
= HELPER FUNCTIONS INSIDE A FILE =
===============================================>>>>>*/

/*=============================================>>>>>
= Function that sets file seek position to just after
the first occurence of the target delimiter relative to
the current file position =
===============================================>>>>>*/

//Function that sets file seek position to just after the first instance of the target delimiter from current position
//input: target file, target delimiter
uint32_t skipPastNextDelimiter(JAZA_FILES_t fileType, const char* targDelimiter){

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.info("skipPastNextDelimiter(\"%s\")", jazaFiles[fileType].name);
   #endif
   //If SD has not been initialized yet, get outta here
   if(!SD_INITIALIZED) return 0;
   //Make sure that the desired file is currently open
   if(!smartFileOpen(fileType)){
      return 0;
   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.trace("file.curPosition() - L%u", __LINE__);
   #endif
   //Save the original position within the file
   uint32_t origPos = file.curPosition();
   //Determine the length of the delimiter we're looking for
   uint8_t delimiterLength = strlen(targDelimiter);
   //Control bool that will break while loop once whole file has been searched
   bool endOfFileReached = false;
   //Loop through file until we have reached the end of file
   while(!endOfFileReached){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.trace("file.curPosition() - L%u", __LINE__);
      #endif
      //Save starting location of search
      uint32_t searchStartPos = file.curPosition();
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.trace("file.read() - L%u", __LINE__);
      printFreeMem();
      #endif
      //Fill our SD analysis buffer
      int bytesRead = file.read(sdBuf, SD_BUF_SIZE);
      //Error check on the read
      if(bytesRead < 0){
         SD_error_handler(__LINE__);
         return 0;
      }
      //Variable that stores whether or not we have reached the end of file
      endOfFileReached = (bytesRead < SD_BUF_SIZE);
      //Define a char pointer that will point to location in our buf where the delimiter is found
      char* delimPtr = strstr(sdBuf, targDelimiter);
      //Did we find a match?
      if(delimPtr){

         unsigned int delimStartChar = searchStartPos + (delimPtr - sdBuf);
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.trace("Found delimiter at char %u", delimStartChar);
         #endif
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.trace("file.seekSet() - L%u", __LINE__);
         #endif
         //Found a delimiter substring within the bytes we're analyzing!
         file.seekSet(delimStartChar + delimiterLength);
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.trace("file.curPosition() - L%u", __LINE__);
         #endif
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.trace("Set file position to %lu (skipped %lu chars)", file.curPosition(), (file.curPosition() - origPos) );
         #endif

         return (file.curPosition() - origPos);
      }
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.trace("file.seekSet() - L%u", __LINE__);
      #endif
      //Didn't find a delimiter, so set file position to end of last read minus
      //a delimiter length (in case the delimiter is bridged across read sectors)
      file.seekSet(searchStartPos + bytesRead - delimiterLength);
   }; //End while loop

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.trace("file.seekSet() - L%u", __LINE__);
   #endif
   //Didn't find the delimiter to skip past in the whole file!
   //Set file position back to what it was
   file.seekSet(origPos);
   return 0;
}


//Function that skips ahead/behind num chars and returns total number of bytes skipped past
bool skipPastNextDelimiters(JAZA_FILES_t fileType, const char* targDelimiter, uint16_t numDelimitersToSkip){

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.info("skipPastNextDelimiters - %u delims", numDelimitersToSkip);
   #endif

   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }
   //// printTrace(myLog, __LINE__, "skipPastNextDelimiters()");
   // printFileContents(fileType, true);

   //Check if our work is already done!
   if(numDelimitersToSkip == 0){
      return 0;
   }

   if(!smartFileOpen(fileType)){
      return 0;
   }

   uint32_t origPos = file.curPosition();  //Save the original position to go back to in case there's a problem
   unsigned int delimitersSkipped = 0;                    //Counts the number of end line characters encountered


   while(delimitersSkipped < numDelimitersToSkip){
      ///*SD_FUCKERY*/      char tempBuf[50];
      ///*SD_FUCKERY*/      snprintf(tempBuf, 50, "-->looking for delimiter #%u", delimitersSkipped + 1);
      ///*SD_FUCKERY*/     // printTrace(myLog, __LINE__, tempBuf);
      //Read past next char
      uint32_t result = skipPastNextDelimiter(fileType, targDelimiter);
      ///*SD_FUCKERY*/     // printTrace(myLog, __LINE__, "skipPastNextDelimiter returned");
      ///*SD_FUCKERY*/      Serial.printlnf("Result of: %u", (unsigned int)result);
      if( result > 0){
         ///*SD_FUCKERY*/        // printTrace(myLog, __LINE__, "got here");
         //Successfully skipped past next targChar
         delimitersSkipped++;
      }
      else{
         ///*SD_FUCKERY*/        // printTrace(myLog, __LINE__, "breaking out of while loop");
         //No more chars of this type to skip past!
         break;
      }
      ///*SD_FUCKERY*/     // printTrace(myLog, __LINE__, "got here");
      ///*SD_FUCKERY*/      Serial.printlnf("delimitersFound = %u", delimitersSkipped);
   };

   ///*SD_FUCKERY*/printTrace(myLog, __LINE__, "Found all desired delimiters, continuing");

   //Check if we skipped to where we were told to
   if( delimitersSkipped < numDelimitersToSkip ){
      //Couldn't skip that many chars!
      ///*SD_FUCKERY*/myLog.warn("Couldn't skip ahead %d delimiters!  Not moving file position", numDelimitersToSkip);
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.seekSet(origPos) - L%u", __LINE__);
      #endif
      file.seekSet(origPos);

   }
   else if(delimitersSkipped == numDelimitersToSkip){
      return true;
   }
   ////myLog.trace("Skipped past %lu delimiters", delimitersSkipped);

   // if(file.curPosition() < origPos){
   //   ///*SD_FUCKERY*/printError(myLog, __LINE__, "IS THIS WHY THINGS ARE GETTINGS WEIRD?");
   //   return false;
   // }

   //Return the number of bytes that have been skipped past

   return false;
}

//Function that counts the number of instances of passed delimiter before the current position
uint32_t numTargDelimitersBeforePosition(JAZA_FILES_t fileType, const char* targDelimiter, uint32_t targPos){


   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("numTargDelimitersBeforePosition(\"%s\")", jazaFiles[fileType].name);
   #endif

   if(!SD_INITIALIZED) return 0;

   if(!smartFileOpen(fileType)) return 0;

   uint32_t origPos = file.curPosition();
   uint32_t startPos;

   //Check if we are using the current position of the file, or a passed position
   if(targPos != 0){
      startPos = targPos;
   }
   else{
      startPos = origPos;
   }

   // myLog.trace("numTargDelimitersBeforePosition()... targDelimiter = \"%s\"   startPos = %lu", getEscapedStr(targDelimiter), startPos);

   // ////myLog.trace("Finding number of delimiters \"%s\" before position %lu", getEscapedStr(targDelimiter), startPos);

   // ////myLog.trace("Getting entry number corresponding to file position %d", startPos);
   if(startPos == 0){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.trace("Start pos is 0...");
      #endif
      return 0;
   }

   //Go to start of the file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.rewind() - L%u", __LINE__);
   #endif
   file.rewind();

   uint32_t numDelimiters = 0;

   unsigned int result = 0;

   //Loop through the file from targChar to targChar
   while((result = skipPastNextDelimiter(fileType, targDelimiter)) > 0){
      //
      // myLog.trace("skipPastNextDelimiter() = %u", result);
      //Check that the position didn't go past starting position
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.curPosition()");
      #endif
      if(file.curPosition() <= startPos){
         numDelimiters++;
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
         myLog.trace("Delimiter # %lu at file position %lu", numDelimiters, file.curPosition());
         #endif
      }
      else{
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
         myLog.trace("Passed startPos");
         #endif
         break;
      }
   };

   // ////myLog.trace("There are %lu delimiters before position %lu", numDelimiters, file.curPosition());
   //Restore file position to original position when function was called
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekSet(origPos) - L%u", __LINE__);
   #endif
   file.seekSet(origPos);

   return numDelimiters;
}


uint32_t numDelimitersInFile(JAZA_FILES_t fileType, const char* targDelimiter){
   if(!SD_INITIALIZED) return 0;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("numDelimitersInFile(\"%s\")", jazaFiles[fileType].name);
   #endif

   if(smartFileOpen(fileType)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.fileSize()");
      #endif
      return numTargDelimitersBeforePosition(fileType, targDelimiter, file.fileSize());
   }
   return 0;
}




//Returns the number of entry delimiters plus one between current file position
//and beginning of the file
uint32_t getCurrentEntryNumber(JAZA_FILES_t fileType, uint32_t seekSpecific){
   if(!SD_INITIALIZED) return 0;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("getCurrentEntryNumber(\"%s\")", jazaFiles[fileType].name);
   #endif

   if(seekSpecific > 0){
      if(smartFileOpen(fileType)){
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.fileSize()");
         #endif
         if(seekSpecific < file.fileSize()){
            // myLog.trace("Specific seek: %u", seekSpecific);
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
            myLog.info("file.seekSet(seekSpecific) - L%u", __LINE__);
            #endif
            file.seekSet(seekSpecific);
         }
         return 0;
      }
      return 0;
   }

   //Check if we can short-cut because its a fixed width file
   if(jazaFiles[fileType].fixedWidth){
      // myLog.trace("Getting fixed width entry byte thingy");
      //Save current entry position
      unsigned int origPos = file.curPosition();
      //Figure out start point of entry 1 and entry 2
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.rewind() - L%u", __LINE__);
      #endif
      file.rewind();
      if(!skipPastNextDelimiter(fileType, entryDelimiter)){
         // printTrace(myLog, __LINE__, mes_skipping);
         return 0;
      }
      //Save entry 1 start point
      unsigned int firstEntryStartByte = file.curPosition();
      //See if we already passed the target point!
      if(firstEntryStartByte > origPos){
         // printTrace(myLog, __LINE__, mes_skipping);
         // myLog.trace("firstEntryStartByte = %u | origPos = %u" , firstEntryStartByte, origPos);
         return 0;
      }
      //Skip to start of entry 2
      if(!skipPastNextDelimiter(fileType, entryDelimiter))  return 1;
      //Save entry 2 start point
      unsigned int secondEntrStartByte = file.curPosition();
      // //Now we know an entry width!
      // unsigned int entryWidth = secondEntrStartByte - firstEntryStartByte;
      //How many bytes from first entry start pos to targByte?
      int numBytesFromFirst = origPos - firstEntryStartByte;
      if(numBytesFromFirst < 0) return 0;
      //We have what we need to calulate the target entry number
      // unsigned int targEntryNum = 1 + (numBytesFromFirst / entryWidth);
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.seekSet(origPos) - L%u", __LINE__);
      #endif
      file.seekSet(origPos);

      // myLog.trace(" (line : %u) firstEntryStartByte = %u | secondEntrStartByte = %u | entryWidth = %u | numBytesFromFirst = %d | targEntryNum = %u",
      //             __LINE__,
      //             firstEntryStartByte,
      //             secondEntrStartByte,
      //             entryWidth,
      //             numBytesFromFirst,
      //             targEntryNum
      //          );

      return ( 1 + ( (origPos - firstEntryStartByte) / (secondEntrStartByte - firstEntryStartByte)) );
   }

   // myLog.trace("finding num entries before position %lu", file.curPosition());

   return ( numTargDelimitersBeforePosition(fileType, entryDelimiter) );
}




/*=============================================>>>>>
= CHANGE ENTRY FUNCTION =
this function changes an entry in a file.  Involves loading remainder of file into RAM
and dynamic RAM (if file is too big for static sdBuf).  Slightly complex stuff
===============================================>>>>>*/

bool JazaSD::replaceEntry(JAZA_FILES_t fileType, uint32_t entryNum, const char* newEntry, bool deleteOperation){
   //Determine if this is a delete operation
   // bool deleteOperation = true;
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("replaceEntry(\"%s\")", jazaFiles[fileType].name);
   #endif

   if(newEntry != 0){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.info("Setting deleteOperation = false");
      #endif
      deleteOperation = false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   if(deleteOperation == true){
      printInfo(myLog, __LINE__, "DELETE OPERATION!");
   }
   #endif

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("Changing entry %u to \"%s\"", (unsigned int)entryNum, newEntry);
   //  Debug trace
   char tempBuf[200];
   snprintf(tempBuf, 50, "changeEntry( entry # %lu )", entryNum);
   printInfo(myLog, __LINE__, tempBuf);
   myLog.trace("Entry is: \"%s\"", newEntry);
   #endif


   //Open the file
   if(!smartFileOpen(fileType)) return false;


   //Locate starting byte of entry to replace
   if(entryNum == 0){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.rewind() - L%u", __LINE__);
      #endif
      file.rewind();
   }
   //Set the file position to the target entry
   else if( !gotoEntry(fileType, entryNum) ){
      printError(myLog, __LINE__, mes_sd_fileSeekError);
      return false;
   }


   //Save the file position at which the target entry to be replaced starts
   uint32_t targEntryStart = file.curPosition();
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("Entry to change starts at %lu", targEntryStart);
   #endif

   skipPastNextDelimiter(fileType, entryDelimiter);
   //Save place where target entry ends
   uint32_t targEntryEnd = file.curPosition();

   //1) See if the entry to replace current entry is same length as current entry
   unsigned int newEntrySizeNoDelim = strlen(newEntry);
   uint32_t newEntrySize = newEntrySizeNoDelim + strlen(entryDelimiter);
   uint32_t oldEntrySize = targEntryEnd - targEntryStart;

   int writeResult = 0;

   if( newEntrySize == oldEntrySize ){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      printInfo(myLog, __LINE__, "Overwriting entry (simple!)");
      #endif
      //We can simply overwrite the old entry!
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.seekSet(targEntryStart) - L%u", __LINE__);
      #endif
      file.seekSet(targEntryStart);
      writeResult = file.write(newEntry);
      writeResult = file.write(entryDelimiter);

      //Set file position back to original entry it was at
      // file.seekSet(origPos);
      return syncFile(__LINE__);

   }

   //2) Try and load rest of SD file and shift it manually

   //Set file position
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekSet(targEntryEnd) - L%u", __LINE__);
   #endif
   file.seekSet(targEntryEnd);
   //Save original file size
   uint32_t oldFileSize = file.fileSize();
   //Calculate how much data needs to be shifted
   uint32_t file_chars_to_shift = oldFileSize - targEntryEnd;
   //Debug output
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("TOTAL CHARS TO BE SHIFTED = %lu", file_chars_to_shift);
   #endif
   //Shift as many characters as possible into sdBuf
   int static_RAM_chars_stored = file.read(sdBuf, (SD_BUF_SIZE - 1));   //Save null-terminating character
   //Null terminate the sdBuf
   sdBuf[static_RAM_chars_stored] = '\0';

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("sdBuf[] is \"%s\"", getEscapedStr(sdBuf));
   #endif

   if((static_RAM_chars_stored < 0)  || ( abs(static_RAM_chars_stored) > (SD_BUF_SIZE-1)) ){
      printError(myLog, __LINE__, mes_sd_readError);
      // myLog.error("Static read error!");
      return false;
   }


   /*----------- STATIC MEMORY ONLY -----------*/
   //Change the entry (finally!)
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("Truncating file to byte %lu", targEntryStart);
   #endif
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.truncate() on L%u", __LINE__);
   #endif
   if(!file.truncate(targEntryStart)){
      // printError(myLog, __LINE__, mes_sd_truncating, mes_err_thrown);
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekSet(targEntryStart) - L%u", __LINE__);
   #endif
   file.seekSet(targEntryStart);
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("Done truncating.  File size now %lu", file.fileSize());
   #endif
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   printFile(fileType, true);
   #endif
   uint32_t bytesWritten = 0;
   uint32_t bytesReWritten = 0;
   writeResult = 0;


   // myLog.trace("Doing a new entry in %s", jazaFiles[fileType].name );

   // if(newEntrySizeNoDelim > 0){
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   if((newEntry == 0)&&(deleteOperation == false)){
      printError(myLog, __LINE__, "WHAT THE FUCK");
   }
   #endif

   if(deleteOperation == false) {
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.trace("Filing entry (newEntrySizeNoDelim == %u)", newEntrySizeNoDelim);
      #endif
      writeResult = file.write(newEntry);
      if(writeResult < 0){
         printError(myLog, __LINE__, mes_sd_writeError);
      }
      bytesWritten += writeResult;
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.write(entryDelimiter) - L%u", __LINE__);
      #endif
      file.write(entryDelimiter);
      // wroteDelimiter = true;
   }
   else{
      //Its a delete operation, overwrite bytes start at targ entry start position
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.seekSet(targEntryStart) - L%u", __LINE__);
      #endif
      file.seekSet(targEntryStart);
   }


   //Debug
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("Writing to byte #%lu --> \"%s\" ", file.curPosition(), getEscapedStr(sdBuf));
   #endif
   //Write to file
   writeResult = file.write( sdBuf, static_RAM_chars_stored );

   if(writeResult < 0){
      printError(myLog, __LINE__, mes_sd_writeError);
   }
   bytesReWritten += writeResult;

   if(bytesReWritten != file_chars_to_shift){
      // myLog.error("Unexpected number of characters shifted....   bytesReWritten = %lu  |  file_chars_to_shift = %lu", bytesReWritten, file_chars_to_shift);
      printError(myLog, __LINE__, mes_err_sanity);
   }


   // myLog.trace("Successfully performed static SD file entry manipulation!");
   //printInfo(myLog, __LINE__, mes_gen_success);

   return syncFile(__LINE__);


}



bool JazaSD::insertEntry(JAZA_FILES_t fileType, uint32_t entryNum, const char* newEntry){

   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("insertEntry(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Open the file
   if(!smartFileOpen(fileType)) return false;
   //Jump to the entry
   if( !gotoEntry(fileType, entryNum) ) return false;
   unsigned int entryStartPos = file.curPosition();
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.curPosition() - L%u", __LINE__);
   #endif
   file.curPosition();
   //Load rest of file into buffer
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.fileSize() - L%u", __LINE__);
   #endif
   unsigned int bytesToLoad = file.fileSize() - entryStartPos;
   if(bytesToLoad > (SD_BUF_SIZE-1)){
      printError(myLog, __LINE__, mes_buf_Small);
      return false;
   }
   //Read rest of file into RAM
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.read() - L%u", __LINE__);
   printFreeMem();
   #endif
   bytesToLoad = file.read(sdBuf, (SD_BUF_SIZE-1) );
   if( bytesToLoad < 0 ){
      printError(myLog, __LINE__, mes_sd_readError);
   }
   //Set file position
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekSet() - L%u", __LINE__);
   #endif
   if(!file.seekSet(entryStartPos)){
      printError(myLog, __LINE__, mes_sd_fileSeekError);
      return false;
   }
   //Write new entry

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.write() - L%u", __LINE__);
   #endif
   if( 0 > file.write(newEntry) ){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      printError(myLog, __LINE__, mes_sd_writeError);
      #endif
      return false;

   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.write() - L%u", __LINE__);
   #endif
   if( 0 > file.write(entryDelimiter) ){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      printError(myLog, __LINE__, mes_sd_writeError);
      #endif
      return false;
   }
   //Write rest of file in RAM back to file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.write() - L%u", __LINE__);
   #endif
   if( 0 > file.write(sdBuf, bytesToLoad) ){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      printError(myLog, __LINE__, mes_sd_writeError);
      #endif
      return false;
   }

   //Made it to here... must have been successful!
   return syncFile(__LINE__);

}


/**
*
* This function searches through the file for given string and returns a pointer
to a char array containing the entire entry that the 1st matched string is in
*
*/

char* JazaSD::searchGetEntry(JAZA_FILES_t fileType, const char* searchStr, uint16_t targInstanceNum){


   if(!SD_INITIALIZED) return NULL;


   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("searchGetEntry(\"%s\")", jazaFiles[fileType].name);
   #endif

   // myLog.trace("Searching for entry with \"%s\" in it.  Searching for occurence #%d", getEscapedStr(searchStr), targInstanceNum);
   //myLog.trace(mes_gen_parsing);
   //initialize pass by reference return value
   //A variable we'll use throughout:
   uint8_t searchStrLen = strlen(searchStr);
   //Do some basic error checks
   if( !(searchStr && (searchStrLen>0) && (searchStrLen < sizeof(sdBuf)) ) ){
      printError(myLog, __LINE__, mes_inValid);
      return NULL;
   }
   //Open the file if need be
   if(!smartFileOpen(fileType)) return NULL;
   // //Set seek position to first entry
   // if(!gotoEntry(fileType, 1)) return NULL;
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.rewind() - L%u", __LINE__);
   #endif
   file.rewind();
   //Start looping through the bytes in the file
   uint16_t instancesEncountered = 0;
   int fileByte = 0;
   char fileChar[2] = {0};
   unsigned int startByte = 0;

   // unsigned int startTime = micros();

   ////myLog.trace("Beginning search from file position 0");
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.read() - L%u", __LINE__);
   printFreeMem();
   #endif
   while( (fileByte = file.read()) > 0 ){

      //For some reason we need to encode things this way
      snprintf(fileChar, sizeof(fileChar), "%c", fileByte);
      //Check if the character matches the 1st searchStr character
      if(fileChar[0] == searchStr[0]){

         //Set file position back 1 byte and do a bulk read the size of the matching string length
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.seekCur() - L%u", __LINE__);
         #endif
         if(!file.seekCur(-1)){
            // myLog.error("Failed to go back 1 byte in file");
            printError(myLog, __LINE__, mes_sd_fileSeekError);
            return NULL;
         }
         // myLog.trace("Matched first character at position %lu", file.curPosition());
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.curPosition() - L%u", __LINE__);
         #endif
         startByte = file.curPosition() ;
         //Bulk read
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.read() - L%u", __LINE__);
         printFreeMem();
         #endif
         int result = file.read(sdBuf, searchStrLen);
         //Result error checking
         if( result == (-1) || result < searchStrLen ){
            // myLog.error( (result<0) ? "Read error -1." : "End of file");
            printError(myLog, __LINE__, mes_sd_readError);
            return NULL;
         }
         //Null-terminate what we just read
         sdBuf[searchStrLen] = '\0';
         // myLog.trace("Trying to find:");
         // myLog.trace("\"%s\" string", getEscapedStr(searchStr));
         // myLog.trace("in");
         // myLog.trace("\"%s\" string", getEscapedStr(sdBuf));
         //Check if the passed searchStr is in the sdBuf
         const char* matchedStr = strstr(sdBuf, searchStr);
         if(matchedStr){
            //Increment tracker of instances encountered
            instancesEncountered++;
            //We have found the string!  Now we have to back track to the beginning of the entry!
            //Set file position to byte where matched string starts
            // file.seekSet(searchPos);
            // myLog.trace("Found instance # %u at position %u", instancesEncountered, (unsigned int)startByte);
            //Set file seek position to halfway between start and end positiong of mathced string
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
            myLog.info("file.seekSet() - L%u", __LINE__);
            #endif
            file.seekSet(startByte + (searchStrLen/2));
            //Check if this is the instance we're looking for
            if(instancesEncountered == targInstanceNum){

               // myLog.trace("Found search string after %lu micros", micros() - startTime);
               //Get the current entry number
               uint32_t entryNum = getCurrentEntryNumber(fileType);
               // myLog.trace("Got entry number after %lu micros", micros() - startTime);
               //  myLog.trace("Matching string in entry #%u", (unsigned int)entryNum);
               //Now we got em by the balls!
               return getEntry(fileType, entryNum);
            }
            else{
               ////myLog.trace("Skipping this instance");
               //Need to skip past this matched string and keep looking
               // file.seekCur(searchStrLen);
            }
         }
         // printTrace(myLog, __LINE__, "Not a match");
         //Keep seeking (loop!)
      }
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.read() - L%u", __LINE__);
      printFreeMem();
      #endif
   };//End while

   //myLog.trace("Reached end of file and found no match");
   return NULL;
}

/*= End of HELPER FUNCTIONS INSIDE A FILE =*/
/*=============================================<<<<<*/


//------------------------- CLASS FUNCION DEFINITIONS --------------------------




/*=============================================>>>>>
= INITIALIZATION FUNCTIONS =
===============================================>>>>>*/

bool SD_INITIALIZED = false;

void JazaSD::begin(){

   myLog.info("Initializing JazaSD");
   Serial.flush();


   #ifdef TEST_MODE_INCLUDE_LAB_EQUIPMENT_HEADERS
   setup_external_trigger_pin();
   #endif


   #ifdef TEST_MODE_SIMULATE_SD_INIT_ERROR
   SD_error_handler(__LINE__);
   return;
   #endif

   //If we are simulating an SD crash, we want to also simulate that the SD
   //card doesn't just work again after reset.... so throw error!
   #ifdef TEST_MODE_SIMULATE_SD_CRASH_AFTER_TIME
   if(SD_RECOVERY_ATTEMPT_ACTIVE){
      SD_error_handler(__LINE__);
      //done
      return;
   }
   #endif

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("Initializing SdFat");
   Serial.flush();
   #endif

   //Initialize the SD card object
   if (!sd.begin(chipSelect, my_spi_settings)) {
      //Initialization error
      SD_error_handler(__LINE__);
      //Done
      return;
   }

   // ready = true;

   //Setup dateTime callback
   SdFile::dateTimeCallback(dateTime);

   #ifdef TEST_MODE_WIPE_SD_ON_STARTUP
   delay(100);
   printWarning(myLog, __LINE__, "WIPING SD CARD!");
   nukeAllFiles();
   #endif

   #ifdef TEST_MODE_WIPE_SD_ON_COLD_BOOT
   if(SRAM_COLD_BOOT){
      delay(100);
      printWarning(myLog, __LINE__, "WIPING SD CARD!");
      nukeAllFiles();
   }
   #endif

   #ifdef TEST_MODE_PRINT_FILES_ON_STARTUP
   printFiles();
   #endif

   //Load the free space on the SD card into memory
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("Calculating free SD space");
   #endif

   #ifndef TEST_MODE_NO_SD_FREE_SPACE_CALCULATION_ON_STARTUP
   //If free space is available, the conclusion is that SD card has successfully been initialized!
   if(freeSpaceKB() > 0){
      SD_INITIALIZED = true;
      SD_RECOVERY_ATTEMPT_ACTIVE = FALSE;

      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
      myLog.info("Free space = %u", sd_free_space_KB);
      #endif

      #ifdef TEST_MODE_VERBOSE_SDFAT_DEBUG
      SD_FAT_DEBUG_ENABLED = true;
      #endif

   }
   #else
   SD_INITIALIZED = true;
   #endif

}



/*=============================================>>>>>
= END INITIALIZATION FUNCTIONS =
===============================================>>>>>*/


/*=============================================>>>>>
= JAZAFILE CONFIGURATION FUNCTIONS =
===============================================>>>>>*/

//Function to pass a pointer to the header string to use for the specified file type
void JazaSD::setHeaders(JAZA_FILES_t fileType, const char* headers, bool skipIfExists){
   if(!SD_INITIALIZED) return;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("setHeaders(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Check if the headers are already in the file!
   if( hasADelimiter(fileType, entryDelimiter) && skipIfExists){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.info("Skipping header insertion");
      #endif
      return;
   }
   //Open the file with the correct permissions
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   printWarning(myLog, __LINE__, mes_sd_printingHeaders, jazaFiles[fileType].name);
   #endif

   //Reaplace the headers
   replaceEntry(fileType,0, headers);

}


/*= End of JAZAFILE CONFIGURATION FUNCTIONS =*/
/*=============================================<<<<<*/


/*=============================================>>>>>
= SD File Management Chore Functions =
===============================================>>>>>*/

bool JazaSD::wipeFile(JAZA_FILES_t fileType){
   if(smartFileOpen(fileType)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.truncate() - L%u", __LINE__);
      #endif
      return file.truncate(0);
   }
   return false;
}

bool JazaSD::replaceFile(JAZA_FILES_t fileToReplace, JAZA_FILES_t replacementFile){
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.warn(
      "Replacing file contents of \"%s\" with contents of \"%s\"",
      jazaFiles[replacementFile].name,
      jazaFiles[fileToReplace].name
   );
   #endif
   //First delete target file
   if(sd.remove(jazaFiles[fileToReplace].name)){
      //Then rename the replacement file to target file's name
      if( sd.rename(jazaFiles[replacementFile].name, jazaFiles[fileToReplace].name) ){
         return true;
      }
   }
   return false;
}


void JazaSD::printFile(JAZA_FILES_t fileType, bool printEscapedChars){
   if(!SD_INITIALIZED) return;

   if(!smartFileOpen(fileType)) return;

   //Save the current read position of the file in order to set it back to this later
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.curPosition() - L%u", __LINE__);
   #endif
   uint32_t origPos = file.curPosition();
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.rewind() - L%u", __LINE__);
   #endif
   file.rewind();

   Serial.printlnf("--FILE CONTENTS FOR \"%s\" --", jazaFiles[fileType].name);
   int fileByte;
   char fileChar[2];
   //Loop through the file one byte at a time
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.read() - L%u", __LINE__);
   printFreeMem();
   #endif
   while( (fileByte = file.read() ) > 0){
      snprintf(fileChar, sizeof(fileChar), "%c", fileByte);
      if(printEscapedChars){
         const char* toPrint = getEscapedCharString(fileByte);
         Serial.print(toPrint);
      }
      else{
         Serial.print(fileChar);
      }
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.read() - L%u", __LINE__);
      printFreeMem();
      #endif
   };
   if(printEscapedChars){
      Serial.println();
   }

   printHeaderBreak("END OF FILE");
   // Serial.printlnf("-------END OF FILE \"%s\" ---------", getOpenFileName());

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekSet() - L%u", __LINE__);
   #endif
   file.seekSet(origPos);

}


void JazaSD::printFiles(){
   for(unsigned int count = 0; count < NUM_TYPES_JAZA_FILES; count++){
      printFile(static_cast<JAZA_FILES_t>(count));
   }
}


#ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
void JazaSD::printFileStructure(FatFile* baseDir, unsigned int indent){

   if(!SD_INITIALIZED) return;

   if(!baseDir){
      baseDir = sd.vwd();
      printHeaderBreak("SD FILE STRUCTURE");
   }

   baseDir->rewind();

   FatFile workerFile;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("workerFile.openNext() - L%u", __LINE__);
   #endif
   while (workerFile.openNext(baseDir, O_READ)) {
      // gotIntoLoop = true;
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.printFileSize() - L%u", __LINE__);
      #endif
      workerFile.printFileSize(&Serial);
      Serial.print("   ");
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.printModifyDateTime() - L%u", __LINE__);
      #endif
      workerFile.printModifyDateTime(&Serial);
      Serial.print("   ");
      Serial.printf("%*s", indent, "");
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.printName() - L%u", __LINE__);
      #endif
      workerFile.printName(&Serial);
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.isDir() - L%u", __LINE__);
      #endif
      if (workerFile.isDir()) {
         // Indicate a directory.
         Serial.println('/');
         // #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
         // myLog.info("Recursing!");
         // #endif
         //Recurse
         printFileStructure(&workerFile, indent + 5);
      }
      Serial.println();
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("workerFile.close() - L%u", __LINE__);
      #endif
      workerFile.close();
   }//End while
}
#endif


bool JazaSD::copyFile(const char* sourcePath, const char* targPath){

   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }

   int targFileSize = 0;
   int bytesWritten = 0;
   int bytesRead = 0;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("smartFileClose() - L%u", __LINE__);
   #endif
   if(file.isOpen()) smartFileClose();
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("archiveFile.close() - L%u", __LINE__);
   #endif
   if(archiveFile.isOpen()) archiveFile.close();
   //Open the source file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("archiveFile.open() - L%u", __LINE__);
   #endif
   if(!archiveFile.open(sourcePath)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("Failed to open file \"%s\"", sourcePath);
      #endif
      return false;
   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("archiveFile.fileSize() - L%u", __LINE__);
   #endif
   targFileSize = archiveFile.fileSize();
   /*----------- Create the new archive file -----------*/
   //Open/create archive file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.open() - L%u", __LINE__);
   #endif
   if(!file.open(targPath, O_CREAT | O_READ | O_WRITE)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("Failed to create file \"%s\"", targPath);
      #endif
      return false;
   }
   //Truncate file if it contains data already
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.truncate() - L%u", __LINE__);
   #endif
   if(file.fileSize() > 0) file.truncate(0);
   /*----------- Copy data from source file to archive file -----------*/
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.warn("Copying %s to %s", sourcePath, targPath);
   myLog.warn("File is %u bytes", targFileSize);
   #endif
   bytesWritten = 0;

   while(bytesWritten < targFileSize){
      //Read source file into memory as much as possible
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("archiveFile.read() - L%u", __LINE__);
      printFreeMem();
      #endif
      bytesRead = archiveFile.read(sdBuf, SD_BUF_SIZE);
      //Check for error
      if(bytesRead <= 0){
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
         myLog.error("Failed to read bytes from source file %s.  bytesRead == %d", sourcePath, bytesRead);
         #endif
         SD_error_handler(__LINE__);
         return false;
      }
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.info("Read %u bytes from source file %s.  Now appending those bytes to %s", bytesRead, sourcePath, targPath);
      #endif
      //Write these bytes to the archive file
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.write() - L%u", __LINE__);
      #endif
      if(bytesRead != file.write(sdBuf, bytesRead) ){
         SD_error_handler(__LINE__);
         return false;
      }

      //Increment bytesWritten variable
      bytesWritten += bytesRead;

   };
   //Sync the archive file from RAM to the SD CARD
   //Dump our RAM cache to the new file on the SD card
   return syncFile(__LINE__);

   // if(!syncFile(__LINE__)){
   //    return false;
   // }
   //
   // return true;
}





unsigned int JazaSD::freeSpaceKB(){
   unsigned int clusterSize = 512L*sd.vol()->blocksPerCluster();
   int freeClusters = sd.vol()->freeClusterCount();
   if(freeClusters < 0) return 0;
   unsigned int clusterSizeKB = clusterSize/1024;
   sd_free_space_KB = (clusterSizeKB * freeClusters);
   return (unsigned int)(sd_free_space_KB);
}


#define FOLDER_PATH_BUF_SIZE 15
#define FILE_PATH_BUF_SIZE 50

bool JazaSD::archiveFiles(){
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }
   //Create a folder for the archive to live in=
   char folderPathBuf[FOLDER_PATH_BUF_SIZE] = {0};
   snprintf(folderPathBuf, FOLDER_PATH_BUF_SIZE, "%lu", the_time);
   if(!sd.mkdir(folderPathBuf)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("Failed to create archive directory \"%s\"", folderPathBuf);
      #endif
      return false;
   }



   char filePathBuf[FILE_PATH_BUF_SIZE] = {0};

   //Buffer to hold the archiveFilePath
   //Copy each file in the root directory to the newly created directory
   for(unsigned int count = 0; count < NUM_TYPES_JAZA_FILES; count++){
      //Compile the archive file name
      snprintf(filePathBuf, FILE_PATH_BUF_SIZE, "%s/%s", folderPathBuf, jazaFiles[count].name);

      #ifdef TEST_MODE_TWIDDLE_TEST_PIN_WHEN_COPYING_JP_TABLE
      if( count == FILE_JAZAPACKTABLE ){
         trigger_on_next_sd_write = true;
      }
      #endif


      #ifdef TEST_MODE_TWIDDLE_TEST_PIN_WHEN_COPYING_PUBHISTORY
      if( count == FILE_PUBLISH_HISTORY ){
         trigger_on_next_sd_read = true;
      }
      #endif

      if(copyFile(jazaFiles[count].name, filePathBuf)){
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
         myLog.info("Created archive %s", filePathBuf);
         #endif
      }

   }//End FOR each file loop
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("archiveFiles() -> Success!");
   #endif

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("smartFileClose() - L%u", __LINE__);
   #endif
   smartFileClose();

   #ifdef TEST_MODE_TWIDDLE_TEST_PIN_WHEN_COPYING_JP_TABLE
   external_trigger_reset();
   #endif

   //Return
   return true;
}


bool JazaSD::eraseArchives(unsigned int beforeDate){
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }
   //Array to store unix timestamps of archive dates
   unsigned int archiveFoldersFound = 0;
   unsigned int archiveFoldersDeleted = 0;
   char filePathBuf[FILE_PATH_BUF_SIZE] = {0};
   unsigned int timeStamp = 0;

   // #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
   // myLog.info("smartFileClose() - L%u", __LINE__);
   // #endif
   if(file.isOpen()) if(!smartFileClose()) return false;

   sd.vwd()->rewind();

   // #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
   // myLog.info("file.openNext() - L%u", __LINE__);
   // #endif
   while (file.openNext(sd.vwd(), O_READ)) {
      // #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
      // myLog.info("file.isDir() - L%u", __LINE__);
      // #endif
      if (file.isDir()) {
         //Get the directory name
         // #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
         // myLog.info("file.getName() - L%u", __LINE__);
         // #endif
         file.getName(filePathBuf, 50);
         // Indicate a directory.
         #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
         // myLog.info("file.printFileSize() - L%u", __LINE__);
         file.printName(&Serial);
         Serial.print(" | ");
         file.printFileSize(&Serial);
         Serial.print(" bytes | ");
         Serial.print(" ");
         // myLog.info("file.printModifyDateTime() - L%u", __LINE__);
         file.printModifyDateTime(&Serial);
         Serial.print(" | ");
         Serial.print(filePathBuf);
         Serial.print(" | ");
         #endif
         // Check if this directory has a name that is a unix timestamp
         if(1 == sscanf(filePathBuf, "%u", &timeStamp)){
            if(timeStamp > 1300000000 && timeStamp < 2220000000){ //2011 to 2040
               //Found an archive file
               archiveFoldersFound++;
               #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
               Serial.print("--> unix folder confirmed");
               #endif
               if(
                  (timeStamp < beforeDate) ||
                  //erase all archive files if no beforeDate provided
                  (beforeDate == 0)
               ){
                  //Delete this archive folder
                  #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
                  // myLog.info("file.rmRfStar() - L%u", __LINE__);
                  Serial.print("--> deleting...");
                  #endif
                  if(!file.rmRfStar()){
                     // #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
                     // myLog.error("Failed to recursively delete archive directory");
                     // #endif
                     Serial.print(" --> error!");
                     return false;
                  }
                  //Increment archiveFolders removed counter
                  archiveFoldersDeleted++;
                  #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
                  Serial.print("  ~~>DELETED ARCHIVE FOLDER!");
                  #endif
               }
            }
         }
         Serial.println();
      }
      //Finally, close the file --> IMPORTANT!
      // #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
      // myLog.info("smartFileClose() - L%u", __LINE__);
      // #endif
      smartFileClose();
   }

   #ifdef TEST_MODE_VERBOSE_ARCHIVE_DELETE
   myLog.info("Deleted %u of %u archive directories", archiveFoldersDeleted, archiveFoldersFound );
   #endif

   return true;

}




bool JazaSD::restoreArchive(unsigned int targStamp){

   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }
   //First thing's first... create an archive of current SD files
   unsigned int currentStamp = the_time;

   if(!archiveFiles()) return false;


   //Function that restores first archive after specified date
   unsigned int closestStamp = 0XFFFFFFF;
   unsigned int timeStamp = 0XFFFFFFFF;
   //Close file if open
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("smartFileClose() - L%u", __LINE__);
   #endif
   if(file.isOpen()) smartFileClose();
   //Rewind root directory to first file
   sd.vwd()->rewind();
   //Loop through each file/folder in root directory
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.openNext() - L%u", __LINE__);
   #endif
   while (file.openNext(sd.vwd(), O_READ)) {
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.isDir() - L%u", __LINE__);
      #endif
      if (file.isDir()) {
         //Get the directory name
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.getName() - L%u", __LINE__);
         #endif
         file.getName(filePathBuf, 50);
         // Check if this directory has a name that is a unix timestamp
         if(1 == sscanf(filePathBuf, "%u", &timeStamp)){
            //Found an archive file
            if(
               (timeStamp > targStamp) &&
               ((timeStamp - targStamp) < (closestStamp - targStamp))
            ){
               closestStamp = timeStamp;
            }

         }//end if directory is named after a unix timestamp
      }// end if a directory
      //Close the file
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("smartFileClose() - L%u", __LINE__);
      #endif
      smartFileClose();
   }//end loop of file
   //Restore the files in the target archive
   if(closestStamp == currentStamp){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.info("Closest archive is the backup one just created!");
      #endif
      return false;
   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.info(
      "Closest archive is from %u aka %s ... ~~~>RESTORING!",
      closestStamp,
      getTimeStr(closestStamp, TIME_TYPE_LONG_VERBOSE)
   );
   #endif
   //Copy each file we need from archive
   char archiveFilePath[50];
   for(unsigned int count = 0; count < NUM_TYPES_JAZA_FILES; count++){
      //Compile archive file name
      snprintf(archiveFilePath, 50, "%u/%s", closestStamp, jazaFiles[count].name);
      //copy that file to root
      if(!copyFile(archiveFilePath, jazaFiles[count].name)){
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
         myLog.error("Failed to restore archive file %s", archiveFilePath);
         #endif
         return false;
      }
   }

   return true;

}




/*= End of SD File Management Chore Functions =*/
/*=============================================<<<<<*/





/*=============================================>>>>>
= SD File Entry Function =
===============================================>>>>>*/

//Function that checks for an occurence of passed delimiter, scanning forward from start of file
bool JazaSD::hasADelimiter(JAZA_FILES_t fileType, const char* targDelimiter){
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("hasADelimiter(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Open the file
   smartFileOpen(fileType);
   //Seek to beginning of file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.rewind() - L%u", __LINE__);
   #endif
   file.rewind();
   //Check for one delimiter
   uint32_t result = skipPastNextDelimiter(fileType, targDelimiter);
   //Return as appropriate
   if(result > 0){
      return true;
   }
   else{
      return false;
   }
}


//Function that sets the file position to the beginning of the specified entry
bool JazaSD::gotoEntry(JAZA_FILES_t fileType, uint32_t entryNum){

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.info("gotoEntry()");
   #endif


   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }
   //// printTrace(myLog, __LINE__, "gotoEntry()");
   if(!smartFileOpen(fileType)) return false;
   //Debug
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("gotoEntry(%lu) in %s", entryNum, jazaFiles[currentlyOpenFile].name);
   #endif
   //Set the file position to start of file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.rewind() - L%u", __LINE__);
   #endif
   file.rewind();
   if(entryNum == 0) return true;
   //Shortcut method for fixed-width encoded files:
   if(jazaFiles[fileType].fixedWidth){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
      myLog.trace("Fixed width");
      #endif
      //// printTrace(myLog, __LINE__, mes_sd_gotoEntry, mes_sd_fixedWidth);
      //Figure out first entry start pos
      if(skipPastNextDelimiter(fileType, entryDelimiter) > 0){
         //File pos is set to the first entry start position
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.curPosition() - L%u", __LINE__);
         #endif
         unsigned int firstEntryStartPos = file.curPosition();
         //Seek ahead to end of this first entry
         if(skipPastNextDelimiter(fileType, entryDelimiter) > 0){
            //File pos is set to end of first entry
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
            myLog.info("file.curPosition() - L%u", __LINE__);
            #endif
            unsigned int firstEntryEndPos = file.curPosition();
            //Figure out how long the first entry is
            unsigned int firstEntryLength = firstEntryEndPos - firstEntryStartPos;
            unsigned int targEntryStartPos = (firstEntryStartPos)
            + ( (entryNum - 1L) * firstEntryLength);
            int entryDelimiterLength = strlen(entryDelimiter);
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
            myLog.trace("First entry is bytes %u to %u therefore entry #%u starts at byte %u",
            firstEntryStartPos, firstEntryEndPos, (unsigned int)entryNum, targEntryStartPos);
            #endif
            //File size sanity check, first check to make sure that the file is big enough
            //to actually contain the target position requested
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
            myLog.info("file.fileSize() - L%u", __LINE__);
            #endif
            if(file.fileSize() < targEntryStartPos){
               printError(myLog, __LINE__, mes_sd_fileSeekError);
               return false;
            }
            //Fixed width sanity check: see if there is an entry delimiter immediately
            //preceding the calculated target entry start position
            //// printTrace(myLog, __LINE__, mes_sd_fixedWidthCheck, mes_chore_evaluating);
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
            myLog.info("file.seekSet() - L%u", __LINE__);
            #endif
            if( file.seekSet(targEntryStartPos - entryDelimiterLength) ){
               //Successfully set file position to just before last entry delimiter preceding target entry
               if(entryDelimiterLength > 9){
                  printError(myLog, __LINE__, mes_buf_Small);
                  return false;
               }
               char delimCheck[10] = {0};
               //read bytes
               #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
               myLog.info("file.read() - L%u", __LINE__);
               printFreeMem();
               #endif
               file.read(delimCheck, entryDelimiterLength);
               //null terminate
               delimCheck[entryDelimiterLength] = '\0';
               //check if delimiter
               if(strcmp(delimCheck, entryDelimiter) == 0){
                  //Debug
                  // printTrace(myLog, __LINE__, mes_sd_fixedWidthCheck, mes_gen_success);
                  //Checks out, a delimiter is located directly before the calculated start pos
                  #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
                  myLog.info("file.seekSet() - L%u", __LINE__);
                  #endif
                  file.seekSet(targEntryStartPos);
                  return true;
               }
               else{
                  //File must not be fixed width after all.
                  printWarning(myLog, __LINE__, mes_err_thrown, mes_sd_variableWidth);
                  //Proceed using old way...
               }
            }
            else{
               printError(myLog, __LINE__, mes_sd_fileSeekError);
            }
         }

      }
   }

   //Regular way for non-fixed width files
   //Skips the amount of lines specified
   //// printTrace(myLog, __LINE__, "calling skipPastNextDelimiters()");
   if( skipPastNextDelimiters(fileType, entryDelimiter, entryNum ) > 0 ){
      //// printTrace(myLog, __LINE__, "skipPastNextDelimiters() returned > 0");
      return true;
   }
   //// printTrace(myLog, __LINE__, "skipPastNextDelimiters() returned <= 0");
   //Haven't skipped anywhere... back where we started!
   return false;
}


//Function to return the byte number of the SD file at which the target entry begins
int JazaSD::getEntryStartByte(JAZA_FILES_t fileType, uint32_t entryNum){
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }
   if(gotoEntry(fileType, entryNum)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.curPosition() - L%u", __LINE__);
      #endif
      return file.curPosition();
   }
   return -1;
}


//Function to retrieve a single-line entry from the specified file
//into the sdBuf, and return a pointer to the sdBuf containing
//the retrieved data
char* JazaSD::getEntry(JAZA_FILES_t fileType, uint32_t entryNum){
   if(!SD_INITIALIZED) return NULL;

   static JAZA_FILES_t lastFileType = NUM_TYPES_JAZA_FILES;

   bool needsRewind = true;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.info("getEntry(%s, %lu)", jazaFiles[fileType].name, entryNum);
   #endif

   //If just getting the next entry, no need to rewind file and start at beginning!
   if(
      (entryNum == lastGetEntryNum + 1) &&
      (fileType == lastFileType)
   ){
      lastGetEntryNum++;
      needsRewind = false;
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
      myLog.info("quick getEntry() (consecutive)");
      myLog.info("file.curPosition() = %lu", file.curPosition());
      #endif
   }

   lastGetEntryNum = entryNum;
   lastGetEntryStartPos = -1;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("Attempting to get entry #%lu from file \"%s\"", entryNum, jazaFiles[fileType].name);
   #endif

   if(!smartFileOpen(fileType)){
      return NULL;
   }

   //Check if we are simply getting the next entry, or if a specific line number
   //has been passed
   if(needsRewind){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.rewind() - L%u", __LINE__);
      #endif
      file.rewind();
      //User passed a line number to get entry from
      // printTrace(myLog, __LINE__, "Calling gotoEntry()");
      //myLog.trace("Looking for entry #%lu", entryNum);
      // Serial.flush();
      //Jump to the desired entry
      if(!gotoEntry(fileType, entryNum)){
         //Couldn't go to that entry, it doesn't exist!
         // char tempBuf[500];
         // snprintf(tempBuf, 500, "Couldn't retrieve entry #%lu from \"%s\" because it doesn't exist!", entryNum, jazaFiles[fileType].name);
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
         printWarning(myLog, __LINE__, mes_sd_fileSeekError, jazaFiles[fileType].name);
         #endif
         return NULL;
      }
      // printTrace(myLog, __LINE__, "Successfully jumped to entry");
   }


   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.curPosition() - L%u", __LINE__);
   #endif
   uint32_t entryStartPos = file.curPosition();
   lastGetEntryStartPos = entryStartPos;
   uint32_t entryEndPos = 0;

   lastFileType = fileType;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.trace("Entry #%lu starts at %lu", entryNum, entryStartPos);
   #endif

   if(!skipPastNextDelimiter(fileType, entryDelimiter)){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
      myLog.trace("Entry #%lu ends at %lu", entryNum, entryEndPos);
      #endif
      return NULL;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.curPosition() - L%u", __LINE__);
   #endif
   entryEndPos = file.curPosition();

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.trace("bytesToRead = %lu - %lu",  entryStartPos, entryEndPos);
   Serial.flush();
   #endif

   uint32_t bytesToRead = entryEndPos - entryStartPos;

   if(bytesToRead > (SD_BUF_SIZE-1) ){
      printError(myLog, __LINE__, mes_buf_Small);
      return NULL;
   }

   ////myLog.trace("Setting file position to %lu", entryStartPos);
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekSet() - L%u", __LINE__);
   #endif
   file.seekSet(entryStartPos);

   // myLog.trace("Executing: file.read(sdBuf, bytesToRead) where bytesToRead == %lu", bytesToRead);
   // Serial.flush();

   //Read the entry into the sdBuffer
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.read() - L%u", __LINE__);
   printFreeMem();
   #endif
   if(file.read(sdBuf, bytesToRead) > 0){
      //Append the null terminating character to the buffer string
      sdBuf[bytesToRead] = '\0';
      ////myLog.trace("Successfully retrieved entry #%lu:", entryNum);
      //printInfo(myLog, __LINE__, mes_gen_success);
      // Serial.println(sdBuf);
      return sdBuf;
   }

   //Default return value
   return NULL;

}



bool JazaSD::getEntryObjAt(JAZA_FILES_t fileType, uint32_t startPos, JazaEntry_t &targEntry){

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("Attempting to get entry startin at byte %lu from file \"%s\"", startPos, jazaFiles[fileType].name);
   #endif
   //Open file
   if(!smartFileOpen(fileType)){
      return NULL;
   }
   //Determine if start position is even in the file
   if(startPos >= file.fileSize()){
      myLog.error("Requested entry start pos not in file - line %d", __LINE__);
      myLog.error("startPos = %lu but fileSize() = %lu", startPos, file.fileSize());
      return false;
   }
   targEntry.startPos = startPos;
   //Set file pointer to this byte
   if(!file.seekSet(startPos)){
      myLog.error("Seek error - line %d", __LINE__);
      return false;
   }
   //Skip past next delimiter
   if(!skipPastNextDelimiter(fileType, "\r\n")){
      myLog.error("Delim not in file - line %d", __LINE__);
   }
   //End of entry is here
   targEntry.endPos = file.curPosition();
   if(!file.seekSet(targEntry.startPos)){
      myLog.error("Seek error - line %d", __LINE__);
      return false;
   }
   uint16_t bytesToRead = targEntry.endPos - targEntry.startPos;
   //Fill the buffer with the goodness
   if(file.read(sdBuf, bytesToRead) > 0){
      //Append the null terminating character to the buffer string
      sdBuf[bytesToRead] = '\0';
      ////myLog.trace("Successfully retrieved entry #%lu:", entryNum);
      //printInfo(myLog, __LINE__, mes_gen_success);
      // Serial.println(sdBuf);
      targEntry.text = sdBuf;
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.info("Got entry:");
      targEntry.print();
      #endif
      return true;
   }

   return false;

}

/*=============================================>>>>>
= Function  to retrieve an entry object (like above, with context)=
===============================================>>>>>*/

JazaEntry_t JazaSD::getEntryObj(JAZA_FILES_t fileType, uint32_t entryNum){
   JazaEntry_t returnObj;
   if(!SD_INITIALIZED) return returnObj;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("getEntryObj(\"%s\")", jazaFiles[fileType].name);
   #endif

   returnObj.text = getEntry(fileType, entryNum);
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.curPosition() - L%u", __LINE__);
   #endif
   if(returnObj.text){
      returnObj.entryNum = entryNum;

      returnObj.startPos = file.curPosition() - strlen(returnObj.text);
   }
   else{
      printError(myLog, __LINE__, mes_sd_readError, jazaFiles[fileType].name);
   }
   return returnObj;
}


char* JazaSD::getLastEntry(JAZA_FILES_t fileType){
   if(!SD_INITIALIZED) return NULL;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("getLastEntry(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Figure out how many entries total are in the file
   int targEntryNum = numEntries(fileType);
   //Check to make sure there are entries in the file
   if(targEntryNum < 1){
      printError(myLog, __LINE__ , mes_sd_noEntries);
      return NULL;
   }
   return getEntry(fileType, targEntryNum);
}

JazaEntry_t JazaSD::getLastEntryObj(JAZA_FILES_t fileType){
   JazaEntry_t returnObj;
   if(!SD_INITIALIZED) return returnObj;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("getLastEntryObj(\"%s\")", jazaFiles[fileType].name);
   #endif
   //Figure out how many entries total are in the file
   int targEntryNum = numEntries(fileType);
   //Check to make sure there are entries in the file
   if(targEntryNum < 1){
      //printInfo(myLog, __LINE__ , mes_sd_noEntries);
   }
   returnObj.text =  getEntry(fileType, targEntryNum);
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.curPosition() - L%u", __LINE__);
   #endif
   if(returnObj.text){
      returnObj.entryNum = targEntryNum;
      returnObj.startPos = file.curPosition() - strlen(returnObj.text);
   }
   else{
      printError(myLog, __LINE__, mes_sd_readError);
      returnObj.entryNum = 0;
      returnObj.startPos = 0;
   }
   return returnObj;
}



JazaEntry_t JazaSD::searchGetEntryObj(JAZA_FILES_t fileType, const char* targStr, uint16_t instanceNum /*= 1*/){
   JazaEntry_t returnObj;
   if(!SD_INITIALIZED) return returnObj;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("searchGetEntryObj(\"%s\")", jazaFiles[fileType].name);
   #endif

   returnObj.text = searchGetEntry(fileType, targStr, instanceNum);
   if(returnObj.text){
      returnObj.startPos = lastGetEntryStartPos;
      returnObj.entryNum = lastGetEntryNum;
      // myLog.trace("lastGetEntryStartPos = %u | lastGetEntryNum =%u", lastGetEntryStartPos, lastGetEntryNum );
   }
   return returnObj;
}


//Function that prints the passed charString with appended entry delimiter to the corresponding fatFile
bool JazaSD::fileEntry(JAZA_FILES_t fileType, const char* entry){
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   myLog.trace("File following entry in \"%s\":", jazaFiles[fileType].name);
   myLog.trace("%s", entry );
   #endif

   if(!smartFileOpen(fileType)) return false;

   //Check if the file is bigger than the minimum size required for auto-archiving
   // if((file.fileSize() >= MIN_SIZE_AUTO_ARCHIVE) && jazaFiles[fileType].autoArchive){
   //    //Warn that this is happenening
   //myLog.warn("Auto-archiving file \"%s\" since it has exceeded maximum size of %d", jazaFiles[fileType].name, MIN_SIZE_AUTO_ARCHIVE);
   //    //Need to archive this file and start fresh before adding next entry
   //    if( !archiveLogFile(fileType) ){
   //myLog.error("Couldn't auto-archive \"%s\"... exiting (cannot add a new entry to already oversized file)", jazaFiles[fileType].name);
   //       return false;
   //    }
   //    smartCloseFile();
   // }

   //Set position to the end of the file
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.seekEnd() - L%u", __LINE__);
   #endif
   file.seekEnd();

   // int numberEntries = numEntries(fileType);

   // if(numberEntries < 0){
   //myLog.warn("Number of entries is %d!  Need to print header entry before filing an entry!", numberEntries);
   //    printHeaders(fileType);
   // }

   //Figure out if the entry ends with a field delimiter and clip it off, if needbe
   // if(strrchr(entry))


   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
   myLog.info("file.write() - L%u", __LINE__);
   #endif
   //Write the entry text
   int bytesWritten = file.write(entry);
   if(
      (bytesWritten >= 0)
      &&
      ((unsigned int)(bytesWritten) == strlen(entry))
   ){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.write() - L%u", __LINE__);
      #endif
      //Write the entry delimiter text
      bytesWritten = file.write(entryDelimiter);
      if(
         (bytesWritten >= 0)
         &&
         ((unsigned int)(bytesWritten) == strlen(entryDelimiter))
      ){
         //Sync the new entry to the SD card
         return syncFile(__LINE__);
      }
      else{
         myLog.error("Write error - line %u -- bytesWritten == %d", __LINE__, bytesWritten);
      }
   }
   else{
      myLog.error("Write error - line %u -- bytesWritten == %d", __LINE__, bytesWritten);
   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   // myLog.error("Failed to file entry!");
   printError(myLog, __LINE__, mes_sd_writeError);
   printWarning(myLog, __LINE__, entry, getEscapedStr(entryDelimiter));
   printFile(fileType);
   #endif
   return false;
}

/*=============================================>>>>>
= Function to overwrite specific bytes within a SD file =
===============================================>>>>>*/
bool JazaSD::overWriteBytes(JAZA_FILES_t fileType, uint32_t startByte, const char* replacementBytes){
   if(!SD_INITIALIZED){
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      myLog.error("No Init! L%u", __LINE__);
      #endif
      return false;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("overWriteBytes(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Debug trace
   // printTrace(myLog, __LINE__, mes_sd_overWritingBytes);
   //myLog.trace("Bytes to write == \"%s\"", getEscapedStr(replacementBytes));
   //Open the target file
   if(smartFileOpen(fileType)){
      //Determine if the bytes being overwritten are indeed all inside the file
      // uint32_t fileLength = file.fileSize();
      int replacementBytesLength = strlen(replacementBytes);

      //Check if the data to overwrite is outside the size of the file
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.fileSize() - L%u", __LINE__);
      #endif
      if(startByte > file.fileSize()) {
         // printTrace(myLog, __LINE__, "adding whitespace up to bytes to modify");
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.seekEnd() - L%u", __LINE__);
         #endif
         if(!file.seekEnd()) printError(myLog, __LINE__, mes_err_sanity);
         //Need to add blank bytes up to the target entry
         unsigned int count;
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.fileSize() - L%u", __LINE__);
         #endif
         for(count = 0; count < (startByte-file.fileSize()); count++){
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
            myLog.info("file.write() - L%u", __LINE__);
            #endif
            file.write(' ');
         }



      }
      //The replacement bytes won't overfill the file... proceed!
      //printInfo(myLog, __LINE__, mes_sd_overWritingBytes, jazaFiles[fileType].name);
      // char tempBuf[50];
      // snprintf(tempBuf, 50, "%s: %lu to %lu", mes_sd_overWritingBytes, startByte, (startByte + replacementBytesLength));
      //printInfo(myLog, __LINE__, tempBuf);
      //Set the file to the start byte of the write
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.seekSet() - L%u", __LINE__);
      #endif
      if(file.seekSet(startByte)){
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.write() - L%u", __LINE__);
         #endif
         int writeResult = file.write(replacementBytes);
         if( writeResult == replacementBytesLength){


            return syncFile(__LINE__);


            //Successfully wrote those bytes!
            // else{
            //    printError(myLog, __LINE__, mes_sd_syncError);
            // }
         }
         // else if(writeResult < 0){
         //    printError(myLog, __LINE__, mes_sd_writeError);
         // }
         // else{
         //    printError(myLog, __LINE__, mes_err_unknown);
         // }
      }
      // else{
      //    printError(myLog, __LINE__, mes_sd_fileSeekError);
      // }
   }
   // else{
   //    printError(myLog, __LINE__, mes_sd_fileOpenError);
   // }
   return false;
}

/*=============================================>>>>>
= Function to read a set number of bytes from a file =
===============================================>>>>>*/
char* JazaSD::readBytes(JAZA_FILES_t fileType, uint32_t startByte, uint32_t numReadBytes){
   if(!SD_INITIALIZED) return NULL;
   //Sanity check on buffer size
   if(SD_BUF_SIZE < (numReadBytes - 1)){
      printError(myLog, __LINE__, mes_buf_Small);
      return NULL;
   }

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("readBytes(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Open the target file
   if(smartFileOpen(fileType)){
      //Set file position
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.seekSet() - L%u", __LINE__);
      #endif
      if(file.seekSet(startByte)){
         //Read target bytes into buffer
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.read() - L%u", __LINE__);
         printFreeMem();
         #endif
         int bytesRead = file.read(sdBuf, numReadBytes);
         //Append null terminator
         sdBuf[bytesRead] = '\0';
         //Check if all bytes desired were read
         if( (unsigned int)(bytesRead) == numReadBytes){
            //Read all the bytes we were looking to read!
            return sdBuf;
         }
         else if(bytesRead < 0){
            printError(myLog, __LINE__, mes_sd_readError);
         }
         // else{
         //    printError(myLog, __LINE__, mes_sd_readError, mes_sd_endOfFile);
         // }
      }
      // else{
      //    printError(myLog, __LINE__, mes_sd_fileSeekError);
      // }
   }
   // else{
   //    printError(myLog, __LINE__, mes_sd_fileOpenError);
   // }
   return NULL;
}

/*=============================================>>>>>
= Function to return the number of bytes in the file =
===============================================>>>>>*/
uint32_t JazaSD::bytesInFile(JAZA_FILES_t fileType){
   if(!SD_INITIALIZED) return 0;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   myLog.trace("bytesInFile(\"%s\")", jazaFiles[fileType].name);
   #endif

   //Open the file
   if(smartFileOpen(fileType)){
      //Return the bytes in the file
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
      myLog.info("file.fileSize() - L%u", __LINE__);
      #endif
      return file.fileSize();
   }
   else{
      #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
      printError(myLog, __LINE__, mes_sd_fileOpenError);
      #endif
   }
   return 0;
}



//Function that returns the number of entries inside fatfile corresponding to passed file type
int JazaSD::numEntries(JAZA_FILES_t fileType){
   if(!SD_INITIALIZED) return -1;

   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
   myLog.trace("numEntries()");
   #endif
   //If file is fixed width, we just need to know first entry start pos, entry length, and file size
   if(jazaFiles[fileType].fixedWidth){

      //printInfo(myLog, __LINE__, mes_sd_findingNumEntries, mes_sd_fixedWidth);
      //Figure out first entry start pos
      if(gotoEntry(fileType, 1)){
         //File pos is set to the first entry
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.curPosition() - L%u", __LINE__);
         #endif
         uint32_t firstEntryPos = file.curPosition();
         //Figure out the length of an entry
         char* firstEntryContent = getEntry(fileType, 1);

         uint32_t firstEntryLength = strlen(firstEntryContent);
         //Get file size
         #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_SUPER_HEAVY_AF
         myLog.info("file.fileSize() - L%u", __LINE__);
         #endif
         uint32_t fileSize = file.fileSize();
         //Figure out number of entries
         uint32_t totalEntries = (fileSize - firstEntryPos)/*<--File bytes containing entries*/
         / (firstEntryLength);
         //Figure out if any bytes would be unaccounted for (fixed width check)
         uint32_t leftOverBytes = (fileSize - firstEntryPos)
         % (firstEntryLength);
         if(leftOverBytes == 0){
            #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY_AF
            myLog.trace("%lu entries", totalEntries);
            #endif
            return totalEntries;
         }
         else{
            //The fixed width method of determining number of entries failed because
            //the target file is not encoded as fixed width in reality
            printError(myLog, __LINE__, mes_sd_variableWidth);
            return -1;
            //Don't exit, we can still determine numEntries the old fashioned way (below)
         }
      }
      // else{
      //printInfo(myLog, __LINE__, mes_sd_noEntries);
      // }
   }
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG_HEAVY
   printInfo(myLog, __LINE__, mes_sd_findingNumEntries, mes_sd_variableWidth);
   #endif
   //File is not fixed width, have to count entry delimiters
   unsigned int numDelimiters = numDelimitersInFile(fileType, entryDelimiter);
   if(numDelimiters == 0) return 0;
   return (numDelimiters -1);

   //  int result = numDelimitersInFile(fileType, entryDelimiter);
   //  if(result<=0) return 0;
   //  return ( result - 1 );
}


/*= SD File Entry Functions =*/
/*=============================================<<<<<*/







/*= End of CONFIGURATION FILE FUNCTIONS =*/
/*=============================================<<<<<*/

/*= End of DATALOGGING FUNCTIONS =*/
/*=============================================<<<<<*/
