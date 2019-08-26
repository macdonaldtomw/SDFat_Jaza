//PARTICLE_STUDIO_READY


/**
 *
 *

This file contains implementations of class for the jazaSD utility, which allows
the rest of our app to easily read and write data to the SD card in a csv formatted way

 *
 */

//Header guards
#ifndef JazaSD_h
#define JazaSD_h

/*=============================================>>>>>
= Dependencies =
===============================================>>>>>*/
#include "myParticle.h"
#include "SD/SdFat/SdFat.h"
#include "TestingConfig.h"

/*=============================================>>>>>
= Configuration settings =
===============================================>>>>>*/
#define SD_BUF_SIZE 2049   //4 pages of SD memory (each page is 512 bytes)

//Declare externally linked buffer for writing to the SD card
extern char sdWriteBuf[SD_BUF_SIZE];


const char* const fieldDelimiter = ",";
const char* const entryDelimiter = "\r\n";


extern int sd_free_space_KB;

/*=============================================>>>>>
= Type definitions =
===============================================>>>>>*/

enum jazaConfig_t{
   CONFIG_LOCKOUT,
   CONFIG_CHARGEBAL,
   NUM_TYPEPS_JAZA_CONFIG
};

//Defines what type of error will be pubslished to the SD card
enum jazaError_t{
   GENERIC_ERROR,
   SIM_CARD_ERROR,
   CELL_CONNECTIVITY_ERROR,
   CLOUD_CONNECTIVITY_ERROR,
   PUBLISH_ERROR,
   SD_ARCHIVE_ERROR,
   NUM_TYPES_JAZA_ERROR   //Must always be the last item in enum!
};

// #define NUM_TYPES_JAZA_FILES 7
enum JAZA_FILES_t{
   FILE_CHANNEL_INFO,
   FILE_USERTABLE,
   FILE_JAZAPACKTABLE,
   FILE_QUEUE_TOCHARGE,
   FILE_QUEUE_CHARGED,
   FILE_HUB_PROPERTIES,
   FILE_JAZAOFFERINGS,
   FILE_PUBLISH_HISTORY,
   FILE_PUBLISH_REGISTRY,
   FILE_PUBLISH_BACKLOG,
   FILE_STORED_STRINGS,
   FILE_TEMP_FILE,
   FILE_JP_HEX_FILE,
   NUM_TYPES_JAZA_FILES //Must always be last item in enum!
};

// enum JAZA_FILE_MODE_t{
//    FILE_MODE_DATALOGGING_READ_WRITE,
//    FILE_MODE_DATALOGGING_WRITE_ONLY,
//    FILE_MODE_REFERENCE_READ_WRITE,
//    FILE_MODE_REFERENCE_READ_ONLY,
//    NUM_TYPES_FILE_MODE
// };

//
// // #define NUM_TYPES_JAZA_ARCHIVES 4
// enum JAZA_ARCHIVES_t{
//    ARCHIVE_DATA_LOGGING,
//    ARCHIVE_TRANSACTIONS,
//    ARCHIVE_EVENTS,
//    ARCHIVE_ERRORS,
//    ARCHIVE_PUBLISH,
//    NUM_TYPES_JAZA_ARCHIVES  //Must always be last item in enum!
// };


/*=============================================>>>>>
= Global variable with external linkage =
===============================================>>>>>*/
// char SD_publishBuffer


void nukeSD();

/*=============================================>>>>>
= JazaFile_t data structure =
===============================================>>>>>*/

//Date structure for holding data related to each file type in the jazaSD specification
struct JazaFile_t{
   JazaFile_t(const char* fileName, bool _fixedWidth = false){
      name = fileName;
      fixedWidth = _fixedWidth;
   }
   const char* name = NULL;
   bool fixedWidth = false;
   uint16_t headerLength = 0;
   // bool isOpen = false;
};

// /*=============================================>>>>>
// = JazaEntry data structure =
// ===============================================>>>>>*/
struct JazaEntry_t{
   unsigned int entryNum = 0;
   unsigned int startPos = 0;
   char* text = NULL;

   unsigned int endPos = 0;

   void print(){
      Serial.printlnf("JazaEntry_t:  entryNum = %u | startPos = %lu | endPos = %lu | text = \"%s\" ",
         entryNum, startPos, endPos, text);
   }

   void reset(){
      entryNum = 0;
      startPos = 0;
      endPos = 0;
      text = NULL;
   }
};


extern JazaFile_t jazaFiles[NUM_TYPES_JAZA_FILES];

/*=============================================>>>>>
= jazaPublish class =
===============================================>>>>>*/
class JazaSD{

public:

   /*=============================================>>>>>
   = Initialization functions =
   ===============================================>>>>>*/
   void begin();

   // void set_UI_error_message();

   /*=============================================>>>>>
   = Jaza File Configuration functions =
   ===============================================>>>>>*/
   void setHeaders(JAZA_FILES_t fileType, const char* headers, bool skipIfExists = true);

   /*=============================================>>>>>
   = SD File Management Chore Functions =
   ===============================================>>>>>*/
   bool wipeFile(JAZA_FILES_t fileType);
   void printFile(JAZA_FILES_t fileType, bool printEscapedChars = false);
   void printFiles();
   #ifdef TEST_MODE_VERBOSE_JAZASD_DEBUG
   void printFileStructure(FatFile* baseDir = NULL, unsigned int indent = 0);
   #endif

   bool archiveFiles();
   bool restoreArchive(unsigned int targStamp);
   bool eraseArchives(unsigned int beforeDate);
   unsigned int freeSpaceKB();

   bool replaceFile(JAZA_FILES_t fileToReplace, JAZA_FILES_t replacementFile);
   bool copyFile(const char* sourcePath, const char* targPath);


   /*=============================================>>>>>
   = SD File entry functions =
   ===============================================>>>>>*/

   bool hasADelimiter(JAZA_FILES_t fileType, const char* targDelimiter);

   bool gotoEntry(JAZA_FILES_t fileType, uint32_t entryNum);

   int getEntryStartByte(JAZA_FILES_t fileType, uint32_t entryNum = 0);

   //Entry based reads
   char* getEntry(JAZA_FILES_t fileType, uint32_t entryNum = 0);
   JazaEntry_t getEntryObj(JAZA_FILES_t fileType, uint32_t entryNum = 0);
   char* getLastEntry(JAZA_FILES_t fileType);
   JazaEntry_t getLastEntryObj(JAZA_FILES_t fileType);
   char* searchGetEntry(JAZA_FILES_t fileType, const char* targStr, uint16_t targInstanceNum);
   JazaEntry_t searchGetEntryObj(JAZA_FILES_t fileType, const char* targStr, uint16_t instanceNum = 1);

   bool getEntryObjAt(JAZA_FILES_t fileType, uint32_t startPos, JazaEntry_t &targEntry);

   //Entry based writes
   bool fileEntry(JAZA_FILES_t fileType, const char* entry);
   //Entry based modify
   bool replaceEntry(JAZA_FILES_t fileType, uint32_t entryNum, const char* newEntry = NULL, bool deleteOperation = true);
   //Entry based insert
   bool insertEntry(JAZA_FILES_t fileType, uint32_t entryNum, const char* newEntry);
   //Entry based delete
   inline bool deleteEntry(JAZA_FILES_t fileType, uint32_t entryNum){return replaceEntry(fileType, entryNum);}

   //Byte-based writes
   bool overWriteBytes(JAZA_FILES_t fileType, uint32_t startByte, const char* replacementBytes);
   // bool appendBytes(JAZA_FILES_t fileType, const char* bytesToAppend);
   //Byte-based read
   char* readBytes(JAZA_FILES_t fileType, uint32_t startByte, uint32_t numReadBytes);
   //Read number of bytes in file
   uint32_t bytesInFile(JAZA_FILES_t fileType);

   int numEntries(JAZA_FILES_t fileType);

   bool printHeaders(JAZA_FILES_t fileType);


   /*=============================================>>>>>
   = Publish backlog functions =
   ===============================================>>>>>*/
   // bool addToPublishBacklog(const char* entryTag, const char* pubBuf);
   // char* getPublishBacklogEntry(uint32_t entryNum, char* tagBuf, size_t tagBufSize);
   // bool deletePublishBacklogEntry(uint32_t entryNum);

   /*=============================================>>>>>
   = DataLogging Functions =
   ===============================================>>>>>*/
   // bool recordEvent(const char* eventMessage, const char* calledFrom);
   // bool recordError(const char* errorMessage, const char* calledFrom, jazaError_t errType);


   /*=============================================>>>>>
   = PUBLIC VARIABLES =
   ===============================================>>>>>*/

private:
   bool ready; //True if all systems check out when initializing jazaSD

};

//Externally linked global utiliity declaration
extern JazaSD jazaSD;
// extern File file;
extern SdFile file;

//Callback function that the SD fat library will use for keeping records
//such as date created, date modified, etc etc.
void dateAndTimeForFats(uint16_t* date, uint16_t *time);

#endif
